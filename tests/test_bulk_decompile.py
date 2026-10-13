#!/usr/bin/env python3
"""Original synthetic evidence tests; optional real Ghidra fixture via GHIDRA_HOME."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('bulk_decompile', ROOT/'scripts/bulk_decompile.py')
bulk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bulk)


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.identity = {'schema_version': 1, 'program_sha256': 'a'*64}
        self.entries = ['0x1000', '0x2000', '0x3000']
        self.coverage = bulk.Coverage(self.root/'coverage', self.identity, self.entries)

    def tearDown(self):
        self.coverage.close()
        self.temp.cleanup()

    def batch(self):
        number, folder = self.coverage.next_batch(3, 1, 1)
        request = bulk.read_json(folder/'request.json')
        bulk.write_new(folder/'metadata.json', {'program_sha256': 'a'*64,
                       'request_sha256': bulk.digest(folder/'request.json'),
                       'requested_count': 3, 'broad_auto_analysis': False})
        return number, folder

    def journal(self, folder, events, trailing=b''):
        (folder/'events.jsonl').write_bytes(b''.join(json.dumps(e).encode()+b'\n' for e in events)+trailing)

    def complete(self, folder, address):
        text = b'int original_fixture(int x) { return x * 3 + 7; }\n'
        (folder/(address+'.c')).write_bytes(text)
        return {'event':'result', 'address':address, 'status':'completed',
                'output_file':address+'.c', 'output_sha256':hashlib.sha256(text).hexdigest()}

    def test_interrupt_resume_preserves_outputs_and_counts(self):
        number, folder = self.batch()
        first = self.complete(folder, self.entries[0])
        self.journal(folder, [{'event':'started','address':self.entries[0]}, first,
                            {'event':'started','address':self.entries[1]}], b'{"event":')
        before = {p.name:p.read_bytes() for p in folder.iterdir()}
        self.coverage.recover()
        counts = self.coverage.summary()
        self.assertEqual((counts['completed'],counts['interrupted'],counts['unattempted']), (1,1,1))
        self.coverage.recover()  # ingestion is idempotent
        self.assertEqual(self.coverage.summary()['attempt_events'], 2)
        _, next_folder = self.coverage.next_batch(3,1,1)
        self.assertEqual(bulk.read_json(next_folder/'request.json')['addresses'], self.entries[1:])
        self.assertEqual(before, {p.name:p.read_bytes() for p in folder.iterdir()})

    def test_explicit_failure_timeout_and_unattempted_are_distinct(self):
        number, folder = self.batch()
        self.journal(folder, [
            {'event':'started','address':self.entries[0]},
            {'event':'result','address':self.entries[0],'status':'failed','error':'outside executable memory'},
            {'event':'started','address':self.entries[1]},
            {'event':'result','address':self.entries[1],'status':'timed_out','timed_out':True},
            {'event':'finished','result_count':2}])
        self.coverage.ingest(number)
        counts=self.coverage.summary()
        self.assertEqual((counts['failed'],counts['timed_out'],counts['unattempted']), (1,1,1))

    def test_duplicate_results_and_corrupt_output_rejected(self):
        number, folder = self.batch()
        result=self.complete(folder,self.entries[0])
        start={'event':'started','address':self.entries[0]}
        self.journal(folder,[start,result,result])
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            self.coverage.ingest(number)
        self.journal(folder,[start,result])
        (folder/result['output_file']).write_text('changed')
        with self.assertRaisesRegex(ValueError,'corrupt pseudocode'):
            self.coverage.ingest(number)
        self.assertEqual(self.coverage.summary()['unattempted'],3)

    def test_complete_corrupt_journal_is_not_treated_as_interruption(self):
        number,folder=self.batch()
        self.journal(folder,[],b'{bad json}\n')
        with self.assertRaisesRegex(ValueError,'Corrupt complete'):
            self.coverage.ingest(number)

    def test_duplicate_journal_keys_rejected(self):
        number, folder = self.batch()
        self.journal(folder, [], b'{"event":"started","event":"result","address":"0x1000"}\n')
        with self.assertRaisesRegex(ValueError, 'Corrupt complete'):
            self.coverage.ingest(number)

    def test_corrupt_request_and_manifest_rejected(self):
        number,folder=self.batch()
        (folder/'request.json').write_text('{}')
        with self.assertRaisesRegex(ValueError,'Corrupt batch request'):
            self.coverage.ingest(number)
        (self.root/'coverage'/'manifest.json').write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(ValueError,'Duplicate JSON key'):
            bulk.Coverage(self.root/'coverage',self.identity,self.entries)

    def test_duplicate_inventory_addresses_rejected(self):
        path=self.root/'inventory.json'
        bulk.write_new(path,{'schema_version':1,'slices':[{'architecture':'arm64','sha256':'a'*64,
                       'function_starts':{'present':True,'count':2,'addresses':['0x1000','1000']}}]})
        with self.assertRaisesRegex(ValueError,'Duplicate inventory'):
            bulk.inventory(path,'arm64')


@unittest.skipUnless(os.environ.get('GHIDRA_HOME') and sys.platform=='darwin',
                     'GHIDRA_HOME unset or non-macOS: original Ghidra integration fixture explicitly skipped')
class GhidraBulkTests(unittest.TestCase):
    def test_generated_fixture_bulk_and_restart(self):
        xcrun=shutil.which('xcrun')
        if not xcrun:
            self.skipTest('macOS toolchain unavailable')
        with tempfile.TemporaryDirectory(prefix='halftrack-bulk-fixture-') as temporary:
            root=Path(temporary)
            source=root/'fixture.c'
            # Enough distinct entries that a coordinator interrupt can be observed.
            source.write_text('\n'.join(f'__attribute__((noinline)) int fixture_{i}(int x) {{ return x * 3 + {i+7}; }}' for i in range(96))+'\nint main(int n,char **v) { return fixture_0(n); }\n')
            assembly=root/'tails.s'
            assembly.write_text('.text\n.globl _fixture_tail_a\n.globl _fixture_tail_b\n.p2align 2\n_fixture_tail_a:\ncmp w0, #0\nb.gt _fixture_tail_b\nadd w0, w0, #5\nret\n_fixture_tail_b:\nadd w0, w0, #11\nret\n')
            binary=root/'fixture' 
            subprocess.run([xcrun,'clang','-arch','arm64','-O1','-fno-inline','-o',str(binary),str(source),str(assembly)],check=True,timeout=30)
            nm=subprocess.check_output([xcrun,'nm','-n',str(binary)],text=True,timeout=10)
            addresses=[f'0x{int(line.split()[0],16):x}' for line in nm.splitlines() if len(line.split())==3 and line.split()[2].startswith('_fixture_')]
            addresses=sorted(addresses,key=lambda a:int(a,16))
            addresses.append('0x1deadbeef')
            inv=root/'inventory.json'
            bulk.write_new(inv,{'schema_version':1,'slices':[{'architecture':'arm64','sha256':bulk.digest(binary),
                           'function_starts':{'present':True,'count':len(addresses),'addresses':addresses}}]})
            projects=root/'projects'
            projects.mkdir()
            headless=Path(os.environ['GHIDRA_HOME'])/'support/analyzeHeadless'
            environment=dict(os.environ,GHIDRA_HEADLESS_MAXMEM='4G')
            with (root/'import.log').open('w') as log:
                subprocess.run([str(headless),str(projects),'Fixture','-import',str(binary),'-noanalysis','-max-cpu','2'],env=environment,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
            output=root/'coverage'
            command=[sys.executable,str(ROOT/'scripts/bulk_decompile.py'),'--inventory',str(inv),
                     '--binary',str(binary),'--project-dir',str(projects),'--project-name','Fixture',
                     '--program','fixture','--output',str(output),'--batch-size','32','--max-batches','1',
                     '--process-timeout','90']
            first=subprocess.run(command,env=environment,capture_output=True,text=True,timeout=100)
            log = output/'batches/00000001/headless.log'
            self.assertEqual(first.returncode,0,first.stdout+first.stderr+(log.read_text()[-6000:] if log.exists() else 'No batch log'))
            before={p.relative_to(output):p.read_bytes() for p in (output/'batches/00000001').iterdir()}
            command[command.index('--max-batches')+1]='4'
            # Interrupt a real Ghidra batch after a durable started event, then resume.
            interrupted=subprocess.Popen(command,env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            deadline=time.monotonic()+30
            observed=False
            try:
                while time.monotonic()<deadline and interrupted.poll() is None:
                    journal=output/'batches/00000002/events.jsonl'
                    if journal.exists() and b'"event":"started"' in journal.read_bytes():
                        observed=True
                        interrupted.send_signal(signal.SIGINT)
                        break
                    time.sleep(0.01)
                if not observed and interrupted.poll() is None:
                    interrupted.send_signal(signal.SIGINT)
                stdout,stderr=interrupted.communicate(timeout=25)
            finally:
                if interrupted.poll() is None:
                    interrupted.kill()
                    interrupted.wait(timeout=10)
            self.assertTrue(observed,'Did not reach durable event before interruption: '+stdout+stderr)
            self.assertNotEqual(interrupted.returncode,0)
            second=subprocess.run(command,env=environment,capture_output=True,text=True,timeout=120)
            self.assertEqual(second.returncode,0,second.stdout+second.stderr)
            self.assertEqual(before,{p.relative_to(output):p.read_bytes() for p in (output/'batches/00000001').iterdir()})
            c=bulk.Coverage(output,bulk.read_json(output/'manifest.json'),addresses)
            counts=c.summary()
            self.assertEqual((counts['completed'],counts['failed'],counts['unattempted']), (98,1,0))
            detail=json.loads(c.db.execute("SELECT detail FROM functions WHERE address='0x1deadbeef'").fetchone()[0])
            self.assertIn('initialized executable memory',detail['error'])
            rows=[json.loads(row[0]) for row in c.db.execute("SELECT detail FROM functions WHERE status='completed'")]
            self.assertTrue(any(row.get('entry_already_decoded') for row in rows))
            self.assertTrue(any(row.get('prior_containing_model_revised') for row in rows))
            c.close()
            code=(output/'batches/00000001'/f'{addresses[0]}.c').read_text()
            self.assertRegex(code,r'\*\s*3\s*\+\s*7')
            self.assertIn('modeled_body_warning',bulk.read_json(output/'batches/00000001/metadata.json'))
            final=subprocess.run(command+['--status'],env=environment,capture_output=True,text=True,timeout=10)
            self.assertEqual(final.returncode,0,final.stderr)
            self.assertIn('"completed": 98',final.stdout)


if __name__=='__main__':
    unittest.main(verbosity=2)
