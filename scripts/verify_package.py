#!/usr/bin/env python3
"""Install a packaged preview into a fresh folder, check what it is, then remove it.

The package must hold the application, its licenses, SOURCE.json and the corresponding source.
The packaged source's CMake version, the bundle's Info.plist (on macOS) and the installed
application's --version line must all name the same version, and --version must name the
commit SOURCE.json records, unmodified. When the repository holds that commit, the packaged
Halftrack source must equal it file for file. Removing the install folder uninstalls the
application; settings and the User Library live outside it (see docs/research/packaging-contract.md).
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

REQUIRED = ['LICENSE', 'LICENSE-JUCE.md', 'LICENSE-Tracktion.md', 'SOURCE.json', 'START HERE.txt', 'dependencies.md',
            'source/halftrack.tar', 'source/juce.tar.gz', 'source/tracktion.tar.gz']
SYSTEMS = {'Darwin': 'macOS', 'Windows': 'Windows', 'Linux': 'Linux'}
SUMMARY = re.compile(r'^Halftrack (?P<version>\S+) \((?P<commit>[^,]+), (?P<system>macOS|Windows|Linux) (?P<architecture>\S+)\)$')


def project_version(cmake_text):
    """The VERSION in CMakeLists.txt's project() call."""
    match = re.search(r'project\s*\(\s*Halftrack\b[^)]*?\bVERSION\s+([0-9][0-9A-Za-z.\-]*)', cmake_text)
    return match.group(1) if match else None


def application(root, system):
    """The installed executable and, on macOS, the bundle's Info.plist."""
    if system == 'Darwin':
        bundle = root / 'Halftrack.app'
        return bundle / 'Contents' / 'MacOS' / 'Halftrack', bundle / 'Contents' / 'Info.plist'
    return root / ('Halftrack.exe' if system == 'Windows' else 'Halftrack'), None


def check_install(root, system, run):
    """Problems with an installed package at root; run(executable) returns its --version output."""
    problems, facts = [], {}
    for relative in REQUIRED:
        if not (root / relative).is_file():
            problems.append(f'missing {relative}')
    executable, plist = application(root, system)
    if not executable.is_file():
        problems.append(f'missing application {executable.relative_to(root)}')
    try:
        source = json.loads((root / 'SOURCE.json').read_text())
        commit = source['halftrack'].rstrip('/').rsplit('/', 1)[-1]
        facts['commit'] = commit
    except (OSError, ValueError, KeyError):
        return problems + ['SOURCE.json does not name the Halftrack commit'], facts
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        problems.append(f'SOURCE.json names "{commit}", not a full commit')
    try:
        with tarfile.open(root / 'source' / 'halftrack.tar') as archive:
            member = archive.extractfile('CMakeLists.txt')
            version = project_version(member.read().decode()) if member else None
    except (OSError, tarfile.TarError, KeyError):
        version = None
    facts['version'] = version
    if not version:
        return problems + ['the packaged source names no project version'], facts
    if plist is not None:
        try:
            info = plistlib.loads(plist.read_bytes())
            facts['bundle'] = {key: info.get(key) for key in ('CFBundleIdentifier', 'CFBundleShortVersionString', 'CFBundleVersion')}
            if info.get('CFBundleShortVersionString') != version or info.get('CFBundleVersion') != version:
                problems.append(f'Info.plist says {info.get("CFBundleShortVersionString")} ({info.get("CFBundleVersion")}), the source says {version}')
            if info.get('CFBundleIdentifier') != 'org.halftrack.studio':
                problems.append(f'Info.plist identifies the bundle as {info.get("CFBundleIdentifier")}')
        except (OSError, plistlib.InvalidFileException):
            problems.append('Info.plist cannot be read')
    if executable.is_file():
        line = run(executable).strip()
        facts['reported'] = line
        match = SUMMARY.match(line)
        if not match:
            problems.append(f'--version printed "{line}"')
        else:
            if match['version'] != version:
                problems.append(f'--version names {match["version"]}, the source says {version}')
            if len(match['commit']) < 7 or not commit.startswith(match['commit']):
                problems.append(f'--version names commit {match["commit"]}, SOURCE.json names {commit[:7]}')
            if match['system'] != SYSTEMS.get(system):
                problems.append(f'--version names {match["system"]}, this is {SYSTEMS.get(system)}')
    return problems, facts


def compare_source(root, repo, commit):
    """Problems where the packaged Halftrack source differs from the repository's commit."""
    try:
        expected = subprocess.run(['git', 'archive', '--format=tar', commit], cwd=repo, capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    def contents(data):
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            return {m.name: hashlib.sha256(archive.extractfile(m).read()).hexdigest() for m in archive.getmembers() if m.isfile()}
    want, have = contents(expected), contents((root / 'source' / 'halftrack.tar').read_bytes())
    problems = [f'source lacks {name}' for name in sorted(want.keys() - have.keys())]
    problems += [f'source has unexpected {name}' for name in sorted(have.keys() - want.keys())]
    problems += [f'source {name} differs from the commit' for name in sorted(want.keys() & have.keys()) if want[name] != have[name]]
    return problems, len(want)


def extract(archive, destination):
    """Unpacks the archive and returns the package folder inside it."""
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as z:
            z.extractall(destination)
    else:
        with tarfile.open(archive) as t:
            t.extractall(destination, filter='tar') if hasattr(tarfile, 'data_filter') else t.extractall(destination)
    folders = [p for p in destination.iterdir() if p.is_dir()]
    if len(folders) != 1:
        raise SystemExit(f'{archive.name} should hold one package folder, not {len(folders)}')
    return folders[0]


def run_version(executable):
    result = subprocess.run([str(executable), '--version'], capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        return f'(exit {result.returncode}) {result.stdout} {result.stderr}'
    return next((line for line in result.stdout.splitlines() if line.startswith('Halftrack ')), result.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('archive', type=Path, nargs='?', help='the package (default: the newest in dist/)')
    parser.add_argument('--report', type=Path, help='write the findings as JSON here')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    archive = args.archive
    if archive is None:
        candidates = sorted([*(repo / 'dist').glob('Halftrack-*.tar.gz'), *(repo / 'dist').glob('Halftrack-*.zip')], key=lambda p: p.stat().st_mtime)
        if not candidates:
            raise SystemExit('No package in dist/: run scripts/package_preview.py first.')
        archive = candidates[-1]
    report = {'archive': archive.name, 'bytes': archive.stat().st_size, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='halftrack-install-') as scratch:
        install = extract(archive, Path(scratch))
        problems, facts = check_install(install, platform.system(), run_version)
        report.update(facts)
        if 'commit' in facts:
            compared = compare_source(install, repo, facts['commit'])
            if compared is None:
                report['source'] = 'not compared: the repository lacks the commit'
            else:
                problems += compared[0]
                report['source'] = f'{compared[1]} files compared with the commit'
        # Uninstall: the application is the install folder.
        shutil.rmtree(install)
        if install.exists():
            problems.append('the install folder could not be removed')
    report['problems'] = problems
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    for key in ('archive', 'version', 'commit', 'reported', 'source'):
        if key in report:
            print(f'{key}: {report[key]}')
    for problem in problems:
        print(f'FAIL {problem}')
    if problems:
        sys.exit(1)
    print('Package verified: installed, identified and removed.')


if __name__ == '__main__':
    main()
