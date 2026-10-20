#!/usr/bin/env python3
"""Conservative source guard for owned callback bodies, not realtime certification.

This small scanner follows calls to functions defined in the audited files. It
intentionally does not parse dependency code, templates, macros, virtual dispatch,
or complete C++. Runtime allocation, contention and deadline tests remain needed.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
import unittest


# Explicit roots keep disappearance/renaming from silently weakening the guard.
ROOTS = {
    "src/Session.cpp": ("applyToBuffer",),  # Original TrackMixer normalization wrapper.
    "src/SubtractiveSynth.cpp": ("SubtractiveSynthDSP::render", "SubtractiveSynthPlugin::applyToBuffer"),
    "src/NativeEffects.cpp": ("NativeEffectDSP::processSamples", "NativeEffectPlugin::applyToBuffer", "NativeEffectPlugin::processMidi", "NativeEffectPlugin::renderInstrument"),
    "src/Instruments.cpp": (
        "InstrumentDSP::render",
        "InstrumentDSP::noteOn",
        "InstrumentDSP::noteOff",
        "InstrumentDSP::controlChange",
        "InstrumentDSP::pitchWheel",
        "InstrumentDSP::configure",
        "InstrumentDSP::reset",
    ),
    "src/MidiEffects.cpp": (
        "Processor::process",
        "Processor::allNotesOff",
        "Processor::configure",
        "Processor::reset",
    ),
    "src/EffectProcessors.cpp": (
        "Saturator::tick",
        "Saturator::configure",
        "Saturator::reset",
        "Overdrive::tick",
        "Overdrive::configure",
        "Overdrive::reset",
        "AutoFilter::tick",
        "AutoFilter::configure",
        "AutoFilter::reset",
        "AutoFilter::syncTo",
        "AutoPan::tick",
        "AutoPan::configure",
        "AutoPan::reset",
        "AutoPan::syncTo",
        "ModulatedDelay::tick",
        "ModulatedDelay::configure",
        "ModulatedDelay::reset",
        "ModulatedDelay::syncTo",
        "Phaser::tick",
        "Phaser::configure",
        "Phaser::reset",
        "Phaser::syncTo",
        "ThreeBandEq::tick",
        "ThreeBandEq::configure",
        "ThreeBandEq::reset",
        "GlueCompressor::tick",
        "GlueCompressor::configure",
        "GlueCompressor::reset",
        "FrequencyShifter::tick",
        "FrequencyShifter::configure",
        "FrequencyShifter::reset",
        "FrequencyShifter::syncTo",
    ),

    "src/UtilityProcessor.cpp": ("UtilityDSP::processSamples", "UtilityPlugin::applyToBuffer"),
    "tests/TestGainPlugin.cpp": ("processBlock",),
}

# Leave newlines and character offsets intact for reliable source diagnostics.
LITERALS = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
HEADER = re.compile(
    r'\b(?P<name>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*'
    r'\([^;{}]*?\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{'
)
CALL = re.compile(r'\b([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\(')
NON_FUNCTIONS = {"if", "for", "while", "switch", "catch", "sizeof", "alignof", "static_assert"}
RULES = {
    "heap lifetime": r'\b(?:new|delete)\b|\b(?:malloc|calloc|realloc|free)\s*\(|\bmake_(?:unique|shared)\s*(?:<[^;{}]*>)?\s*\(',
    "storage growth": r'\b(?:setSize|resize|reserve|push_back|emplace_back|insert|append|addEvent|addEvents)\s*\(',
    "dynamic container": r'\b(?:vector|deque|list|map|unordered_map|String|StringArray|MemoryBlock)\s*[<{]',
    "locking": r'\b(?:ScopedLock|ScopedTryLock|ScopedLockType|SpinLock|CriticalSection|shared_lock|unique_lock|scoped_lock|lock_guard|mutex|recursive_mutex)\b|\b(?:lock|try_lock|unlock)\s*\(',
    "waiting or signalling": r'\b(?:sleep|sleep_for|sleep_until|wait|wait_for|wait_until|notify|notify_one|notify_all|yield|join)\s*\(',
    "file or system I/O": r'\b(?:fstream|ifstream|ofstream|FileInputStream|FileOutputStream)\b|\b(?:fopen|fclose|fread|fwrite|fflush|open|close|read|write|stat|mkdir|socket|connect|send|recv|system|fork|exec)\s*\(',
    "logging or UI dispatch": r'\b(?:Logger|MessageManager|DBG)\b|std::(?:cout|cerr|clog)\b|\b(?:printf|fprintf|puts|postMessage|triggerAsyncUpdate|repaint|setBounds|sendChangeMessage)\s*\(',
    "exception": r'\bthrow\b',
}
COMPILED_RULES = {label: re.compile(pattern) for label, pattern in RULES.items()}


@dataclass(frozen=True)
class Function:
    path: str
    name: str
    body: str
    line: int


def mask_literals(source: str) -> str:
    return LITERALS.sub(lambda match: re.sub(r'[^\n]', ' ', match.group()), source)


def functions_in(path: str, source: str) -> list[Function]:
    masked = mask_literals(source)
    result = []
    position = 0
    while match := HEADER.search(masked, position):
        name = match.group('name')
        if name in NON_FUNCTIONS:
            position = match.end()
            continue
        start = match.end()
        depth = 1
        end = start
        while end < len(masked) and depth:
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        if depth:
            raise ValueError(f'{path}: unbalanced function body for {name}')
        result.append(Function(path, name, masked[start:end - 1], masked.count('\n', 0, start) + 1))
        position = end
    return result


def inspect_sources(sources: dict[str, str], roots: dict[str, tuple[str, ...]]) -> tuple[list[str], int]:
    functions = [function for path, source in sources.items() for function in functions_in(path, source)]
    failures = []
    pending = []
    for path, names in roots.items():
        for name in names:
            matches = [f for f in functions if f.path == path and f.name == name]
            if not matches:
                failures.append(f'{path}: missing audited callback {name}; review scope before renaming/removing it')
            pending.extend(matches)
    # Name-only resolution is conservative: audit all matching owned overloads.
    by_name: dict[str, list[Function]] = {}
    for function in functions:
        by_name.setdefault(function.name.rsplit('::', 1)[-1], []).append(function)
    visited = set()
    while pending:
        function = pending.pop()
        key = (function.path, function.name, function.line)
        if key in visited:
            continue
        visited.add(key)
        for label, pattern in COMPILED_RULES.items():
            for match in pattern.finditer(function.body):
                line = function.line + function.body.count('\n', 0, match.start())
                failures.append(f'{function.path}:{line}: {function.name}: {label}: {match.group().strip()}')
        for match in CALL.finditer(function.body):
            pending.extend(by_name.get(match.group(1).rsplit('::', 1)[-1], ()))
    return failures, len(visited)


class GuardTests(unittest.TestCase):
    def check(self, source: str, expected: str | None):
        errors, _ = inspect_sources({'fixture.cpp': source}, {'fixture.cpp': ('processBlock',)})
        if expected is None:
            self.assertEqual(errors, [])
        else:
            self.assertTrue(any(expected in error for error in errors), errors)

    def test_literal_and_comment_text_is_ignored(self):
        self.check('void processBlock() { /* new X; */ const char* s = "lock()"; auto r = R"tag(write())tag"; out[0] = 0; }', None)

    def test_allocation(self):
        self.check('void processBlock() { auto* p = new float[16]; }', 'heap lifetime')

    def test_reachable_helper(self):
        self.check('void helper() { std::scoped_lock guard(m); } void processBlock() { helper(); }', 'locking')

    def test_unreachable_setup_is_not_callback(self):
        self.check('void prepare() { data.resize(512); } void processBlock() { out[0] *= gain.load(); }', None)

    def test_storage_growth(self):
        self.check('void processBlock() { samples.push_back(1); }', 'storage growth')

    def test_io_logging_and_waiting(self):
        for operation, expected in [('fwrite(ptr,1,4,file);', 'I/O'), ('Logger::writeToLog(text);', 'logging'), ('thread.notify();', 'signalling')]:
            with self.subTest(operation=operation):
                self.check('void processBlock() {' + operation + '}', expected)

    def test_missing_callback_is_failure(self):
        self.check('void renamed() {}', 'missing audited callback')

    def test_mixer_wrapper_checks_owned_helper_after_dependency_call(self):
        errors, _ = inspect_sources(
            {'mixer.cpp': 'void normalize() { auto* p = new float; } '
             'void applyToBuffer() { VolumeAndPanPlugin::applyToBuffer(); normalize(); }'},
            {'mixer.cpp': ('applyToBuffer',)})
        self.assertTrue(any('heap lifetime' in error for error in errors), errors)

    def test_nested_blocks_keep_helper_reachable(self):
        self.check('void helper() { throw 1; } void processBlock() { if (ready) { helper(); } }', 'exception')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true', help='test the guard itself without requiring source files')
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GuardTests)
    if not unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful():
        return 1
    if args.self_test:
        return 0
    repo = Path(__file__).resolve().parents[1]
    missing = [path for path in ROOTS if not (repo / path).is_file()]
    if missing:
        print('Missing required audited source: ' + ', '.join(missing), file=sys.stderr)
        return 1
    sources = {path: (repo / path).read_text() for path in ROOTS}
    for path in ROOTS:
        header = Path(path).with_suffix(".h")
        if (repo / header).is_file():
            sources[header.as_posix()] = (repo / header).read_text()
    failures, visited = inspect_sources(sources, ROOTS)
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print(f'Owned callback source guard passed: {visited} callback/helper bodies inspected.')
    print('Excluded engine call: VolumeAndPanPlugin::applyToBuffer inherits engine locking; it is not certified realtime-safe.')
    print('Scope: source patterns only; dependencies, compiler output, runtime allocations, and deadlines are not certified.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
