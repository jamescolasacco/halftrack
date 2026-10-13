# Preview verification

Evidence date: **2026-10-13**. This records the original audio/MIDI preview and subsequent research-driven corrections. It is not a completed DAW or a feature/sound parity certification.

## Automated evidence

The combined Release acceptance executable passed on Apple Silicon macOS in **15.47 seconds** after integrating MIDI, persistence, FIFO export preflight, reentrant export protection and actual editor-handler tests. `ctest` supplies the generated VST3 fixture; direct `--self-test` execution without `HALFTRACK_TEST_PLUGIN` does not establish plug-in support.

| Area | Established by generated fixtures | Limits |
| --- | --- | --- |
| Audio mix | Unity routing, gain, pan, mute, solo and summing against expected PCM | The one-second audio comparisons exclude the first 4096 and final 1024 frames; maximum permitted error is 0.0001. This does not establish transient identity or latency compensation. |
| Recording | Actual engine capture graph fed by an injected stereo sine device; arm, input selection, monitor Off/In, stop-to-clip, media ownership and export | No microphone or physical interface is opened by the test. Hardware clock drift, permissions, latency and deadlines are not measured. |
| MIDI and synth | Stable track/clip/note IDs; note create/move/resize/delete and parameter/tempo undo; fixed 16 voices; sustain/release; 440 Hz fundamental; independently calculated nonzero filter samples; block partition invariance | One bandlimited saw/square subtractive synth, not a complete instrument collection or comparison to proprietary DSP. |
| MIDI render | A note starting at beat one after clip start at 120 BPM begins at sample 24001 at 48 kHz; release and polyphony are exercised | This is a controlled zero-external-latency graph. |
| Parameter transfer | Bounded FIFO order, full-queue handling, concurrent delivery of 10000 complete packets, final state after 129 rapid changes before offline export | Direct owned DSP transfer only; host graph mutations and arbitrary plug-ins are outside this guarantee. |
| Persistence | Versioned human-readable XML; track/clip/note IDs, musical timing, velocity, all synth settings, mixer/tempo, relative audio assets; identical MIDI render after reopening | Reopening intentionally starts a fresh undo history. Keep the project and sibling Assets directory together. |
| Editor interaction | Production hit testing and mouse/key handlers create, move, resize, delete and undo notes; consecutive synth-control notifications preserve settings; track deletion/undo/recreation retains a valid editor | Synthesized JUCE events exercise real handlers, but do not establish OS mouse delivery. |
| Export lifetime | Nested project replacement, edits, undo, transport and recursive export are rejected while rendering; cancellation preserves output and restores editing | Native hardware and arbitrary plug-in reentrancy still need further coverage. |
| Failure preservation | Invalid/malformed project leaves current project intact; failed save retains prior file; unknown plug-in registry entry rejects load with scanning guidance | Project files cannot silently register executable plug-in paths. |
| VST3 | Generated gain fixture discovery, actual processing, nondefault state save/reopen, bypass and removal | No third-party plug-in or Audio Unit compatibility claim follows from this fixture. |
| Export | Exact duration/rate/channels, selected extra render time, worker cancellation preserving an existing destination | A dry tail tests duration and silence, not effect decay. The full rate/depth matrix remains untested. |
| Owned callback checks | Static audit of 16 owned callback/helper bodies and runtime interception of C++ new/delete during prepared synth and queue processing | No complete malloc, mutex, syscall, dependency or deadline certification. See [the real-time audit](realtime-safety.md). |

Tests are in `tests/SessionTests.cpp`, `tests/MidiTests.cpp`, `tests/PersistenceTests.cpp`, `tests/RealtimeTests.cpp` and `tests/realtime_audit.py`. Generated signals, projects, logs and diagnostic exports stay in ignored build/artifact folders. No vendor samples, presets or reference recordings are used.

The initial Windows audio build passed [CI run 38439413736](https://github.com/jamescolasacco/halftrack/actions/runs/38439413736). Its macOS 14 job exposed an older upstream-incompatible compiler. The current workflow targets macOS 15, Windows 2022 and Ubuntu 24.04; combined MIDI results are still pending. Do not infer physical driver or native UI parity from a compile/test pass.

## Native app observations

- The repaired browser starts without enumerating a personal Music folder; it launches responsively and waits for a chosen folder.
- Four original generated audio stems imported with source-derived waveforms. Fit, playback/stop and actual track/master meter activity were observed.
- The audio demo was saved and reopened through native dialogs, retaining all four tracks. GUI export decoded independently with Python `wave` as stereo, 48 kHz, 24-bit, 864000 frames: 16 seconds plus a two-second tail.
- Audio Setup reported CoreAudio speaker output, 96 kHz and 512 samples, with no input selected. This establishes the observed configuration, not measured acoustic output or latency.
- The combined MIDI build starts with a clean project; creating a MIDI track opens its 16-beat clip and actual synth controls. Space and Escape operate transport after the editor receives focus. Coordinate input through the native automation tool did not produce grid edits. The independent production hit-test and actual mouse/key-handler regression passed. Native mouse delivery remains inconclusive; do not mark the full manual MIDI workflow passed.
- Attempting to load a project whose controlled plug-in has not been scanned preserves the active project. The new error directs the user to Scan / Manage; a backend regression verifies reopen after registry discovery.

The [eight-minute smoke path and extended checklist](manual-test-phase1.md) distinguish performed GUI checks from API tests. Physical input, loopback, hot-plug, acoustic monitoring, stress/deadline measurement, and all unlisted GUI cases remain unverified.

## Current boundaries

The preview contains native audio and MIDI editing, one polyphonic subtractive synth, basic track mixing, audio device controls, recording, portable projects, external plug-in hosting and WAV export. Its UI uses an original dark/amber identity and editable SVG mark.

The complete target remains much larger: clip/scene launching and follow actions, automation, sends/returns and advanced routing, racks/macros, full instruments/effects, warp-marker editing, comping/slicing, MIDI expression/tuning, CLAP hosting, recovery and library management are unfinished. AU hosting is enabled on macOS but not verified here. No controls are described as implemented merely because a drawing exists.

The requested **whole-host no-lock/no-allocation callback contract is not met by the selected engine**. Dependency callbacks use locks and recording has fallback allocations. Owned synth checks passing do not waive this gap. Existing proprietary project compatibility, a programmable patch environment, branded hardware integration and cloud features are outside the current accepted scope.

## Research-driven mixer update (2026-10-13)

Original mixer/undo change c805766 built in Release and passed the full local acceptance suite in 48.01 seconds. The new known-good formula tests cover 35 complete-window stereo renders, including 21 pan positions, fader headroom, silent/muted/soloed cases, undo/redo and new/legacy project reopening. Worst observed error was 1.28e-7 PCM. This establishes settled transfer behavior for those generated fixtures; moving automation, active clip-edge transients, internal mixer bypass and physical hardware remain outside this new runtime evidence.

The refreshed local package is `dist/Halftrack-preview-darwin-arm64-ac20d09.tar.gz` (60,952,470 bytes; SHA-256 `449a6ca154874cadc8d531173c59292c3110a684b7d849ac3e14bbc2c2b447bd`). Its executable matches the built app byte-for-byte. The corresponding original source points to ac20d09, includes the reproducible analysis-tool tests, and excludes ignored reference binaries/decompiler outputs. The earlier c2383a5 preview remains a separate historical artifact.

The Ghidra exporter was independently exercised on tiny original C fixtures: two successful outputs, one explicit invalid-address failure, matching hashes/body ranges, duplicate rejection and overwrite preservation. The retained opt-in test passed in 9.686 seconds against local Ghidra 12.1.4/JDK 21. This verifies tooling behavior; the seven selected reference decompilations are research evidence, not application equivalence tests. See [research results](research/reverse-engineering-results.md).

## Recording follow-up

Combined research CI [38476341206](https://github.com/jamescolasacco/halftrack/actions/runs/38476341206) passed Windows and Linux but exposed recording loss on macOS. Slower synthetic driver pacing reproduced it locally: MIDI discovery discarded an active take by clearing playback contexts. A narrow Tracktion patch now defers discovery until recording stops. The focused regression passes twice (8.93 s / 8.17 s), and the full local acceptance suite passes in 53.60 s. The Windows patch-writing correction is committed as `1294f5b`; both exact source hashes remain unchanged. Fresh three-platform CI is [38479881217](https://github.com/jamescolasacco/halftrack/actions/runs/38479881217); results remain pending. The ac20d09 package above is historical evidence and contains the discovered defect. No physical microphone was used in this diagnosis.

Current macOS draft: `dist/Halftrack-preview-darwin-arm64-5f40131.tar.gz` (60,975,548 bytes; SHA-256 `cc2cb27799b126e3df8e874fb96af766ab603051c9ae2948460b4651dd62a79f`). The packaged executable matches the tested build byte-for-byte. Its source archive is exactly 5f40131; its Tracktion archive includes the checked recording-safety patch. Original-source packaging excludes ignored reference binaries and decompiler outputs.
