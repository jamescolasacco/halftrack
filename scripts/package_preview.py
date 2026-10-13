#!/usr/bin/env python3
"""Package a locally built preview with its license and exact source references."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, default=Path('build'))
parser.add_argument('--output-dir', type=Path, default=Path('dist'))
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
if subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--'], cwd=repo).returncode != 0:
    raise SystemExit('Commit tracked source changes before packaging the preview.')
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
variant = f'{platform.system().lower()}-{platform.machine().lower()}'
name = f'Halftrack-preview-{variant}-{commit[:7]}'
package = args.output_dir / name
package.mkdir(parents=True, exist_ok=True)
products = args.build_dir / 'Halftrack_artefacts' / 'Release'
app = products / ('Halftrack.app' if platform.system() == 'Darwin' else ('Halftrack.exe' if platform.system() == 'Windows' else 'Halftrack'))
if not app.exists():
    raise SystemExit(f'Build the application first: {app}')
if app.is_dir():
    shutil.copytree(app, package / app.name, dirs_exist_ok=True, symlinks=True)
else:
    shutil.copy2(app, package / app.name)
for relative in ['LICENSE', 'docs/dependencies.md', 'docs/preview-verification.md', 'assets/halftrack-mark.svg', 'docs/manual-test-phase1.md', 'docs/realtime-safety.md']:
    shutil.copy2(repo / relative, package / Path(relative).name)
(package / 'SOURCE.json').write_text(json.dumps({
    'halftrack': f'https://github.com/jamescolasacco/halftrack/tree/{commit}',
    'source_archive': f'https://github.com/jamescolasacco/halftrack/archive/{commit}.tar.gz',
    'juce': 'https://github.com/juce-framework/JUCE/tree/37c894f83d379179b2070d437ccd0f1cd9af9576',
    'tracktion_engine': 'https://github.com/Tracktion/tracktion_engine/tree/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f',
}, indent=2) + '\n')
# Include complete corresponding source, including licenses embedded in headers.
# CMake can rebuild offline using FETCHCONTENT_SOURCE_DIR_JUCE/TRACKTION overrides.
source_bundle = package / 'source'
source_bundle.mkdir(exist_ok=True)
subprocess.run(['git', 'archive', '--format=tar', '--output=' + str((source_bundle / 'halftrack.tar').resolve()), 'HEAD'], cwd=repo, check=True)
# Include dependency notices and source from downloads or pinned local overrides.
for label, candidates in {
    'JUCE': [args.build_dir / '_deps/juce-src', repo / '.deps/tracktion_engine/modules/juce'],
    'Tracktion': [args.build_dir / '_deps/tracktion-src', repo / '.deps/tracktion_engine'],
}.items():
    source = next((path for path in candidates if (path / 'LICENSE.md').is_file()), None)
    if source is None:
        raise SystemExit(f'Missing {label} source license; cannot package preview.')
    shutil.copy2(source / 'LICENSE.md', package / f'LICENSE-{label}.md')
    def source_filter(entry):
        parts = Path(entry.name).parts
        if '.git' in parts or (label == 'Tracktion' and len(parts) >= 3 and parts[1:3] == ('modules', 'juce')):
            return None
        return entry
    with tarfile.open(source_bundle / f'{label.lower()}.tar.gz', 'w:gz') as archive:
        archive.add(source, arcname=label.lower(), filter=source_filter)
(package / 'START HERE.txt').write_text(
    'Halftrack native preview\n\n'
    'Open Halftrack.app on macOS, Halftrack.exe on Windows, or Halftrack on Linux.\n'
    'This is the basic audio and MIDI preview. Full DAW features remain in development.\n'
    'New MIDI Track creates an instrument track and clip. Piano Roll draws notes.\n'
    'Audio Setup selects hardware. Import Audio adds files. File contains Save and Export.\n'
    'Save projects as .halftrack; keep the sibling Assets folder with each project.\n'
    'The Mac build is an unsigned development preview. Windows driver/UI checks remain separate from CI.\n'
    'A short manual checklist, real-time limitations, licenses, and corresponding source are included.\n'
    'Extract source/halftrack.tar and the dependency archives to inspect all source notices.\n'
    'Build offline with -DFETCHCONTENT_SOURCE_DIR_JUCE=<juce> and -DFETCHCONTENT_SOURCE_DIR_TRACKTION=<tracktion>.\n')
if platform.system() != 'Windows':
    archive = args.output_dir / f'{name}.tar.gz'
    with tarfile.open(archive, 'w:gz') as output:
        output.add(package, arcname=name)
else:
    archive = args.output_dir / f'{name}.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for item in package.rglob('*'):
            if item.is_file():
                output.write(item, Path(name) / item.relative_to(package))
print(archive.resolve())
