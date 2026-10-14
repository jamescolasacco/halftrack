#!/usr/bin/env python3
"""Original synthetic fixtures for read-only multi-pass extraction accounting."""
from contextlib import closing
import hashlib
import importlib.util
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
with patch.object(sys, 'path', [str(ROOT / 'scripts'), *sys.path]):
    SPEC = importlib.util.spec_from_file_location('verify_decompile_coverage', ROOT / 'scripts/verify_decompile_coverage.py')
    verify = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(verify)


class VerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addresses = ['0x1000', '0x2000', '0x3000', '0x4000']
        self.inventory = self.write_inventory('inventory.json', self.addresses)

    def tearDown(self):
        self.temp.cleanup()

    def write_inventory(self, name, addresses):
        path = self.root / name
        path.write_text(json.dumps({'schema_version': 1, 'slices': [{
            'architecture': 'arm64', 'sha256': 'a' * 64,
            'function_starts': {'present': True, 'count': len(addresses), 'addresses': addresses}}]}))
        return path

    def run_fixture(self, name, addresses, statuses):
        folder = self.root / name
        folder.mkdir()
        selection = self.write_inventory(name + '-selection.json', addresses)
        manifest = {'schema_version': 1, 'architecture': 'arm64', 'program_sha256': 'a' * 64,
                    'inventory_sha256': hashlib.sha256(selection.read_bytes()).hexdigest(),
                    'address_count': len(addresses), 'exporter_sha256': 'b' * 64,
                    'project_dir': '/original-fixture', 'project_name': name, 'program': 'fixture',
                    'broad_auto_analysis': False}
        (folder / 'manifest.json').write_text(json.dumps(manifest))
        batch = folder / 'batches' / '00000001'
        batch.mkdir(parents=True)
        db = sqlite3.connect(folder / 'coverage.sqlite3')
        db.executescript('CREATE TABLE functions(ordinal INTEGER PRIMARY KEY,address TEXT UNIQUE,status TEXT,attempts INTEGER,batch INTEGER,detail TEXT); CREATE TABLE batches(id INTEGER PRIMARY KEY,state TEXT); INSERT INTO batches VALUES(1,"ingested");')
        for ordinal, (address, status) in enumerate(zip(addresses, statuses)):
            detail = {'address': address, 'status': status}
            if status == 'completed':
                content = ('int original_fixture_' + address + '(void) { return 7; }\n').encode()
                (batch / (address + '.c')).write_bytes(content)
                detail.update(output_file=address + '.c', output_sha256=hashlib.sha256(content).hexdigest())
            attempted = status != 'unattempted'
            db.execute('INSERT INTO functions VALUES(?,?,?,?,?,?)',
                       (ordinal, address, status, int(attempted), 1 if attempted else None, json.dumps(detail) if attempted else None))
        db.commit()
        db.close()
        return folder, selection

    def test_overlap_recovers_once_and_preserves_later_failure_evidence(self):
        base = self.run_fixture('base', self.addresses, ['completed', 'timed_out', 'failed', 'unattempted'])
        retry = self.run_fixture('retry', self.addresses[:3], ['timed_out', 'completed', 'failed'])
        continuation = self.run_fixture('rest', self.addresses[3:], ['completed'])
        before = {p: p.read_bytes() for run, _ in (base, retry, continuation) for p in run.rglob('*') if p.is_file()}
        report = verify.audit(self.inventory, 'arm64', [base, retry, continuation])
        self.assertEqual(report['coverage'], {'completed': 3, 'failed': 1, 'timed_out': 0, 'interrupted': 0, 'unattempted': 0})
        self.assertEqual(report['unique_addresses'], 4)
        self.assertEqual(report['completed_outputs_checked'], 3)
        self.assertEqual(report['recovered_after_first_run'], 1)
        self.assertEqual(report['runs'][1]['coverage']['timed_out'], 1)
        self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_duplicate_success_is_one_address_but_both_files_are_checked(self):
        base = self.run_fixture('base', self.addresses[:1], ['completed'])
        retry = self.run_fixture('retry', self.addresses[:1], ['completed'])
        report = verify.audit(self.inventory, 'arm64', [base, retry])
        self.assertEqual(report['coverage']['completed'], 1)
        self.assertEqual(report['coverage']['unattempted'], 3)
        self.assertEqual(report['completed_outputs_checked'], 2)
        (retry[0] / 'batches/00000001/0x1000.c').write_text('corrupted')
        with self.assertRaisesRegex(ValueError, 'hash'):
            verify.audit(self.inventory, 'arm64', [base, retry])

    def test_wrong_program_or_selection_is_rejected(self):
        run = self.run_fixture('base', self.addresses, ['unattempted'] * 4)
        manifest_path = run[0] / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['program_sha256'] = 'c' * 64
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'identity'):
            verify.audit(self.inventory, 'arm64', [run])
        manifest['program_sha256'] = 'a' * 64
        manifest_path.write_text(json.dumps(manifest))
        run[1].write_text(run[1].read_text() + '\n')
        with self.assertRaisesRegex(ValueError, 'inventory hash'):
            verify.audit(self.inventory, 'arm64', [run])

    def test_outside_inventory_and_duplicate_run_are_rejected(self):
        outside = self.run_fixture('outside', ['0x9000'], ['completed'])
        with self.assertRaisesRegex(ValueError, 'outside'):
            verify.audit(self.inventory, 'arm64', [outside])
        valid = self.run_fixture('valid', self.addresses[:1], ['completed'])
        with self.assertRaisesRegex(ValueError, 'Duplicate run'):
            verify.audit(self.inventory, 'arm64', [valid, valid])

    def test_missing_empty_and_unexpected_output_paths_are_rejected(self):
        run = self.run_fixture('base', self.addresses[:1], ['completed'])
        code = run[0] / 'batches/00000001/0x1000.c'
        code.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing'):
            verify.audit(self.inventory, 'arm64', [run])
        code.write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'Empty'):
            verify.audit(self.inventory, 'arm64', [run])
        with closing(sqlite3.connect(run[0] / 'coverage.sqlite3')) as db, db:
            detail = json.loads(db.execute('SELECT detail FROM functions').fetchone()[0])
            detail['output_file'] = '../../external.c'
            db.execute('UPDATE functions SET detail=?', (json.dumps(detail),))
        with self.assertRaisesRegex(ValueError, 'filename'):
            verify.audit(self.inventory, 'arm64', [run])

    def test_output_symlink_is_rejected(self):
        run = self.run_fixture('base', self.addresses[:1], ['completed'])
        code = run[0] / 'batches/00000001/0x1000.c'
        target = self.root / 'same-bytes.c'
        target.write_bytes(code.read_bytes())
        code.unlink()
        try:
            code.symlink_to(target)
        except OSError:
            self.skipTest('Creating symbolic links is not permitted on this platform')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            verify.audit(self.inventory, 'arm64', [run])

    def test_active_journal_is_excluded_and_interrupted_remains_distinct(self):
        run = self.run_fixture('base', self.addresses, ['completed', 'failed', 'interrupted', 'unattempted'])
        with closing(sqlite3.connect(run[0] / 'coverage.sqlite3')) as db, db:
            db.execute('INSERT INTO batches VALUES(2,"running")')
        pending = run[0] / 'batches/00000002'
        pending.mkdir()
        (pending / 'events.jsonl').write_text('{"event":"result","status":"completed","address":"0x4000"}\n')
        report = verify.audit(self.inventory, 'arm64', [run])
        self.assertEqual(report['coverage'], {'completed': 1, 'failed': 1, 'interrupted': 1, 'unattempted': 1, 'timed_out': 0})
        self.assertEqual(report['runs'][0]['unindexed_batches'], [2])

    def test_snapshot_releases_reader_before_hashing_and_keeps_frozen_statuses(self):
        run = self.run_fixture('base', self.addresses, ['completed'] + ['unattempted'] * 3)
        original_digest = verify.digest
        changed = False

        def digest_with_concurrent_commit(path):
            nonlocal changed
            if Path(path).suffix == '.c' and not changed:
                with closing(sqlite3.connect(run[0] / 'coverage.sqlite3', timeout=0.1)) as db, db:
                    db.execute('UPDATE functions SET status="failed",attempts=1,batch=1,detail=? WHERE ordinal=1',
                               (json.dumps({'status': 'failed', 'address': '0x2000'}),))
                changed = True
            return original_digest(path)

        with patch.object(verify, 'digest', digest_with_concurrent_commit):
            report = verify.audit(self.inventory, 'arm64', [run])
        self.assertTrue(changed)
        self.assertEqual(report['coverage']['unattempted'], 3)
        self.assertEqual(report['coverage']['failed'], 0)
        later = verify.audit(self.inventory, 'arm64', [run])
        self.assertEqual(later['coverage']['failed'], 1)
        self.assertNotEqual(report['runs'][0]['indexed_rows_sha256'], later['runs'][0]['indexed_rows_sha256'])

    def test_command_writes_new_report_and_refuses_overwrite(self):
        run = self.run_fixture('base', self.addresses[:1], ['completed'])
        report_path = self.root / 'verified.json'
        command = [sys.executable, str(ROOT / 'scripts/verify_decompile_coverage.py'),
                   '--inventory', str(self.inventory), '--run', str(run[0]), str(run[1]),
                   '--report', str(report_path)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        original = report_path.read_bytes()
        report = json.loads(original)
        self.assertEqual(report['coverage']['completed'], 1)
        second = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn('already exists', second.stderr)
        self.assertEqual(report_path.read_bytes(), original)

    def test_failed_verification_does_not_write_success_report(self):
        run = self.run_fixture('base', self.addresses[:1], ['completed'])
        (run[0] / 'batches/00000001/0x1000.c').write_text('changed')
        report = self.root / 'invalid.json'
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/verify_decompile_coverage.py'),
                                 '--inventory', str(self.inventory), '--run', str(run[0]), str(run[1]),
                                 '--report', str(report)], capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('hash mismatch', result.stderr)
        self.assertFalse(report.exists())

    def test_corrupt_database_inventory_is_rejected(self):
        run = self.run_fixture('base', self.addresses, ['unattempted'] * 4)
        with closing(sqlite3.connect(run[0] / 'coverage.sqlite3')) as db, db:
            db.execute('UPDATE functions SET address="0x9999" WHERE ordinal=0')
        with self.assertRaisesRegex(ValueError, 'database inventory'):
            verify.audit(self.inventory, 'arm64', [run])


if __name__ == '__main__':
    unittest.main(verbosity=2)
