#!/usr/bin/env python3
"""Tie each partially implemented capability to the evidence that checks it, and to a run's results.

The capability ledger (docs/feature-parity.md) lists the rows; docs/evidence-map.md names, for each
row with PARTIAL status, the automated suites that exercise it (CTest names without "-acceptance",
"session" for the Session suite, "python" for the tooling tests) or "manual" with what was checked
by hand. The suites come from CMakeLists.txt, and each one's source files from src/Main.cpp's
self-test dispatch.

  evidence_report.py --check                 the map covers every PARTIAL row and names real suites
  evidence_report.py --junit ctest.xml --out evidence.json [--markdown evidence.md]
                                             one run's evidence: commit, environment, command and,
                                             per row, each suite's expected and actual result
"""
import argparse
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT / 'docs' / 'evidence-map.md'


def ledger_rows(text):
    """Active rows with PARTIAL status, as {id: capability name}."""
    rows = {}
    for row_id, name, rest in re.findall(r'^\| ([A-Z]+-\d+) \| ([^|]+) \|(.*)$', text, re.M):
        if not row_id.startswith('PRE') and 'PARTIAL' in rest:
            rows[row_id] = name.strip()
    return rows


def suites(cmake_text):
    """CTest suites by short name: {name: self-test flag}."""
    found = {}
    for name, flag in re.findall(r'add_test\(NAME ([a-z0-9-]+?)-acceptance COMMAND \$<TARGET_FILE:Halftrack> --self-test(-[a-z0-9-]+)?\b', cmake_text):
        found[name] = flag.lstrip('-') if flag else ''
    return found


def suite_sources(main_text, source_texts):
    """Each self-test flag's source files: {flag: [file names]}; '' is the Session suite."""
    defined = {}
    for name, text in source_texts.items():
        # Definitions only: a declaration ends with a semicolon.
        for function in re.findall(r'^int (?:MainComponent::)?(run[A-Za-z]+)\s*\([^;]*?\)\s*(?:\{|$)', text, re.M):
            defined[function] = name
    sources = {}
    for flag, body in re.findall(r'args\.contains \("--self-test-?([a-z0-9-]*)"\)\)\s*\{(.*?)\n        \}', main_text, re.S):
        functions = re.findall(r'\b(run[A-Za-z]+)\s*\(', body)
        sources[flag] = sorted({defined[f] for f in functions if f in defined})
    return sources


def evidence_map(text):
    """{row id: (suite names, manual note)} from the map's table."""
    entries = {}
    for row_id, named, manual in re.findall(r'^\| ([A-Z]+-\d+) \| ([^|]*) \|([^|\n]*)\|?$', text, re.M):
        entries[row_id] = ([s.strip() for s in named.split(',') if s.strip()], manual.strip())
    return entries


def check(ledger_text, map_text, cmake_text):
    """Problems with the map: rows it lacks or has extra, suites that do not exist, rows with no evidence."""
    rows, entries, known = ledger_rows(ledger_text), evidence_map(map_text), set(suites(cmake_text)) | {'session', 'python'}
    problems = [f'{r} is PARTIAL in the ledger but not in the evidence map' for r in sorted(rows.keys() - entries.keys())]
    problems += [f'{r} is in the evidence map but not PARTIAL in the ledger' for r in sorted(entries.keys() - rows.keys())]
    for row_id, (named, manual) in sorted(entries.items()):
        problems += [f'{row_id} names {s}, which is not a suite' for s in named if s not in known]
        if not named and not manual:
            problems.append(f'{row_id} names no suite and no manual evidence')
    return problems


def junit_results(xml_text):
    """{ctest name: (status, seconds, checks passed)} from CTest's JUnit output; a suite prints a
    PASS line for each of its checks, which CTest keeps as the test's output."""
    results = {}
    for case in ElementTree.fromstring(xml_text).iter('testcase'):
        status = case.get('status') or ('fail' if case.find('failure') is not None else 'skipped' if case.find('skipped') is not None else 'run')
        status = {'run': 'passed', 'fail': 'failed', 'notrun': 'not run', 'disabled': 'not run'}.get(status, status)
        output = case.findtext('system-out') or ''
        checks = sum(1 for line in output.splitlines() if line.startswith('PASS '))
        results[case.get('name')] = (status, float(case.get('time') or 0), checks)
    return results


def environment():
    def git(*args):
        try:
            return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return 'unknown'
    return {
        'commit': git('rev-parse', 'HEAD'),
        'modified': git('status', '--porcelain', '--untracked-files=no') not in ('', 'unknown'),
        'system': platform.system(), 'release': platform.release(), 'machine': platform.machine(),
        'python': platform.python_version(),
    }


def report(ledger_text, map_text, cmake_text, results, command):
    rows, entries = ledger_rows(ledger_text), evidence_map(map_text)
    known = suites(cmake_text)
    out = {'environment': environment(), 'command': command, 'rows': []}
    for row_id, name in rows.items():
        named, manual = entries.get(row_id, ([], ''))
        evidence = []
        for suite in named:
            test = 'python-tooling' if suite == 'python' else f'{suite}-acceptance'
            status, seconds, checks = results.get(test, ('not run', 0.0, 0))
            flag = '' if suite == 'session' else known.get(suite, suite)
            evidence.append({'suite': suite, 'ctest': test, 'command': 'python -m unittest discover -s tests -p test_*.py' if suite == 'python'
                             else f'Halftrack --self-test{"-" + flag if flag else ""} <artifacts>', 'expected': 'passed', 'actual': status, 'seconds': seconds, 'checks': checks})
        out['rows'].append({'row': row_id, 'capability': name, 'evidence': evidence, 'manual': manual,
                            'verified': bool(evidence) and all(e['actual'] == 'passed' for e in evidence)})
    return out


def markdown(data):
    env = data['environment']
    lines = [f"# Evidence for {env['commit'][:10]}{' (modified)' if env['modified'] else ''}", '',
             f"{env['system']} {env['release']} {env['machine']}. Command: `{data['command']}`.", '',
             '| Row | Capability | Suites (actual) | Manual |', '| --- | --- | --- | --- |']
    for row in data['rows']:
        suites_text = ', '.join(f"{e['suite']} ({e['actual']}" + (f", {e['checks']} checks" if e['checks'] else '') + ')' for e in row['evidence']) or '—'
        lines.append(f"| {row['row']} | {row['capability']} | {suites_text} | {row['manual'] or ''} |")
    verified = sum(1 for r in data['rows'] if r['verified'])
    lines += ['', f"{verified} of {len(data['rows'])} rows have every named suite passing in this run."]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--junit', type=Path)
    parser.add_argument('--python-result', choices=['passed', 'failed'], help="the tooling tests' result in this run")
    parser.add_argument('--command', default='ctest --test-dir build -C Release --output-junit test-artifacts/ctest.xml')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--markdown', type=Path)
    args = parser.parse_args()
    ledger = (ROOT / 'docs' / 'feature-parity.md').read_text(encoding='utf-8')
    cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    mapping = MAP.read_text(encoding='utf-8')
    if args.check:
        problems = check(ledger, mapping, cmake)
        for problem in problems:
            print(f'FAIL {problem}')
        if problems:
            sys.exit(1)
        print(f'Evidence map covers {len(ledger_rows(ledger))} rows.')
        return
    if not args.junit or not args.out:
        parser.error('give --check, or --junit and --out')
    results = junit_results(args.junit.read_text(encoding='utf-8'))
    if args.python_result:
        results['python-tooling'] = (args.python_result, 0.0, 0)
    data = report(ledger, mapping, cmake, results, args.command)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    if args.markdown:
        args.markdown.write_text(markdown(data), encoding='utf-8')
    failed = [r['row'] for r in data['rows'] if r['evidence'] and not r['verified']]
    print(f"{sum(r['verified'] for r in data['rows'])} of {len(data['rows'])} rows verified; {len(failed)} with a failing or missing suite: {' '.join(failed)}")


if __name__ == '__main__':
    main()
