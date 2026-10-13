#!/usr/bin/env python3
"""Resumable, bounded Ghidra export of inventoried function-start addresses.

Inputs must be an authorized isolated binary and an existing -noanalysis Ghidra
project. Never executes the target. Output belongs in an ignored local directory.
One process handles a batch, with 4G heap / 2 CPUs. Interrupted batches are kept;
resume retries their unfinished addresses in NEW directories without overwriting
pseudocode. Completed failures/timeouts are coverage, not silently skipped work.

Example: GHIDRA_HOME=/path/to/ghidra JAVA_HOME=/path/to/jdk python3
scripts/bulk_decompile.py --inventory inventory.json --architecture arm64
--binary isolated.macho --project-dir projects --project-name Research
--program isolated.macho --output local-coverage --max-batches 4
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import sqlite3
import subprocess
import sys
import time

SCHEMA = 1
SCRIPT = Path(__file__).resolve().parent / 'ghidra' / 'BulkDecompileBatch.java'
STATUSES = {'completed', 'failed', 'timed_out', 'interrupted'}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def canonical(value):
    if not isinstance(value, str) or not re.fullmatch(r'(?:0[xX])?[0-9a-fA-F]{1,16}', value):
        raise ValueError('Invalid 64-bit address')
    return f'0x{int(value, 16):x}'


def read_json(path):
    # Reject duplicate keys; ordinary json.load would silently accept corruption.
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError(f'Duplicate JSON key: {key}')
            out[key] = value
        return out
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=pairs)


def write_new(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, sort_keys=True, separators=(',', ':'))
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())


def inventory(path, architecture):
    value = read_json(path)
    if value.get('schema_version') != 1:
        raise ValueError('Unsupported inventory schema')
    slices = [s for s in value['slices'] if s['architecture'] == architecture]
    if len(slices) != 1:
        raise ValueError('Inventory must have exactly one selected architecture')
    selected = slices[0]
    starts = selected['function_starts']
    addresses = [canonical(a) for a in starts['addresses']]
    if not starts['present'] or not addresses or starts['count'] != len(addresses):
        raise ValueError('Missing or inconsistent function-start inventory')
    if len(set(addresses)) != len(addresses):
        raise ValueError('Duplicate inventory addresses')
    if addresses != sorted(addresses, key=lambda a: int(a, 16)):
        raise ValueError('Function-start inventory is not ordered')
    return selected['sha256'], addresses


@contextmanager
def exclusive(path):
    """OS-released advisory lock: a killed coordinator cannot leave a stale lease."""
    with Path(path).open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            if stream.tell() == 0:
                stream.write(b'0')
                stream.flush()
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


class Coverage:
    def __init__(self, output, identity, addresses):
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        manifest = self.output / 'manifest.json'
        database = self.output / 'coverage.sqlite3'
        if manifest.exists():
            if read_json(manifest) != identity:
                raise ValueError('Coverage manifest does not match input/program/tool identity')
            if not database.exists():
                raise ValueError('Existing manifest is missing its coverage database')
        else:
            if database.exists() or (self.output / 'batches').exists():
                raise ValueError('Refusing to initialize over existing coverage artifacts')
            write_new(manifest, identity)
        self.db = sqlite3.connect(database)
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.execute('PRAGMA foreign_keys=ON')
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS functions(
              ordinal INTEGER PRIMARY KEY, address TEXT UNIQUE NOT NULL,
              status TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,
              batch INTEGER, detail TEXT);
            CREATE INDEX IF NOT EXISTS pending ON functions(status, ordinal);
            CREATE TABLE IF NOT EXISTS batches(
              id INTEGER PRIMARY KEY, request TEXT NOT NULL, state TEXT NOT NULL,
              process_status TEXT, elapsed REAL);
        ''')
        count = self.db.execute('SELECT COUNT(*) FROM functions').fetchone()[0]
        if count == 0:
            with self.db:
                self.db.executemany('INSERT INTO functions(ordinal,address,status) VALUES(?,?,?)',
                                    ((i, a, 'unattempted') for i, a in enumerate(addresses)))
        elif count != len(addresses):
            raise ValueError('Coverage database inventory count mismatch')
        elif [r[0] for r in self.db.execute('SELECT address FROM functions ORDER BY ordinal')] != addresses:
            raise ValueError('Coverage database inventory addresses mismatch')
        if self.db.execute('PRAGMA quick_check').fetchone()[0] != 'ok':
            raise ValueError('Corrupt coverage database')
        if self.db.execute("SELECT COUNT(*) FROM functions WHERE status NOT IN ('unattempted','completed','failed','timed_out','interrupted')").fetchone()[0]:
            raise ValueError('Invalid coverage database status')
        (self.output / 'batches').mkdir(exist_ok=True)
        self.identity = identity

    def close(self):
        self.db.close()

    def summary(self):
        counts = {s: 0 for s in ('unattempted', 'completed', 'failed', 'timed_out', 'interrupted')}
        counts.update(dict(self.db.execute('SELECT status,COUNT(*) FROM functions GROUP BY status')))
        counts['total'] = sum(counts.values())
        counts['attempt_events'] = self.db.execute('SELECT SUM(attempts) FROM functions').fetchone()[0]
        return counts

    def next_batch(self, size, preparation, decompile):
        # Retry interrupted attempts once in the next invocation, not in a tight loop.
        rows = list(self.db.execute("SELECT address FROM functions WHERE status IN ('unattempted','interrupted') ORDER BY ordinal LIMIT ?", (size,)))
        if not rows:
            return None
        request = {'schema_version': SCHEMA, 'program_sha256': self.identity['program_sha256'],
                   'addresses': [r[0] for r in rows], 'preparation_seconds': preparation,
                   'decompile_seconds': decompile}
        with self.db:
            cursor = self.db.execute('INSERT INTO batches(request,state) VALUES(?,?)',
                                     (json.dumps(request, separators=(',', ':')), 'running'))
            batch_id = cursor.lastrowid
        folder = self.output / 'batches' / f'{batch_id:08d}'
        folder.mkdir()
        write_new(folder / 'request.json', request)
        return batch_id, folder

    def ingest(self, batch_id, process_status='interrupted', elapsed=0):
        row = self.db.execute('SELECT request,state FROM batches WHERE id=?', (batch_id,)).fetchone()
        if row is None:
            raise ValueError('Unknown batch')
        if row[1] == 'ingested':
            return
        request = json.loads(row[0])
        folder = self.output / 'batches' / f'{batch_id:08d}'
        request_path = folder / 'request.json'
        if request_path.exists() and read_json(request_path) != request:
            raise ValueError('Corrupt batch request')
        metadata = folder / 'metadata.json'
        journal = folder / 'events.jsonl'
        if metadata.exists():
            meta = read_json(metadata)
            if meta.get('program_sha256') != self.identity['program_sha256'] or meta.get('request_sha256') != digest(request_path):
                raise ValueError('Batch provenance mismatch')
            if meta.get('broad_auto_analysis') is not False or meta.get('requested_count') != len(request['addresses']):
                raise ValueError('Invalid batch metadata')
        elif journal.exists():
            raise ValueError('Journal has no provenance metadata')
        started, results = set(), {}
        finished = False
        if journal.exists():
            raw = journal.read_bytes()
            lines = raw.splitlines(keepends=True)
            for i, line in enumerate(lines):
                if not line.endswith(b'\n'):
                    if i != len(lines)-1:
                        raise ValueError('Truncated internal journal record')
                    # A killed write may leave a final partial record; retain it verbatim.
                    break
                try:
                    value = json.loads(line)
                except (ValueError, UnicodeError) as error:
                    raise ValueError('Corrupt complete journal record') from error
                if finished:
                    raise ValueError('Journal event after finished')
                if value.get('event') == 'finished':
                    if value.get('result_count') != len(results):
                        raise ValueError('Finished count mismatch')
                    finished = True
                    continue
                address = value.get('address')
                if address not in request['addresses']:
                    raise ValueError('Journal address outside requested batch')
                if value.get('event') == 'started':
                    if address in started:
                        raise ValueError('Duplicate started address')
                    started.add(address)
                elif value.get('event') == 'result':
                    if address not in started or address in results or value.get('status') not in STATUSES:
                        raise ValueError('Duplicate, missing-start, or invalid result')
                    if value['status'] == 'completed':
                        if value.get('output_file') != address+'.c':
                            raise ValueError('Unexpected output filename')
                        code = folder / value['output_file']
                        if code.is_symlink() or not code.is_file() or code.stat().st_size == 0 or digest(code) != value.get('output_sha256'):
                            raise ValueError('Missing, empty, or corrupt pseudocode output')
                    results[address] = value
                else:
                    raise ValueError('Unknown journal event')
        with self.db:
            for address in started:
                result = results.get(address, {'status': 'interrupted', 'stage': 'process',
                                              'error': 'Process ended without a durable result'})
                old_status = self.db.execute('SELECT status FROM functions WHERE address=?', (address,)).fetchone()[0]
                if old_status not in ('unattempted', 'interrupted'):
                    raise ValueError('Refusing to replace a terminal result')
                self.db.execute('UPDATE functions SET status=?,attempts=attempts+1,batch=?,detail=? WHERE address=?',
                                (result['status'], batch_id, json.dumps(result), address))
            self.db.execute('UPDATE batches SET state=?,process_status=?,elapsed=? WHERE id=?',
                            ('ingested', process_status, elapsed, batch_id))
        return len(started), len(results), finished

    def recover(self):
        for row in list(self.db.execute("SELECT id FROM batches WHERE state='running' ORDER BY id")):
            self.ingest(row[0])


def stop_process(process):
    if process.poll() is not None:
        return
    if os.name == 'nt':
        process.terminate()
    else:
        os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        if os.name == 'nt':
            process.kill()
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=10)


def run_process(command, folder, timeout):
    environment = dict(os.environ, GHIDRA_HEADLESS_MAXMEM='4G')
    start = time.monotonic()
    with (folder / 'headless.log').open('xb') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   env=environment, start_new_session=(os.name != 'nt'))
        try:
            code = process.wait(timeout=timeout)
            status = f'exit:{code}'
        except subprocess.TimeoutExpired:
            stop_process(process)
            status = 'process_timeout'
        except BaseException:
            stop_process(process)
            raise
    return status, time.monotonic()-start


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--architecture', default='arm64')
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--project-dir', type=Path, required=True)
    parser.add_argument('--project-name', required=True)
    parser.add_argument('--program', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ghidra-home', type=Path, default=os.environ.get('GHIDRA_HOME'))
    parser.add_argument('--batch-size', type=int, default=256)
    parser.add_argument('--max-batches', type=int, default=1)
    parser.add_argument('--preparation-seconds', type=int, default=10)
    parser.add_argument('--decompile-seconds', type=int, default=20)
    parser.add_argument('--process-timeout', type=int, default=600)
    parser.add_argument('--status', action='store_true', help='validate provenance and print counts without launching Ghidra')
    args = parser.parse_args(argv)
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'Interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    if not 1 <= args.batch_size <= 4096 or not 1 <= args.max_batches <= 10000:
        parser.error('batch-size must be 1..4096 and max-batches 1..10000')
    if not 1 <= args.preparation_seconds <= 120 or not 1 <= args.decompile_seconds <= 120 or not 5 <= args.process_timeout <= 3600:
        parser.error('per-function timeouts must be 1..120 and process timeout 5..3600 seconds')
    expected, addresses = inventory(args.inventory, args.architecture)
    if digest(args.binary) != expected:
        raise ValueError('Binary SHA256 does not match selected inventory slice')
    project_dir = args.project_dir.resolve()
    if not (project_dir / (args.project_name+'.gpr')).is_file():
        raise ValueError('Existing Ghidra project is required; import separately with -noanalysis')
    identity = {'schema_version': SCHEMA, 'program_sha256': expected,
                'inventory_sha256': digest(args.inventory), 'architecture': args.architecture,
                'address_count': len(addresses), 'project_dir': str(project_dir),
                'project_name': args.project_name, 'program': args.program,
                'exporter_sha256': digest(SCRIPT), 'broad_auto_analysis': False}
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    # Project-wide coordinator lock prevents two different output runs mutating one project.
    with exclusive(project_dir / (args.project_name+'.bulk.lock')), exclusive(args.output / '.lock'):
        coverage = Coverage(args.output, identity, addresses)
        try:
            coverage.recover()
            print(json.dumps({'coverage': coverage.summary()}), flush=True)
            if args.status:
                return 0
            if args.ghidra_home is None:
                raise ValueError('GHIDRA_HOME or --ghidra-home is required; tools are never installed automatically')
            launcher = Path(args.ghidra_home).resolve() / 'support' / ('analyzeHeadless.bat' if os.name == 'nt' else 'analyzeHeadless')
            if not launcher.is_file():
                raise ValueError('Ghidra headless launcher not found')
            for _ in range(args.max_batches):
                batch = coverage.next_batch(args.batch_size, args.preparation_seconds, args.decompile_seconds)
                if batch is None:
                    break
                batch_id, folder = batch
                command = [str(launcher), str(project_dir), args.project_name, '-process', args.program,
                           '-noanalysis', '-max-cpu', '2', '-scriptPath', str(SCRIPT.parent),
                           '-postScript', SCRIPT.name, str(folder/'request.json')]
                status, elapsed = run_process(command, folder, args.process_timeout)
                started, done, finished = coverage.ingest(batch_id, status, elapsed)
                print(json.dumps({'batch': batch_id, 'process_status': status, 'elapsed_seconds': round(elapsed, 3),
                                  'started': started, 'results': done, 'finished': finished,
                                  'coverage': coverage.summary()}), flush=True)
                if status != 'exit:0' or not finished or started != done:
                    # Stop this invocation; explicit resume preserves evidence and retries unfinished entries.
                    return 2
                if started == 0:
                    raise ValueError('Batch made no progress; inspect its retained log')
            return 0
        finally:
            coverage.close()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, sqlite3.Error) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        raise SystemExit(1)
