#!/usr/bin/env python3
"""Verify and combine frozen read-only snapshots of bounded extraction runs.

Each --run takes a coverage directory and its exact (possibly subset) inventory.
Every completed file is hash checked, including overlapping successful retries.
Unique address coverage uses any verified success, otherwise the last attempted
status in the supplied run order. Unindexed journals are excluded explicitly.
No target code is executed, no analysis project is opened, and no input changes.
This measures tool-output integrity, not semantic recovery or behavior parity.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import sqlite3

from bulk_decompile import decode_json, digest, inventory

STATUSES = ('completed', 'failed', 'timed_out', 'interrupted', 'unattempted')


def counts(values):
    result = dict.fromkeys(STATUSES, 0)
    for value in values:
        result[value] += 1
    return result


def regular_file(root, *parts):
    path = root
    for part in parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f'Refusing symlink in evidence path: {path}')
    if not path.is_file():
        raise ValueError(f'Missing regular evidence file: {path}')
    return path


def snapshot(root, selection, architecture, program_hash, allowed):
    selected_hash, addresses = inventory(selection, architecture)
    if selected_hash != program_hash:
        raise ValueError('Run selection program identity mismatch')
    if not set(addresses).issubset(allowed):
        raise ValueError('Run selection contains addresses outside the full inventory')
    manifest_path = regular_file(root, 'manifest.json')
    manifest = decode_json(manifest_path.read_text(encoding='utf-8'))
    if (manifest.get('schema_version') != 1 or manifest.get('program_sha256') != program_hash
            or manifest.get('architecture') != architecture
            or manifest.get('address_count') != len(addresses)
            or manifest.get('broad_auto_analysis') is not False
            or not isinstance(manifest.get('exporter_sha256'), str)
            or not re.fullmatch('[0-9a-f]{64}', manifest['exporter_sha256'])):
        raise ValueError('Run manifest identity mismatch')
    if manifest.get('inventory_sha256') != digest(selection):
        raise ValueError('Run inventory hash mismatch')
    database = regular_file(root, 'coverage.sqlite3')
    db = sqlite3.connect(database.as_uri() + '?mode=ro', uri=True)
    try:
        db.execute('BEGIN')
        timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
        if db.execute('PRAGMA quick_check').fetchone()[0] != 'ok':
            raise ValueError('Corrupt coverage database')
        rows = list(db.execute('SELECT ordinal,address,status,attempts,batch,detail FROM functions ORDER BY ordinal'))
        unindexed = [row[0] for row in db.execute('SELECT id FROM batches WHERE state != "ingested" ORDER BY id')]
    finally:
        # File hashing can be lengthy. Release the database snapshot first.
        db.close()
    if [(r[0], r[1]) for r in rows] != list(enumerate(addresses)):
        raise ValueError('Run database inventory differs from its exact selection')
    indexed_hash = hashlib.sha256()
    for row in rows:
        indexed_hash.update((json.dumps(row, separators=(',', ':')) + '\n').encode('utf-8'))
        _, address, status, attempts, batch, detail = row
        if status not in STATUSES or type(attempts) is not int or attempts < 0:
            raise ValueError(f'Invalid coverage status/attempt count at {address}')
        if status == 'unattempted':
            if attempts != 0 or batch is not None or detail is not None:
                raise ValueError(f'Unattempted entry has attempt evidence: {address}')
        elif attempts == 0 or type(batch) is not int or batch < 1 or detail is None:
            raise ValueError(f'Attempted entry has incomplete evidence: {address}')
    return rows, {'path': str(root), 'selection': str(selection), 'snapshot_utc': timestamp,
                  'manifest_sha256': digest(manifest_path), 'identity': manifest,
                  'indexed_rows_sha256': indexed_hash.hexdigest(),
                  'coverage': counts(row[2] for row in rows),
                  'attempt_events': sum(row[3] for row in rows),
                  'unindexed_batches': unindexed}


def audit(inventory_path, architecture, runs):
    inventory_path = Path(inventory_path).resolve()
    program_hash, addresses = inventory(inventory_path, architecture)
    if not re.fullmatch('[0-9a-f]{64}', program_hash):
        raise ValueError('Invalid inventory program hash')
    if not runs:
        raise ValueError('At least one coverage run is required')
    allowed = set(addresses)
    combined = dict.fromkeys(addresses, 'unattempted')
    baseline = None
    seen = set()
    reports = []
    checked = total_bytes = 0
    for folder, selection in runs:
        root = Path(folder).resolve()
        if root in seen:
            raise ValueError('Duplicate run directory')
        seen.add(root)
        rows, report = snapshot(root, Path(selection).resolve(), architecture, program_hash, allowed)
        run_checked = run_bytes = 0
        for _, address, status, _, batch, serialized in rows:
            if status == 'completed':
                detail = decode_json(serialized)
                if detail.get('address') != address or detail.get('status') != status:
                    raise ValueError(f'Completed entry detail mismatch: {address}')
                if detail.get('output_file') != address + '.c':
                    raise ValueError(f'Unexpected output filename: {address}')
                code = regular_file(root, 'batches', f'{batch:08d}', address + '.c')
                size = code.stat().st_size
                if size == 0:
                    raise ValueError(f'Empty completed output: {code}')
                if digest(code) != detail.get('output_sha256'):
                    raise ValueError(f'Completed output hash mismatch: {code}')
                run_checked += 1
                run_bytes += size
                combined[address] = 'completed'
            elif status != 'unattempted' and combined[address] != 'completed':
                combined[address] = status
        if baseline is None:
            baseline = combined.copy()
        checked += run_checked
        total_bytes += run_bytes
        reports.append({**report, 'completed_outputs_checked': run_checked, 'completed_output_bytes': run_bytes})
    recovered = sum(combined[a] == 'completed' and baseline[a] in ('failed', 'timed_out', 'interrupted') for a in addresses)
    return {'schema_version': 1, 'inventory_sha256': digest(inventory_path),
            'architecture': architecture, 'program_sha256': program_hash,
            'unique_addresses': len(addresses), 'coverage': counts(combined.values()),
            'completed_outputs_checked': checked, 'completed_output_bytes': total_bytes,
            'recovered_after_first_run': recovered, 'runs': reports,
            'scope': 'Per-run frozen read-only snapshots; databases closed before file hashing. '
                     'Every completed output checked; overlapping addresses counted once. '
                     'Any verified completion wins, otherwise latest attempted status in supplied order. '
                     'Active unindexed batches excluded. Extraction integrity, not semantic recovery.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--architecture', default='arm64')
    parser.add_argument('--run', type=Path, nargs=2, action='append', metavar=('COVERAGE', 'SELECTION'), required=True)
    parser.add_argument('--report', type=Path, required=True, help='New local JSON report; never overwritten')
    args = parser.parse_args(argv)
    if args.report.exists() or args.report.is_symlink():
        parser.error('Report already exists; choose a new path')
    report = audit(args.inventory, args.architecture, args.run)
    with args.report.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(json.dumps({'report': str(args.report), 'coverage': report['coverage'],
                      'unique_addresses': report['unique_addresses'],
                      'completed_outputs_checked': report['completed_outputs_checked'],
                      'completed_output_bytes': report['completed_output_bytes'],
                      'recovered_after_first_run': report['recovered_after_first_run']}))


if __name__ == '__main__':
    main()
