# Halftrack progress

Updated 2026-10-20. Master-volume automation is implemented in `b0aa4b8`, with production UI in `f04cfb3`. Hosted fixed-envelope playback, sibling-stream updates, manual-level retention, native undo, portable reopening and malformed master-state validation have generated-audio coverage. The preceding integrated build passes all **27 local Mac acceptance suites in 422.51 s**, and the strengthened 36-scenario synth callback matrix also passes. MIDI release repair `6d6fa88` now preserves old channel releases/controller state after a callback interruption; its focused monitoring and recording suites pass in **27.22/34.71 s**. A separately reproduced frozen-clock fresh-key loss is corrected by `e2356e4` with an external-input-only receipt clock; its first focused monitoring run passes in **27.52 s**. Final deterministic guard fixtures `5cb6991` and the complete focused monitoring suite pass in **27.81 s**; the complete integrated build passes **27/27 local Mac suites in 422.17 s**.

Published application checkpoint `645e82c64def76a496eddaac268eb60964476fc1` now passes [clean builds, every native suite and source-inclusive packaging on all three platforms](https://github.com/jamescolasacco/halftrack/actions/runs/39379186381): **macOS 27/27 in 1228.88 s**, **Windows 26/26 in 416.52 s**, and **Linux 26/26 in 350.52 s**. Mac MIDI monitoring/input pass in **73.43/112.09 s**, and master automation passes in **40.93 s**. This supersedes the failed earlier candidate's release validation; it does not prove that every historical intermittent failure had the same cause. The verified local Mac archive contains the exact tested executable and source; its extracted monitoring, automation and UI checks also pass. Later commits add research/status documentation only. Physical controller/listening and whole-host strict real-time safety remain unverified.

REA is installed in an isolated development environment and has produced checked fixture, processing-dispatch and event-timing evidence. It has not produced a recovered complete instrument implementation. Complete DAW parity and reference-exact sound remain unmeasured. The working features, missing functionality and bounded research coverage below remain the project status.

Static analysis independently verifies **392,921 / 392,926** main-program ARM64 outputs (three preparation failures and two 600-second timeouts remain) and **36,596 / 36,599** scanner outputs (three preparation failures remain). All **13,420** declared entries across seven additional device components and two architectures previously produced verified output. [Residual analysis](research/residual-extraction-recovery.md) and [component coverage](research/additional-component-audit.md) retain unresolved entries and fingerprints. Extraction coverage is not feature completion; Halftrack remains an incomplete DAW with the gaps below.

Scanner follow-up `5580373` passes [clean builds, tests and packaging on all three platforms](https://github.com/jamescolasacco/halftrack/actions/runs/38499473562). On Windows, the ordinary two-crash request now completes in **56.14 ms**, compared with 24,052.2 ms at `17a0575`; the complete scanner suite takes **1.08 s**, including a second real-crash case that verifies both retained-process exit classifications, both dead-peer teardown skips, no dead-peer shutdown writes and fresh-child recovery. The ordinary case including final teardown takes 61.17 ms. These are controlled-fixture measurements from one CI run, not a guarantee for arbitrary plug-ins or alive-but-hung workers.

## Working and evidenced

- Repository/folder named Halftrack, original SVG identity, AGPL-3.0-only source, pinned C++20/JUCE 8/Tracktion build.
- Native audio arrangement, generated-source waveforms, playback, mixer gain/pan/mute/solo, input assignment/monitoring, recording, owned media, atomic XML projects, VST3 hosting, offline WAV/AIFF/FLAC export with mono and peak-normalization options.
- Track volume/pan and master-volume automation: graphical and numeric point editing, snapping, native gesture undo, read bypass, retained manual values, tempo remapping, track copying and portable reopen. Generated-audio and editor tests pass locally. Its main-window controls pass the macOS, Windows and Linux CI UI suites and the final combined local suite, including native keyboard focus.
- Eight-band EQ, linked peak/RMS compression, stereo/ping-pong delay with tempo divisions, and an original room reverb: real DSP, native editors, chain order/bypass/remove, project save/reopen, grouped undo, complete-file audio/tail oracles and owned callback guards. Advanced controls and reference sound matching remain incomplete.
- Eight MIDI note tools: quantize starts/ends, reverse, pitch inversion, legato, halve/double timing and fixed length; selected notes or whole-clip UI operation, atomic bounds rejection, stable selection, undo/redo and project tests.
- Insertable channel Utility: gain/balance, width/mid-side, channel selection/polarity, mono/bass mono/audition, mute/DC filtering, native editor and device ordering. Complete-file render, project and undo regressions pass locally. Reference numerical identity remains unmeasured.
- MIDI controller selection, Off/Auto/In monitoring, native Arrangement recording/merge overdub, event and release-velocity retention, held-note/seek finalization, one-take undo and portable editable recordings pass generated-input tests. Physical timing/hot-plug, MPE and record-mode/channel controls remain unfinished.
- Tap tempo and audio/MIDI clip activation now have dedicated native render, running-playback, undo/reopen and UI-handler tests. Activation retains media and Session ownership; full physical/reference timing is unmeasured.
- MIDI tracks, stable-ID piano-roll editing, a sixteen-voice subtractive synth with saw/pulse/sine/noise, pulse width, channel-local sustain and pitch wheel, tempo, undo/redo and portable project reopening have automated coverage.
- Session audio/MIDI slots, scenes, beat/bar launch boundaries, exclusive switching, Stop/Return and graph-rebuild ownership pass local rendered-signal tests. Native UI handlers and malformed-slot project rejection pass. Empty-slot takeover now passes arrangement-only, source-free and rebuild/cancellation cases.
- Native Pre/Post audio/MIDI sends, shared returns, mute/solo, effect ordering, saved routing, grouped undo and malformed-route preservation pass generated-signal tests. Sends/return UI handlers and eight rendered layouts pass local review; physical input delivery is separate.
- Export latency now preserves active clip edges and very short files. The real 127-sample VST3 oracle passes 17 full-window comparisons, including 64-frame clips and five chained delays; ten edge exports also independently decode bit-identically to their dry reference.
- Initial combined local Mac acceptance suite passed in 15.47 seconds after MIDI/FIFO, editor-handler and export-guard integration. Includes synthetic recording, controlled VST3 state, portable project/media, rendered mixer samples, cancellation preserving an existing destination, tail duration, gesture undo, startup dirty-state regression.
- Earlier GUI check: four generated stems imported, playback position/meters advanced, CoreAudio output setup visible, Fit and unsaved Discard worked. No microphone was captured.

## In progress

- Engine MIDI tracks/clips/notes and built-in polyphonic synth pass deterministic DSP, undo, saturated FIFO and persistence tests. Export reentrancy now rejects edits/project replacement while the worker owns its graph, with a cancellation/preservation regression.
- UI: piano roll and synth editing compile. Browser relaunch is verified. Actual editor handler and hit-test regressions pass. Native automation grid input is inconclusive; physical mouse delivery remains unverified.
- Persistence/root: MIDI project round-trip checks, architecture/ownership contract, branding/research retirement, packaging and Signalsmith selection.
- QA: shared editing/tempo source `1881625` passes all 17 Mac suites and all three platform CI jobs. Controller recording passes all 18 local Mac acceptance suites, including generated-device, event-byte, render, undo and UI checks. Physical hardware and native GUI checks remain separate. The workflow targets macOS 15 and requires AppleClang 17+.

## Missing, not stubbed controls

Additional Session launch modes, weighted/random/scene follow actions, Session recording, full MIDI expression, device/send automation, automation recording/override, curved envelope editing and advanced routing/racks, full native effect/instrument inventory, CLAP hosting, full stretch/warp editor, hardware stress tests, crash recovery, and cross-platform GUI certification are not complete. No completion claim is based on a control being drawn. Existing unsupported controls are omitted rather than wired to fake processing.

## Known issues / verification remaining

- Browser relaunch and GUI audio save/reopen/export passed. MIDI grid input through native automation remains unverified. Generated custom/generic plug-in editor lifetime tests now pass; arbitrary vendor/native-input checks remain pending.
- Whole-host strict callback safety is not met: the dependency has locks and recording fallback allocations; see docs/realtime-safety.md.
- The earlier recording loss is fixed and verified locally and in all three platform CI jobs. Earlier ac20d09 is a historical package containing the discovered defect; use the final refreshed draft.
- No measured physical input latency, acoustic loopback, deadline stress, or arbitrary plug-in real-time safety evidence.
- The new latency oracle includes opening/closing clip samples. Older mixer tests use settling margins; nonlinear processor parity and whole-product numerical identity remain unproved.

## Ownership log

Root revised repository policy and research/docs after the owner's new rules. Earlier branded research remains in history for transparency but is retired from the current tree; no history rewrite. Engine owns Session and synth; UI owns MainComponent; QA owns CI/audit/manual checks; root owns integration and persistence tests. Root will coordinate any edits to Session persistence functions with engine before writing them.

Root added independent MIDI project round-trip/failure-preservation tests in tests/PersistenceTests.cpp and wired future engine MIDI tests into Main/CMake; no edits to engine-owned persistence implementation. Tests await the engine integration build.

Root QA: browser-fixed app relaunch succeeded; native dialog imported4ordered stems into reused/new tracks, saved/reopened all4tracks, and exported stereo48kHz24-bit864000frames (18seconds including2second tail), independently decoded with Python wave. Added initial main-window keyboard focus in Main.cpp after observing shortcuts without a focused component.

UI update: committed the bounded asynchronous browser fix separately as d31d42b after a successful local build. Implemented the MIDI editor against the engine-owned Session facade: stable-ID note drawing, selection, dragging, edge resizing, velocity, keyboard transposition/time moves, real arrangement note previews, and synth controls. MIDI integration build and GUI verification are pending; these controls are not yet being claimed as verified. This appended ownership note is the UI agent's only documentation edit; root retains the surrounding progress sections.

Root coordinated a narrow Session::loadProject error-message change with engine: projects requiring unscanned plug-ins now direct the user to Scan / Manage before reopening. The registry remains the source of trusted discovered plug-ins; project files do not automatically register arbitrary executable paths. Independent persistence test covers failure preservation and successful reopen after registration.

Root integration: MIDI/persistence/owned allocation suite passed15.21seconds. QA commits ab8b82f and8461038 add three-platform CI, scoped callback checks and an eight-minute user smoke path. Engine commit4b61e16 includes MIDI and the FIFO export preflight. Root wired actual UI handler tests after native automation grid clicks showed no edits; verification remains explicit.

Final local integration passed15.47seconds, including actual piano-roll mouse/key handlers and graph reentrancy tests. Export lifetime guard is fe02b10. Static callback audit passes8self-tests and16owned callback/helper bodies. No global real-time guarantee or full native MIDI walkthrough is claimed.

## Draft handoff requested by owner

The owner requested an immediate draft handoff on 2026-10-13. Source is published through c2383a5; the locally tested macOS archive is `dist/Halftrack-preview-darwin-arm64-c2383a5.tar.gz`. The app is open with the original five-track `artifacts/demo/Halftrack First Session.halftrack` example, containing four generated stems and forty original synth notes. Playback advanced with real track/master meter activity. Native automation timed out after opening the file, but a stack sample showed normal event processing and reconnecting displayed the loaded session; no app-hang fix was justified.

Local acceptance and production editor-handler tests passed in 15.47 seconds. Native grid mouse delivery remains inconclusive through automation. The three-platform CI run is [38445146571](https://github.com/jamescolasacco/halftrack/actions/runs/38445146571); at handoff macOS had compiled and was testing, while Windows/Linux builds were running. CI is not yet declared green. At handoff, the complete feature sweep remained unfinished and development paused for feedback. The subsequent owner instruction below authorizes renewed work.

## Linux link portability correction

QA ownership: root authorized this agent to edit only the Linux link dependency in CMakeLists.txt; engine, mixer tests and shared model code remain with their existing owners. CI run [38445146571](https://github.com/jamescolasacco/halftrack/actions/runs/38445146571/job/115461470388) compiled Linux sources but failed final linkage on Tracktion WaveNode/WaveNodeRealTime references to `__atomic_store`. The macOS and Windows jobs passed. The correction explicitly links Linux libatomic for the engine-dependent Halftrack executable; the JUCE-only plug-in fixture does not require it. JUCE's configure probe tests only long-long atomics and does not cover the engine's aggregate operations. Existing macOS CMake reconfiguration and whitespace checks passed. Linux runtime and final-link verification await the next CI run; no local Linux/container environment is available, and this build correction does not establish lock-free behavior.

## New direction: binary-assisted research

The owner explicitly superseded the earlier no-decompilation rule and preview pause. Parallel research covers tooling, vetted agent skills and measurable audio/workflow parity. Root completed read-only Mach-O inventory and an isolated official Ghidra setup; execution evidence appears below. Raw target material stays in ignored local artifacts. No claim of source recovery, completed clone or zero differences is made.

## Panning correction ownership and verification

The parity-research agent has root-assigned ownership of `src/Session.cpp` and `tests/SessionTests.cpp` for the panning correction. This is a coordinated edit to the engine/mixer area. New tracks will use an explicitly serialized constant-power mixer with unity center; existing serialized mixers retain their prior sound. No proprietary source or assets are used. Focused offline-render matrix tests and combined acceptance results will be recorded below after execution.

Root integration: three primary-source research reports now cover tool selection, inspected skills and behavioral measurement. Official Ghidra 12.1.4 archive SHA-256 matched the published digest; macOS ARM64 native components built successfully using JDK 21 and the supplied Gradle wrapper. The isolated ARM64 reference copy imported successfully with automatic analysis disabled and no dependent libraries imported. Local metadata inventory records both slices and embedded debug-section absence. Decompilation coverage is not yet claimed.

Original Mach-O tooling has 20 passing synthetic tests (45a8eb9). The actual reference inventory succeeded. Linux atomic linkage correction is a93b813; both are pushed. Root owns shared documentation; agents own their respective tool, mixer and CI changes. Raw reference artifacts remain ignored.

The panning task also has root-approved ownership of the narrow callback-root addition in `tests/realtime_audit.py`. The new multiplier is audited as owned source; the inherited engine mixer remains an explicitly excluded dependency call with its documented lock limitation.

Panning correction verification: the combined Release acceptance suite passes in **48.01 seconds** on Apple Silicon macOS. New audio/MIDI tracks serialize `halftrack-mixer`, which retains the engine's volume/pan automation and smoothing, selects its sine/cosine law, and multiplies the first two audio channels by sqrt(2). At user gain g and pan p, the diagonal gains are g·sqrt(2)·cos((p+1)·pi/4) and g·sqrt(2)·sin((p+1)·pi/4); center is unity and extremes are +3.0103 dB. Master behavior and existing serialized `volume` mixers are unchanged. Legacy projects intentionally retain their linear panning; this correction does not retroactively migrate their sound.

The generated stereo matrix test covers 21 pan positions, -12 dB and maximum +6 dB engine fader gain, exact zero/mute silence, solo, mute/solo/pan undo and redo, and both new-type and legacy save/reopen. Every rendered fixture sample is compared, with silent margins allowing the existing graph to settle; this does not characterize active clip-edge ramps. Those tests exposed an existing mute/solo undo omission: their engine cached values used no undo manager. The Session facade now writes the same state properties with the edit's undo manager, verified by state and rendered output. The source callback audit passes **9 self-tests and 17 owned callback/helper bodies**, while explicitly excluding the inherited engine mixer's known locks from its guarantee.

## Verified local analysis and mixer correction

The isolated ARM64 reference imported and seven explicitly selected utility-processing entries decompiled successfully with per-address manifests; no whole-program or zero-difference claim is made. The reference's 392,926 declared ARM64 function-start addresses exactly match Apple's independent LLVM inventory. Original binary SHA-256 is unchanged after analysis. See [local research results](research/reverse-engineering-results.md) for versions, scope and private artifact locations.

Commit c805766 passes the full local acceptance suite in 48.01 seconds, including 35 complete-window mixer renders with a worst PCM error of 1.28e-7 against independent math. New tracks get normalized sinusoidal panning; existing project tracks preserve legacy behavior. Mute/solo now undo correctly. Callback audit passes nine self-tests / seventeen owned bodies, with inherited dependency locks still explicitly unresolved. Combined CI at ac20d09 caught a macOS synthetic-recording failure after the new mixer checks passed. Diagnosis is in progress; CI is not green. The refreshed macOS package is `dist/Halftrack-preview-darwin-arm64-ac20d09.tar.gz`; binary identity, source revision and exclusion of raw analysis artifacts were verified.

The retained opt-in Ghidra integration fixture (15b3587) passed in 9.686 seconds against local Ghidra 12.1.4/JDK 21. Ordinary parser tests remain tool-independent. Independent source review found no regression in mixer automation ordering, bypass dispatch, smoothing initialization, subtype lookup or extra-channel gain handling; these source findings do not substitute for new dynamic automation/multichannel runtime tests.

Recording CI diagnosis ownership: the parity-research agent owns the focused recording fixture extraction in `tests/SessionTests.cpp` and the root-authorized `--self-test-recording` dispatch seam in `src/Main.cpp`. Initial ownership covered the focused recording investigation; the subsequent extension below authorized the dependency-level fix after the actual cause was reproduced. No physical audio input is opened.

Recording CI fix ownership extension: the parity-research agent owns the narrow Tracktion integration patch and CMake application block, as authorized by root after reproducing the active-take loss during deferred MIDI discovery. Root retains dependency/architecture/package documentation.

## Active-take loss during MIDI discovery

Combined CI [38476341206](https://github.com/jamescolasacco/halftrack/actions/runs/38476341206) passed Windows (45.52 s acceptance) and Linux (38.27 s), including all 20 parser tests and packaging. macOS passed the mixer checks but failed recording after 121.78 s. The missing WAV was reproduced locally by pacing the injected synthetic driver at 30 ms per block: the engine's initial MIDI refresh cleared playback contexts and discarded the take before the test called Stop. Fast 2 ms pacing passed, proving that a longer post-stop wait would not repair the lost recording.

The coordinated fix is a pin-checked, idempotent Tracktion source patch applied by CMake to downloaded and local dependency sources. Automatic and explicit MIDI refresh wait until all recordings stop, with a 100 ms retry and no persisted scan-setting change. The regression crosses the timer deadline, explicitly rescans during capture, verifies recording remains active, and checks captured signal plus discovery after stop. Temporary diagnostic logging and environment-based pacing controls have been removed. The permanent regression failed before the patch and passes twice afterward (8.93 s and 8.17 s). Independent review found no actionable issue; temporary-copy checks passed initial application, repeat application, unknown-source rejection without mutation, and CRLF helper compatibility. Commit `5f40131` passes the full local native acceptance suite in 53.60 s. The Windows patch-writing correction is committed as `1294f5b`; both exact source hashes remain unchanged. Final three-platform CI [38479881217](https://github.com/jamescolasacco/halftrack/actions/runs/38479881217) passed on macOS, Windows and Linux, including acceptance tests and source-inclusive packaging.

The intermediate local verification archive is `dist/Halftrack-preview-darwin-arm64-5f40131.tar.gz` (60,975,548 bytes; SHA-256 `cc2cb27799b126e3df8e874fb96af766ab603051c9ae2948460b4651dd62a79f`). Its executable matches the tested build. The included original source is exactly 5f40131 and includes the patch script; the included Tracktion source has the verified patched hash. No raw reference binaries or decompiler artifacts enter the original source archive. Earlier archives remain historical snapshots, not the current draft.

Windows configuration follow-up ownership: root authorized the same engine agent to correct only `cmake/PatchTracktion.cmake`. Run 38479154559 accepted the exact upstream source but failed the post-write hash on Windows. Official CMake 4.4.4 source shows `file(WRITE)` uses text mode, translating LF to CRLF there. The correction uses explicit LF output via `file(CONFIGURE ... NEWLINE_STYLE LF)` while retaining both exact source hashes. Independent temporary-copy validation passes, and final Windows CI confirms successful configuration, compilation, acceptance and packaging. No audio-processing source changes are part of this follow-up.

Final run 38479881217: macOS passed the full native acceptance suite in 145.04 s and completed packaging, including the recording regression that previously failed. Linux also passed tests and packaging. Windows also passed configuration, compilation, acceptance and packaging, completing all three platforms successfully.

## Final research handoff

All source is published through `1294f5b`, followed by documentation-only updates. [Final CI 38479881217](https://github.com/jamescolasacco/halftrack/actions/runs/38479881217) is green on macOS, Windows and Linux. Local Release acceptance passed in 53.60 s; CI macOS passed in 145.04 s, Linux in 44.09 s and Windows in 51.52 s. The original Mach-O parser's 20 tests pass on every platform. The final local Mac archive is generated from the clean documentation-inclusive commit; its filename and bundled `SOURCE.json` identify the exact revision, and the included Tracktion source contains the hash-verified recording patch.

Research is complete for this handoff: six tools and eight skill candidates surveyed; the vetted Ghidra workflow is working locally; seven selected entries exported with evidence manifests; concrete type/boundary limitations recorded after disassembly checks. The complete DAW, full reference comparison corpus, physical hardware certification and all missing features above remain unfinished.

## Continued implementation after the draft

The owner explicitly requested continued decompilation and closest achievable parity beyond the draft. Root owns a generated nonzero-latency VST3 fixture and independent compensation tests (`tests/TestLatencyPlugin.cpp`, `tests/LatencyTests.cpp`), with Main/CMake wiring. Engine owns Session launcher/model/persistence and `tests/LauncherTests.cpp`; UI owns the Session grid and editor interactions in MainComponent. The analysis agent owns resumable bulk tooling and exclusive mutation of the local Ghidra project. Shared progress and evidence remain with root; raw target analysis remains ignored. No physical audio input is used by the automated tests.

Independent nonzero-latency host validation passes locally in 10.46 seconds. An original VST3 fixture reports and implements a 127-sample delay. Direct processing is sample-exact across four block sizes; seven full-window 48 kHz stereo exports verify single/serial delays, parallel dry paths, save/reopen, bypass and removal without shifting the comparison. This establishes the exercised engine compensation paths, not every external plug-in or hardware latency. Root also owns the narrow synthetic test-device factory in `tests/SessionTests.cpp` and CI discovery of all pure Python tooling tests for this integration.

UI documentation ownership extension: root authorized the UI agent to append Session/launcher procedures to `docs/manual-test-phase1.md` and this ownership note. These procedures begin NOT RUN; production handler regressions, native automation, acoustic audition, and hardware timing remain separate evidence. No engine, persistence, or root test files are edited by the UI agent.

Launcher timing ownership extension: root authorized the engine agent to add a separately pin-checked `cmake/PatchClipLauncher.cmake` touching the native track ownership state and switching node. Synthetic audio exposed arrangement leaking back after launch until message-thread publication. The fix must latch audio ownership at the scheduled boundary, preserve it after Stop, and make explicit Return immune to stale callback writes. Root owns the CMake include and dependency/package documentation; this agent owns the narrow patch and regression.

Root has engine-authorized ownership of the narrow raw launcher-shape validation in `Session::loadProject` and independent malformed-slot regressions in `tests/PersistenceTests.cpp`. This validation occurs before engine object creation; engine retains scheduling and native patch ownership.

Export boundary ownership: root owns `tests/LatencyTests.cpp`, the new pin-checked `cmake/PatchRenderLatency.cmake`, and its CMake wiring. Strengthening the existing real VST3 test exposed lost opening samples. The independent reviewer identified ignored audio-view offsets and incorrect short-block trimming in the pinned offline renderer. Engine-owned Session changes are not needed for this correction.

Launcher documentation ownership extension: root authorized the UI agent to reconcile `docs/dependencies.md`, `docs/realtime-safety.md`, `docs/feature-parity.md`, and `docs/preview-verification.md` after commits 8606b05, 0f9e217, and 16cb96b. The update records the three-file launcher patch and locally passing Session/UI subset while keeping current CI, the unresolved latency-edge test, and unperformed manual/hardware checks distinct. Source, architecture, and other progress content remain with their owners.

Empty-slot takeover ownership: root authorized the engine agent to extend `Session`, `tests/LauncherTests.cpp`, the launcher architecture section, and the separately pinned launcher patch (including a narrow `EditNodeBuilder.cpp` hunk). Launching an empty slot or scene cell must silence arrangement audio at the common launch boundary and retain session ownership until Return. A per-track native launch handle schedules that silent takeover; no hidden clip or arrangement mutation is introduced. Root retains CMake/Main, export-latency work, shared build ownership, and final dependency documentation.

Export correction verification: root commit `195bcd0` fixes both native offline-render slice defects. The first red run failed in 8.79 s; fixing only the view offset resolved the 48000-frame cases but left the 64-frame test red (14.14 s). Both fixes pass the full focused latency suite in 17.40 s. Independent 24-bit WAV decoding confirms all ten edge exports match their respective dry graphs exactly. Tooling discovery ran 28 tests (26 passed, 2 optional Ghidra integrations skipped); the owned callback guard passed 9 self-tests and 17 source bodies. Full integration awaits the empty-slot follow-up; no new whole-host or hardware safety claim is made.

Scanner safety ownership: root authorized the independent QA agent to add `cmake/PatchPluginScanner.cmake`, `tests/PluginScanTests.cpp`, and a narrow construction-only probe in the original gain fixture. The public seam is the native known-plug-in scanner with a real child process; tests cover discovery, crash/cancel/restart, and process identity without audio-device or microphone access. Root retains Main/CMake wiring, Session, shared build coordination, and final documentation integration. The patch will synchronize replies and connection flags and reject child-launch failure instead of scanning inside the host. AUv3 discovery remains excluded by the current UI and is not covered by this isolation claim.

Audio-oracle hardening ownership: root owns the narrow `tests/SessionTests.cpp` comparison change following independent review. Decoded audio now rejects nonfinite samples across the whole file; the older steady-gain comparison validates dimensions and finite values even in its settling margins. This does not change application DSP or claim those margins match a reference envelope.

Asynchronous editor ownership extension: root authorized the UI agent to add `tests/PluginEditorTests.cpp` and the exact-pin `cmake/PatchAsyncPlugin.cmake`, then change only `PluginWindow`, `UIBehaviour::createPluginWindow`, and `Session::showPluginEditor` after the engine releases Session.cpp. The test seam uses an original synthetic asynchronous plug-in format and covers loading/failure, actual editor content, and removal/track deletion/undo/project replacement. Root owns Main/CMake wiring and build coordination; launcher and persistence source remain outside this edit.

Integrated empty-slot/export checkpoint: the complete four-test native suite passes in **69.62 seconds** (latency 15.57 s, UI 0.83 s, launcher 3.59 s, session 49.61 s), including root's finite-sample oracle checks. Engine commit `b24cb42` owns silent empty targets through source insertion, graph rebuilds and all-scenes deletion; explicit Return restores arrangement playback. Root reconciled architecture, research contract and manual UI-26 expectations after engine released the shared documentation. Those manual checks remain NOT RUN. The new scanner and asynchronous-editor regressions are separate work in progress.

Reference interaction check: the installed application reopened, but macOS refused terminal Accessibility access (osascript error -25211). No reference project was edited or recording captured. Automated firsthand interaction remains unverified in this environment; this does not block independent implementation or offline tests.

### Controlled Audio Unit verification ownership

The engine agent owns new `tests/AudioUnitTests.cpp` and its macOS-only process-local registration helper. Root owns the AU build target and test-dispatch wiring. The fixture remains inside the build directory, is registered only inside test processes, and is never copied into a user Components directory. Existing plug-in UI and scanner files remain with their assigned owners.

Scanner and AUv2 checkpoint: scanner commit `27478dc` passes the real child-process regression in 4.44 seconds; initial combined integration passed it again in 4.23 seconds. AUv2 fixture commit `367b06d` passes in 6.75 seconds with child PID evidence and seven complete-window renders (maximum PCM difference 1.19e-7). Root owns their CMake/Main wiring and documentation. The asynchronous editor suite is intentionally red before correction: early editor requests, failed creation state and redo after undo during loading need resolution. The older four-suite code at `5961295` now passes all three CI platforms; this does not certify these newer changes.

Scanner retry follow-up ownership: root authorized QA to extend the scanner patch and native regression after review found that infrastructure launch failure incorrectly blacklisted an untested plug-in. The regression must retry on the same known-plug-in list without clearing its blacklist; deliberate child crashes must remain blacklisted. Root retains build scheduling and final source-pin/documentation integration.

Async completion diagnosis extension: root authorized the UI agent to extend the pinned ExternalPlugin.cpp correction to detached callback completion and its existing track-properties reattachment hook. The red lifecycle test proves that completing a plug-in after undoing its insertion destroys redo history (canRedo changes from true to false). The correction must retain undo/redo restoration and also test attached completion against unrelated redo; no general UndoManager or launcher changes are included.

Scanner retry follow-up: independent review found that the first fail-closed branch classified scanner startup failure as a plug-in crash, causing a persistent blacklist entry. A same-list retry regression failed in 0.42 seconds and passes in 4.08 seconds after `de9a0d0`. No blacklist reset occurs between the failed launch and the successful retry; real child crashes still blacklist. Root retains integration wiring and final publication.

Send/return ownership: root authorized the engine agent to prepare `tests/SendReturnTests.cpp` while Session remains with the UI agent for editor lifecycle fixes. No send API or controls are claimed as implemented yet. The accepted native-aux contract uses stable destination IDs, strict load validation, original audio fixtures and undo/redo; combined solo behavior is recorded as an explicit working default in open-questions. Root retains Main/CMake wiring, shared build coordination and final documentation.

Integrated plug-in checkpoint: root wiring `727c36e` and UI lifecycle fix `6f4b63c` pass all seven local suites in **88.10 seconds** (AUv2 6.13 s, editor 3.31 s, scanner 4.15 s, latency 16.74 s, UI 0.87 s, launcher 3.69 s, session 53.19 s). Editor tests now verify actual custom/generic contents, processor registration, close/reopen, removal/undo/redo, track deletion and project replacement, detached completion and unrelated redo preservation. The host teardown omission was separate from the engine async-state defect. Root published this source checkpoint; remote CI is pending.

Worker-completion ownership: independent review confirmed JUCE permits creation callbacks on other threads. Root assigned the QA reviewer only `tests/PluginEditorTests.cpp` and `cmake/PatchAsyncPlugin.cmake` for a deterministic callback-thread regression and narrowly demonstrated fix. Engine now owns Session/header and the new send/return tests; UI will consume the documented routing facade. The passing message-thread tests above do not establish worker completion safety.

Worker delivery correction: `6235185` queues asynchronous plug-in results onto the message thread before testing the weak target or touching edit state. Four deterministic worker cases failed before correction (4.55 s total) and pass afterward (expanded editor suite 4.42 s). Success/failure, undo before queued delivery and Session destruction before delivery are checked without timing-probability assertions. Five disposable pin cases plus reapplication pass; independent lifecycle review found no additional defect in the exercised scope. Full integration with sends remains pending.

Preserved Mac checkpoint: `dist/Halftrack-preview-darwin-arm64-9b22c26.tar.gz`, 61,222,818 bytes, SHA-256 `313f4f01c9c9ca85207ab1b3c6d61b66afba3f7b419898ac7dbb7d151c146803`. Its executable matches the seven-suite tested build, original source is exactly `9b22c26`, and the bundled async/scanner/render source pins match that build. Private reference analysis is excluded. This snapshot predates worker delivery and sends/returns; it is retained for reproducibility while work continues.

Independent analysis checkpoint at 2026-10-14 01:00:36 UTC: 117,864 indexed outputs verified present, nonempty and SHA-matching (215,370,004 bytes), with thirteen explicit failures, eleven timeouts and 275,038 unattempted entries. The active unindexed batch was excluded. This is tool-output integrity, not reconstructed semantics or feature parity.

Pending-sort ownership extension: root authorizes the engine agent to add `cmake/PatchEditSort.cmake` for three exact-pinned Tracktion files and invoke its narrow synchronous sort hooks from Session. A rendered send-gesture regression proved that native deferred structural sorting consumed the first Undo after export. The fix keeps native ordering actions inside their initiating transaction without pumping arbitrary UI events. Root owns CMake wiring and final dependency evidence.

Windows scanner follow-up ownership: QA owns only `tests/PluginScanTests.cpp` for a crash-case deadline correction and elapsed child-event diagnostics. CI 38492940509 passed macOS and Linux; Windows passed ordinary scanning and all other native suites, but failed the deliberate two-crash case at its 15-second deadline. The pinned Windows pipe can spend eight seconds detecting a dead child and another eight sending shutdown before retry. The downloaded events show one crashed child before cancellation. Only this case receives a 45-second bounded budget; production timeouts and the other test deadlines remain unchanged. Windows confirmation awaits CI.

Send/return backend integration passes in **37.05 seconds** on local Apple Silicon. Forty-eight complete-file renders plus nine running-graph signal checks pass; maximum observed PCM difference is 1.79e-7. Cases cover Off/unity/fractional sends, destination-wide Pre/Post, source and return mute/solo, combined solos, two sources/returns, external effect ordering and bypass, MIDI sources, 127-sample compensation on unequal paths, and 64-frame exports. The running graph preserves exactly 127 samples of an existing return tail when an unrelated empty source is soloed, then reaches exact silence. Grouped send undo, pristine return creation, removal/restore and fourteen malformed routing loads pass; rejected loads preserve the active unsaved edit and real undo/redo history. The initial UI subset is still red on an exact floating-point dB assertion; final integration and screenshots await that diagnosis.

Independent frozen analysis verification at 2026-10-14 01:32:16 UTC checked all 185,447 indexed completed files: present, nonempty and SHA-matching, 343,048,252 bytes with zero integrity issues. Thirteen failures, twelve timeouts and 207,454 unattempted entries remain in that snapshot. The coordinator continues separately; these are dated output-integrity counts, not recovered semantics or a finished clone.

Routing UI `7567f4f` passes production handler acceptance in **2.01 seconds**. The failed unity assertion measured -0.000000238 dB from native float conversion; non-Off comparisons now use a finite 1e-5 dB tolerance while exact Off, requested slider unity and displayed values retain explicit checks. Return slots are tested as empty scene-aligned native slots, not an absent vector. All eight new rendered layouts were reviewed at 1100x720 and 1440x900; root independently checked two representative images. No native OS input, acoustic output or physical device claim follows. Full eight-suite integration is running.

Focused private reference triage verified twenty retained output hashes and eight bounded independent LLVM/symbol artifacts for a seven-helper crossfader candidate family. Caller/stub and table-selection links are supported; final runtime dispatch, UI mapping and audible behavior remain unresolved. The public research summary records only factual findings and a future measurement protocol. No extracted implementation entered the application.

Full local routing/plugin integration at `ce1128e` passes **eight suites in 133.65 seconds**: AUv2 6.26 s, sends 41.97 s, asynchronous editor 4.54 s, scanner 4.47 s, latency 16.71 s, UI 1.20 s, launcher 3.71 s, and session 54.75 s. The source callback guard passes nine self-tests / seventeen owned bodies; tooling discovery ran 28 tests with 26 passes and two explicitly optional Ghidra integrations skipped. Six disposable structural-sort patch cases pass. Remote three-platform CI is the next verification boundary; the Windows deadline correction is not yet declared resolved.

Published and packaged checkpoint: `17a0575`, [CI 38496744486](https://github.com/jamescolasacco/halftrack/actions/runs/38496744486) running. The local Mac archive is `dist/Halftrack-preview-darwin-arm64-17a0575.tar.gz` (61,323,645 bytes; SHA-256 `adaee5a77362a57bc00137e7898145b7f54de78661b4d57450d709e052de0bc0`). Its executable is byte-identical to the eight-suite tested app, SOURCE.json identifies exact commit 17a05753e30bb49ee6efb0a719a4f20c8ffd04f4, and the original source includes routing/tests/patch helpers with private analysis excluded. Six current native patch outputs were checked inside the bundled source archive.

Final three-platform verification: [CI 38496744486](https://github.com/jamescolasacco/halftrack/actions/runs/38496744486) completed successfully for source `17a0575`, including clean configuration, compilation, native tests, callback/tooling checks and source-inclusive packages. Windows passed seven suites in 149.33 s; scanner acceptance took 32.96 s. Its two distinct crashed children were observed at 50.3 ms and 16088.6 ms, and the two-crash request completed at 24052.2 ms, confirming why the previous 15-second budget was inadequate. The deadline correction preserves native timeout behavior; faster dead-child detection remains a separate investigation. This does not establish physical drivers, arbitrary plug-ins, native OS input or complete DAW parity.

Windows confirmed-exit recovery ownership: root authorizes the engine agent to add the exact-pinned `cmake/PatchChildProcess.cmake` for JUCE's coordinator header/implementation and extend `cmake/PatchPluginScanner.cmake`. QA owns the real-process regression in `tests/PluginScanTests.cpp`; root owns CMake ordering, corresponding-source packaging verification and documentation. The narrow change may query the retained Windows process handle from the serialized scanner worker, prefer a complete reply, and skip shutdown writes only for a confirmed exited child. Healthy/hung-worker timeouts, generic pipes and audio callbacks are outside this change. Test-only structural observations must compile out of production; Windows execution is required before claiming a speed improvement.

Piano-roll selection ownership: root authorizes the UI agent to extend only `src/MainComponent.*` and its embedded acceptance checks with selection of multiple notes and atomic group editing through the existing Session facade. Shift-click selection, an explicit marquee gesture that preserves ordinary drawing, common bounded movement and bulk deletion must retain one-step undo. Engine source remains unchanged; root owns architecture, manual steps and final integration. The exact interaction contract and measured checks will be recorded before claiming completion.

Confirmed-exit scanner candidate: helpers `8a56333` and real-process tests `b8dc92c` are committed. Fourteen disposable pin/upgrade/rejection checks pass and the new populate-patch-add CMake order configured successfully in 8.8 seconds with local dependency overrides. Windows runtime and speed remain unverified until the new CI run. The local build also contains coherent but not-yet-committed piano-roll edits; any focused local scanner result must be distinguished from exact-commit CI. No package is created from that mixed working tree.

Scanner checkpoint `5580373` is published; [CI 38499473562](https://github.com/jamescolasacco/halftrack/actions/runs/38499473562) is running all three platforms. Local incremental compilation succeeded and focused Mac scanner acceptance passed in 7.43 seconds, using the documented mixed working tree with in-progress piano-roll source. The Windows-specific fast path is not exercised by that Mac run. UI now owns the shared build slot for its new handler regressions.

Independent piano-roll review found a facade-valid group-resize edge: one 1/64-beat note plus another note ending at the clip boundary makes a fixed 1/32 minimum produce an inverted common-delta interval. UI owns a red regression and a correction preserving each existing short note's floor. Root also authorizes a generated raw-project fixture inside the UI acceptance test to establish the accepted loaded-overhang case; no loader/model edits are included. Infeasible group intervals must be rejected before any note changes.

Atomic note-update ownership extension: review of the new group editor exposed a second concrete path: grouped velocity can update a contained note before the existing single-note facade rejects a later accepted loaded-overhang note. Engine now owns a narrow `Session::updateMidiNotes` batch API and MIDI regressions, reusing authoritative validation for all IDs/values before any transaction or mutation. UI consumes it instead of duplicating engine validation. No general command abstraction or loader policy change is included; root retains documentation and final integration.

Independent frozen analysis verification at 2026-10-14 02:14:37 UTC checked all 266,335 indexed completed outputs: present, nonempty and SHA-matching, 460,872,719 bytes with zero integrity issues. Thirteen failures, twenty timeouts and 126,558 unattempted entries remain in that snapshot. The read-only database transaction closed before hashing; active unindexed work was excluded. The coordinator continues, and counts do not establish recovered semantics or product parity.

Scanner final verification: CI 38499473562 completed successfully for exact source `558037392f63f8289ac2a54b738d3c445cf258b1` on macOS, Windows and Linux. The Windows artifact's SOURCE.json identifies that exact commit; both modified JUCE files and the scanner helper source match their recorded hashes inside the corresponding-source archives. Archive SHA-256 is `837a3d130c58561a2dbf332312394960072c8afac919dc4009f1d7d1aa956816`. The current piano-roll/model work is newer and still requires its own integration run.

MIDI batch `a5c8984` passes combined compilation and focused UI/session acceptance in 55.18 seconds (UI 1.78 s, session 53.36 s). The first short-note resize regression failed in 1.76 s; the exact accepted-overhang case failed in 2.23 s; after interval guards, the separate grouped-velocity partial-update regression failed in 2.02 s. The batch facade resolves that third failure by validating all requested notes before mutation. Tests cover real redo/dirty preservation, empty and quantized no-ops, invalid later values, missing/request-duplicate/native-ambiguous IDs, loaded overhang, grouped edits and export reentrancy. Independent source review found no remaining defect in that scope. UI is finishing its selection-membership performance follow-up before full integration.

Integrated MIDI checkpoint `13406de` passes all eight local macOS suites in **133.69 seconds**: AUv2 6.72 s, sends 40.86 s, editor 4.54 s, scanner 4.75 s, latency 16.73 s, UI 1.77 s, launcher 3.70 s and session 54.56 s. Tooling tests ran 28 cases with 26 passes and two optional Ghidra integrations skipped; the owned callback source guard passes nine self-tests and seventeen bodies. UI's focused final run passed in 2.34 s, including 512-note selection/transpose/velocity/Delete and undo/redo. Stable-ID hash membership and one live-ID pruning set remove the reviewed quadratic selection membership path; no runtime responsiveness bound is claimed. Independent final review found no iterator, primary-note or ordering defect. Both actual component layouts at 1100x720 and 1440x900 were reviewed. Native input checks UI-42 through UI-45 remain NOT RUN.

Clipboard editing ownership: root authorizes the engine agent to add atomic note-batch insertion and, if required for safe Cut/Delete, batch removal in `src/Session.*` and `tests/MidiTests.cpp`. The former persistence/architecture agent now owns only `src/MainComponent.cpp` and its embedded UI tests for Copy/Cut/Paste/Duplicate. Root retains shared documentation and integration. Clipboard values use fresh inserted IDs; validation precedes all mutation; invalid batches preserve dirty state and undo/redo. The typed editor clipboard is local to the application and survives clip changes. No operating-system clipboard or cross-application interchange is claimed.

The source-inclusive Mac package for `9b598430221a2fb8ffa3a044d8c8207d347fbd76` passed independent verification: 61,375,803 bytes, SHA-256 `ed494645860085d685423a496d1c49c5be80c52433873140c5c5099b5dd0154b`. The executable matches the eight-suite tested build, the original source matches a fresh Git archive, both dependency source archives match the local build inputs, and all fourteen documented patch-output hashes match. The source excludes generated/private analysis. This verifies packaged identity and source inclusion, not notarization, installation, hardware behavior or an independent reproducible build.

The CLAP primary-source report is complete at `docs/research/clap-hosting-path.md`. No mature drop-in host was established in the investigated sources; the originally suggested extension is a plug-in producer. A bounded original adapter needs registration in both parent and child scanner, full descriptor identity, and separate processing/editor/state/latency tests. No CLAP support is claimed. The new private volume-conversion follow-up checked table construction and a saved-byte restoration method; runtime activation, units and sound remain unmeasured.

Grouped-note checkpoint `9b598430221a2fb8ffa3a044d8c8207d347fbd76` is now verified by [CI 38501588398](https://github.com/jamescolasacco/halftrack/actions/runs/38501588398): Linux completed at 02:44:11 UTC, macOS at 02:46:19 UTC and Windows at 02:47:04 UTC on 2026-10-14. All three passed clean configuration, compilation, native/tooling/callback checks and source-inclusive packaging. The clipboard work is newer and requires its own evidence.

Save/redo correction ownership: the clipboard UI regression exposed a confirmed existing bug in mixed audio/MIDI projects: Save As succeeds and clears dirty state but relocates media through an undo-bound source value, discarding a pending note redo. Root is authorized to change only the final successful-save media relink in `Session::saveProject` and add an independent mixed arrangement/slot/MIDI regression in `tests/PersistenceTests.cpp`. Engine and QA review the narrow model change; UI keeps the strong failing assertions. Saving must not become a musical undo action.

Save-path cache regression ownership: root authorizes the engine agent to add only the same-basename relocation case in `tests/LauncherTests.cpp`. It checks the native current-source cache as well as real current arrangement/slot output without reopening, with old generated files removed and synchronous audio registered only at new paths. Root retains the Session correction and build slot. QA identified that an unchanged relative source string otherwise skips the native source-change listener when the project directory moves.

Clipboard/save integration: the focused UI, launcher and session suites pass together in 61.78 seconds (2.53 s, 3.94 s and 55.31 s). The same-basename relocation regression first failed at the native cached arrangement source in 0.98 s, then passed after explicit normal media refresh for unchanged relative text in a changed directory. Current arrangement and slot output are checked after old generated sources are removed. Both clipboard layouts were independently reviewed at 1100x720 and 1440x900. The text-focus fixture now establishes a real DocumentWindow/TextEditor focus before directly invoking handlers; this is not OS key-event delivery.

Integration ownership handoff: the UI, engine and QA agents finished and froze their reviewed files, then their turns ended with the account usage-limit error before the final commits. Root now assumes ownership of the reviewed `src/MainComponent.cpp` and `tests/LauncherTests.cpp` changes for final integration and separate commits, alongside the root-owned save correction. Their implementations and review findings are retained; no unrelated edits are included.

Integrated clipboard/save source `d5a3688` passes **all eight Mac suites in 120.09 seconds**: AUv2 5.63 s, sends 35.98 s, editor 4.33 s, scanner 3.88 s, latency 15.02 s, UI 2.05 s, launcher 3.89 s and session 49.31 s. The source callback guard still passes nine self-tests/seventeen owned bodies, with inherited dependency locks explicitly excluded. No physical device or complete reference parity follows. The new cross-platform run and package identity check are next.

Continued persistence investigation: root owns a focused `tests/LauncherTests.cpp` regression for a loaded audio clip removed before Save As, then restored through actual undo. Active-source relinking cannot by itself preserve relative media paths retained only in undo history. The next test distinguishes this case from the already verified current-clip cache fix before any further model change.

The removed-clip relocation regression failed in 1.32 seconds after native undo restored both clip IDs but resolved their saved relative paths against the new folder. Root now anchors source paths in runtime state on load and successful save, while the separately serialized project remains relative. This replaces the special same-text refresh branch: an actual relocated absolute path triggers the normal media listener and retains musical undo. The regression includes repeated deletion redo/undo after newly collecting restored media, then real arrangement/slot playback with old generated sources removed. A test-only XML tag typo was corrected to the native `AUDIOCLIP` identifier; no behavior assertion was relaxed. The final integrated Mac build passes all eight suites in **121.67 seconds** (AUv2 6.24 s, sends 36.38 s, editor 4.57 s, scanner 4.02 s, latency 15.10 s, UI 2.04 s, launcher 4.24 s and session 49.07 s). This verifies current and undo-restored arrangement/slot playback after relocation as well as pending redo and relative on-disk media; it does not persist undo history across reopening or establish hardware behavior.

Published clipboard/source-cache checkpoint `b4006fd` passed clean builds, all tests and packaging on macOS, Windows and Linux in [CI 38504992018](https://github.com/jamescolasacco/halftrack/actions/runs/38504992018). The later undo-retained-media correction is committed separately as `18565b6`; its next cross-platform run remains pending. Its Mac archive was independently checked against the tested executable, the exact committed own-source archive, all bundled dependency files/licenses and fourteen documented dependency patches. Archive size 61,435,100 bytes; SHA-256 `92cdc34317270d26b79bb1b0b9d7c674f27c1417b284f90f9498b86931529fd4`. Full identity and scope are in [preview verification](preview-verification.md).

Independent sweep audit at 2026-10-14 03:25:45 UTC froze 354,325 completed / 67 failed / 40 timed out / 38,494 unattempted entries. Every completed output matched its recorded SHA-256, totaling 793,060,114 bytes with zero missing, empty or mismatched files. The active batch was excluded and the read transaction closed before hashing. This checks extraction integrity, not source recovery or semantic completeness.

Next focused synth correction: root assumes `src/SubtractiveSynth.cpp`, `tests/MidiTests.cpp` and the narrowly related `tests/RealtimeTests.cpp` checks after the engine/QA agents stopped at their usage limit. The current synth treats MIDI All Notes Off as immediate All Sound Off and repeatedly restarts release on pedal-up messages; Reset All Controllers does not clear its supported sustain controller. Root will reproduce these cases with generated offline audio and correct channel-local release behavior without adding new synthesis controls.

The new synth regression failed on the original implementation in 52.00 seconds: All Notes Off differed from individual note-offs in the complete rendered release window. The correction now separates immediate All Sound Off, ordinary All Notes Off, sustain transitions and resetting the supported sustain controller. Generated paired renders check full stereo buffers, channels 1/16, repeated messages, sustained/held notes, later note-offs and immediate silence; the prior fixed-voice panic check now correctly uses CC120. The runtime allocation fixture exercises all supported controllers, and the source callback guard still passes nine self-tests / seventeen owned bodies. The final Mac build passes all eight suites in **120.70 seconds** (AUv2 5.80 s, sends 36.00 s, editor 4.30 s, scanner 4.01 s, latency 15.09 s, UI 2.01 s, launcher 4.25 s and session 49.24 s). All paired controller-render buffers match sample-for-sample. This is Halftrack controller-contract coverage, not comparison with a reference instrument or physical MIDI keyboard.

The controller correction is committed as `a542c56`. Package `efedd6e` includes that source plus the factual late-pass analysis notes. Its executable/source/dependency identity checks pass; archive size 61,439,714 bytes and SHA-256 `bf078b50b3e41836e40343401bedb0f1cba3924dd5f93f7ae76158d58e70cd23`. This verifies the local build and included corresponding source. Its new CI run has not yet been published; the earlier media-history run is being allowed to finish.

The media-history correction `18565b6` now passes [CI 38506267371](https://github.com/jamescolasacco/halftrack/actions/runs/38506267371) on macOS, Windows and Linux, including clean builds, acceptance suites and packaging. The later synth correction remains separately covered by its local results until the next run completes.

The integrated synth/media checkpoint `b40c0e6` now passes [CI 38507449967](https://github.com/jamescolasacco/halftrack/actions/runs/38507449967) on all three platforms, including clean builds, tests and packaging. The local verified `efedd6e` archive contains the same application changes; the later checkpoint adds verification documentation only. No physical-device or whole-product parity claim follows.

Coverage-verification ownership: root owns a read-only verifier and synthetic fixtures in `scripts/verify_decompile_coverage.py` and `tests/test_verify_decompile_coverage.py`. The corrected analysis copy recovered 65 of 148 prior unsuccessful entries; a separate continuation covers the remaining inventory and sixteen later timeouts. Combining runs must validate their exact inventories and program identities, check every completed output hash, and count overlapping addresses once. The original coverage databases and artifacts remain unchanged; extraction coverage remains distinct from recovered behavior.

The new verifier passes eleven original synthetic cases; full tooling discovery runs **39 tests: 37 passed, two optional Ghidra integrations skipped**. Cases cover overlapping successes and failures, preserved inputs, mismatched inventories/programs, missing/empty/corrupt outputs, unsafe paths, frozen snapshots with a concurrent writer, unindexed work and create-only reports. Its first real audit checked **359,001 completed output files / 827,108,022 bytes** with no issue across the frozen baseline and retry. Deduplicated coverage is 359,001 completed, 66 failed, 36 timed out and 33,823 unattempted; 65 earlier unsuccessful addresses recovered. The remaining pass is excluded from this checkpoint until its own verification. Application source and its already verified Mac package are unchanged.


Verifier test-lifetime correction: the first new CI run failed on Windows in four fixture teardowns with WinError 32. The test writers used SQLite's transaction context, which commits but does not close the connection; Windows correctly refused to remove the still-open temporary database. Root now explicitly closes those four fixture connections after their transactions. The production verifier already closes its read-only connection in `finally` and is unchanged. All 39 local tooling tests run again with 37 passes and two optional skips; the corrected Windows run remains pending. No cleanup exception or behavioral assertion is suppressed.


The verifier follow-up `ab9cbfe` now passes [CI 38510438644](https://github.com/jamescolasacco/halftrack/actions/runs/38510438644) on all three platforms, including the corrected Windows fixture cleanup, all native acceptance suites and source-inclusive packaging. Linux completed at 04:29:38 UTC, Windows at 04:30:24 UTC and macOS at 04:30:38 UTC on 2026-10-14. Application code remains the already verified synth/media implementation.


The bounded ARM64 extraction pass and independent four-run audit are complete. All 392,926 declared entries were attempted; **392,846 completed outputs / 868,144,041 bytes** passed hash checks, leaving **66 failed and 14 timed out**, with no interrupted or unattempted entries. Address deduplication records 87 baseline failures/timeouts recovered. The late abort-metadata correction recovered all thirteen entries in the stalled cluster; the same thirty-second single-entry comparison changed from timeout to 159 ms with an unchanged modeled-body hash. The same follow-up, which also annotated five verified tail branches and one exception helper, recovered six earlier entries. Exact individual causation is asserted only for the controlled abort comparison. The stopped final model and both unchanged binary inputs have separate fingerprints. These are extraction results, not a percentage of Halftrack implemented or a complete semantic reconstruction. [Final research results](research/reverse-engineering-results.md#final-bounded-pass-verification-2026-10-14) retain the run denominators, remaining errors and verification-report hash `e2987129882e6dc39aac835ac7dcd6f2d4e7f329617a4a0828a8d14881f3583a`.


Pitch-wheel implementation ownership: root assumes `src/SubtractiveSynth.*`, `tests/MidiTests.cpp` and the focused allocation fixture in `tests/RealtimeTests.cpp` while the engine/QA agents remain stopped at the account limit. The next increment adds channel-local pitch wheel to the built-in instrument through its actual MIDI callback, with generated audio comparisons, controller-reset behavior and bounded processing checks. No reference-device sound match is claimed.


Pitch-wheel verification: the real-callback regression first failed in **46.39 seconds** when maximum wheel still rendered A4 instead of B4. The implementation now passes full stereo reference-note buffers at center/minimum/maximum, per-channel endpoint and half-down tuning, ordinary note-off identity, voice-state continuity, invalid inputs, sustain/controller reset, retained wheel after CC120/123, full host reset and finite high-frequency behavior. A complete 2,048-frame timed sequence matches at block sizes 1, 31, 128, 512 and 2,048, including a nonzero MIDI/destination offset and events rounded to a block end. Frequency checks separately cover already sounding held, pedal-held and releasing voices. All eight native Mac suites pass in **122.25 seconds** (AUv2 5.99 s, sends 36.16 s, editor 4.52 s, scanner 3.83 s, latency 15.16 s, UI 2.18 s, launcher 4.58 s and session 49.80 s). The expanded owned-DSP fixture records zero intercepted C++ new/delete calls; the source guard passes nine self-tests and eighteen owned bodies. These checks establish the stated Halftrack contract; external MIDI-device UI, expression lanes and paired reference-instrument measurements remain absent.


The pitch-wheel increment is committed and published as `6690295`. Its Mac package contains the same tested executable and exact own-source revision, with all bundled dependency/license and patched-file identity checks passing. Archive: `dist/Halftrack-preview-darwin-arm64-6690295.tar.gz`, 61,461,463 bytes, SHA-256 `ae32a30ba4134388d5fdd424f7a5eb54b85c485304f935f5b2aeacd2ee461552`. Cross-platform [CI 38513977347](https://github.com/jamescolasacco/halftrack/actions/runs/38513977347) is running; no remote pass is claimed yet.


Pitch-wheel source `6690295` now passes [CI 38513977347](https://github.com/jamescolasacco/halftrack/actions/runs/38513977347) on all three platforms, including clean builds, the original tooling fixtures, native acceptance and source-inclusive packaging. On 2026-10-14, macOS completed at 05:08:22 UTC, Windows at 05:08:28 UTC and Linux at 05:09:35 UTC. The verified local `6690295` archive contains that exact application source. This closes this increment's build/test/package checks; controller UI, physical hardware and whole-product equivalence remain separate work.


Continued component-analysis and oscillator ownership: root owns the isolated bundle inventory/device sweeps and residual main-program retry, plus `src/Session.*`, `src/SubtractiveSynth.*`, the narrow `SynthPanel`/handler tests in `src/MainComponent.cpp`, and related MIDI/persistence/realtime tests for sine/noise/pulse-width support. The existing specialist agents remain unavailable at their reported account limit, so root integrates these areas directly. All reference exports stay in ignored local artifacts. No original binary is modified or executed by the analysis scripts.


Oscillator implementation: root added original sine, deterministic per-voice white noise and adjustable zero-mean pulse to the built-in instrument. The native waveform selector and width control use validated model commands, the fixed settings FIFO, native undo and version-1 project persistence. Existing waveform IDs and missing-width square behavior remain compatible. The sine test first failed in 51.19 seconds; the finished implementation passes all eight Mac suites in **133.15 seconds**. Independent sine/noise sample fixtures, coherent pulse-harmonic/DC checks, all-mode block partitioning/reset/voice-state checks, invalid-command preservation, serialized-state sanitization and complete engine render/save/reopen/undo comparisons pass. Native handler tests cover all choices, width enablement, retained values and undo/redo. The 1100×720 generated piano screenshot was visually inspected; all eight controls fit. The source audit passes nine self-tests/eighteen bodies; the expanded runtime fixture detects zero intercepted C++ new/delete calls. Physical audio, reference sound matching, dual oscillators/modulation and whole-host deadline guarantees remain unverified or absent. Cross-platform CI and the new package have not yet been produced for this increment.


Oscillator release verification: source `ea248699ef915a4dc6300789f3a8de9905cdbed8` passes [CI 38518528688](https://github.com/jamescolasacco/halftrack/actions/runs/38518528688) on Linux (05:59:18 UTC), macOS (06:00:03 UTC) and Windows (06:01:55 UTC), 2026-10-14. The source-inclusive Mac archive `dist/Halftrack-preview-darwin-arm64-ea24869.tar.gz` is 61,487,037 bytes, SHA-256 `84ae468976c7a731132328beec607c88ead67e2fbf13ae9abb273020db1f0b72`. Its executable matches the tested build (24,506,248 bytes; SHA-256 `1ddbc44dfe2314275d42a264cd8e10bbac2c19d900a0001fd7b59bf71752a277`); own-source archive, both full dependency sources/notices, fourteen patched dependency files and excluded private paths all pass verification. Signing, notarization, physical hardware and whole-product equivalence remain unverified.

Additional-component audit: 51,057 regular files yielded 941 native occurrences/939 unique hashes, one explicitly unsupported legacy 32-bit candidate, 131 Java class header candidates and zero read errors. All seven separate audio-device components completed both architecture passes; three bounded retry entries recovered, producing **13,420/13,420 verified outputs** across fourteen slices. This is static extraction only. The [component report](research/additional-component-audit.md) records input/report fingerprints, the instrument wrapper call mapping, actual new Halftrack behavior, and remaining helpers/frameworks/scripts/archive coverage.

Main-program residual recovery: root owns the isolated diagnostic/tool-model experiments and research documentation; application interfaces are unchanged. The nine-run verifier checks 392,910 outputs / 871,680,755 bytes, retaining seven failures, nine timeouts and no unattempted/interrupted entries. Sixty-four entries recovered beyond the previous 392,846 checkpoint. Five narrow ARM instruction-model corrections passed 704 emulator cases and 128 original native processor checks before their outputs were credited. Exact switch/tail metadata, immutable copied projects, failed prototypes and input/report hashes remain recorded. No reference function was executed, and no extracted implementation entered public source. See the [residual report](research/residual-extraction-recovery.md).

Scanner component continuation: the frozen baseline produced 36,582 outputs. A 120-second retry recovered one; independently checked no-return and tail-call metadata in another verified project copy recovered eight from the original unresolved selection, including the prior recovery. Combined unique coverage is **36,590 / 36,599**, with seven failures and two timeouts. All 36,591 completed files across overlapping passes match their recorded hashes; the installed input and isolated slice are unchanged. The [component report](research/additional-component-audit.md#native-scanner-component) distinguishes this extraction work from Halftrack's existing tested scanner implementation. The Intel slice and remaining bundle components are not claimed complete.

Current ownership, channel utility continuation: specialist agents still report their account usage limit. Root owns the new UtilityProcessor/UtilityEditor files, the narrow Session/MainComponent integration, CMake/test registration, and DSP/model/UI/persistence/realtime acceptance tests. Existing agent areas are consumed directly under this recorded ownership; no unrelated refactor is planned. Remaining analysis failures will be investigated in separate ignored copies while the application feature is developed.


Channel utility local acceptance: the new original device, engine integration, editor and project support pass the focused Mac suite in 16.61 seconds. Its 320 matrix cases, bass/DC mathematical oracles, complete-file exports, MIDI/return insertion, device reordering and all fourteen settings' persistence pass. Root also repaired a mono-transition tail leak and added a regression for it and mute after DC filtering. Runtime heap/queue checks and the full suite are awaiting final rebuild; platform CI and package refresh are not yet verified. These controls are working code, not a claim of reconstructed proprietary Utility DSP.


Final channel utility local verification: all nine Mac CTest suites pass in **145.41 seconds**, including the new utility suite in **13.51 seconds** and the full instrument/mixer/project suite in 56.60 seconds. Runtime checks find zero intercepted C++ new/delete calls in Utility DSP and queue operations; 10,000 concurrent complete packets remain ordered and coherent. The source guard passes nine fixtures across 28 owned bodies. The separate Python run passes 39 tests with two optional integration tests skipped. The actual 640×390 editor snapshot was inspected. Physical mouse/audio checks remain unrun. Source and UI changes are ready for cross-platform CI; no complete-clone claim is made.


Root follow-up ownership: parameter-registration and processing traces now identify a third-order bass crossover and a two-pole DC section in the modern reference channel utility path. The next narrow DSP change implements the corresponding standard filter transfer functions independently, using new impulse and frequency oracles; UI/model/serialization are unchanged. The original 8176007 implementation is a valid working checkpoint, but its simpler filter topology is superseded by this evidence. Timing, filter-switch history and exact floating-point output remain unverified against paired reference renders.

The same reference trace identifies the modern Utility balance callback's center-normalized sine law. Root is replacing the provisional linear attenuation with a standard unity-center constant-power balance law and adding 41 independently calculated positions. This changes the noncenter gain behavior intentionally; exact sine-approximation rounding remains unverified.


Reference-informed Utility follow-up: all nine local Mac suites pass in **146.16 seconds**, including the Utility suite in **13.85 seconds**. Tests compare 41 balance positions, third-order high/low bass impulse samples and steady-state magnitude at 20/120/480/2000 Hz, mid preservation, and two-pole DC step behavior at 8/44.1/48/96 kHz. Existing full-buffer, project/editor/undo, real-time allocation and 10,000-packet tests also pass. Source guard: nine self-tests and 29 owned bodies. The changes use independently derived standard filters and trigonometric gains; proprietary arithmetic approximations, parameter scheduling and complete sample parity remain unverified.


Utility final platform/package verification: `90c9eb2` passes all jobs in CI 38532090898 (macOS, Windows and Linux). The refreshed source-inclusive Mac package passes archive/source/license checks and its own generated Utility suite from a fresh extraction. Exact sizes, hashes and validation boundaries are recorded in [preview verification](preview-verification.md#channel-utility-implementation-and-filter-refinement--2026-10-14). The complete DAW clone remains unfinished; the missing-feature list above still applies.

Final bounded analysis verification: the sixteen-run main audit validates **392,920 outputs / 873,848,226 bytes**, with four failures and two 300-second timeouts retained. This continuation recovers ten more entries beyond 392,910, including both oversized outputs and the invalid-rewrite case. The latest copied instruction model passes 1,024 emulator cases, with 320 new native conjunction cases supplementing the prior 128 native checks. The separate scanner audit validates **36,593 unique outputs** (36,594 files including an overlapping success), leaving four failures and two timeouts. Frozen source-project and original executable hashes remain unchanged. All bounded processes finished. These are verified analysis outputs; only the separately tested Utility changes are new application behavior. The [recovery](research/residual-extraction-recovery.md) and [component](research/additional-component-audit.md) reports retain unresolved entries and exact fingerprints.

Current effects ownership: specialist agents remain unavailable under the account limit. Root owns the four-device native effects implementation, its new DSP/editor/test files, and the narrow Session, main-window menu, test-registration and runtime-audit integration. The shared parameter packet and per-device public properties will be documented in architecture.md. Each device must have independent signal expectations plus project/undo validation; controls will not imply measured reference equivalence. Remaining decompiler experiments stay isolated and ignored.

Four-device continuation: EQ, compressor, stereo delay and room reverb now have working DSP, native controls, actual chain insertion, bypass/reorder/remove, all-property save/reopen and grouped undo. The initial focused suite passes in 15.90 s; all ten Mac suites pass in 164.32 s, including the runtime guards. Independent EQ, compressor, delay and reverb expectations are described in the effects contract. Editor inspection prompted readable numeric formatting and wider EQ sliders. The polished editor and explicit delay/reverb tail metadata pass the updated focused suite in 15.08 s; complete-file comparisons include one second after source audio ends. No reference-identical algorithm claim is made.

Note-tools ownership: root now owns the focused Session transformation facade, piano-roll menu/button and keyboard integration, and dedicated note-tool tests. Operations use existing stable note identities and the engine batch-edit transaction; no new undo subsystem or alternate model is introduced.

Note tools now implement eight operations in both arrangement and Session clips, with visible native menu access and quantize shortcuts. The dedicated known-result/selection/boundary/persistence/undo suite passes in 2.52 s. The existing production UI suite, including the new actual shortcut and stable-selection regression, passes in 2.17 s. Invalid complete selections and oversized time stretches preserve state and redo.

Delay synchronization ownership: root owns the small follow-up to the effect packet, delay DSP/editor metadata and Session comparison. Project tempo is derived runtime state carried in the FIFO; it is not a separately editable or serialized device tempo. Fixed prepared capacity will cover all documented synchronized divisions without audio-thread allocation.

Delay synchronization passes the extended native-effects suite in 19.73 s. Fifty-five independent impulse cases cover eleven rhythmic divisions at 20, 60, 123, 300 and 999 BPM; the slowest case exercises the complete 12-second prepared capacity. Real engine exports follow immediate tempo edits, saved projects derive the reopened tempo, and stale UI-derived tempo cannot override it or create undo history. Latest editor screenshots were inspected after numeric formatting and EQ control fixes.

Final combined local verification: source `6467066` passes all eleven Mac suites in **168.69 s**. The source callback guard passes nine fixtures over 42 owned bodies. Python runs 39 tests: 37 pass, with two optional integrations skipped. Fresh extraction of `Halftrack-preview-darwin-arm64-6467066.tar.gz` verifies binary identity, exact owned source revision, dependency source/notices and private-artifact exclusion; its effects and note-tools tests also pass. The original 1100×720 piano-roll layout and new EQ/delay editors were visually reviewed. This does not establish native input delivery, physical audio performance or numerical identity with the reference.

The bounded analysis continuation resolved one main-program memory-range failure by independently checking a non-returning stack-failure import and correcting copied analysis metadata. Two main entries still time out after 600 seconds, both before and after that correction. The scanner continuation recovered its memory-range failure and both timeouts; the combined tool/options/metadata result is retained without attributing every recovery to one change. The final main audit checks 21 runs, 392,921 files and 873,850,023 bytes; the scanner audit checks five runs, 36,596 unique outputs / 36,597 files and 90,386,722 bytes. Both retain three preparation failures. All bounded jobs finished. Twenty files across the frozen source projects and the installed main / isolated main / isolated scanner inputs retain their original hashes. Public Halftrack code remains independently authored; these extraction outputs are not bundled application code.

Combined platform verification: source `6467066` passes native builds, tests and source-inclusive packaging on macOS, Windows and Linux in CI 38594873180. The local package remains tied to that tested source. Documentation-only updates record the outcomes without changing the executable. Complete feature reconstruction and reference-identical audio remain unfinished.

Audio-editing continuation ownership: specialist-agent capacity is still unavailable under the account limit, so root owns the focused Session facade and snapshot fields, a new audio-clip editor, arrangement/slot menu integration, dedicated audio-edit tests, CMake/main test registration and associated documentation. Scope is native clip gain/fades, non-destructive unwarped arrangement trimming, split and duplicate, using existing engine state and undo. No parallel agent is editing these areas.

Audio-startup regression ownership: root also owns a narrow exact-source dependency patch in cmake/PatchClipGain.cmake. The generated Session playback test reproduced a unity-gain opening despite a saved -6 dB clip. The pinned stretch-reader constructor primes before the first processing callback sets gain. Initialize the reader gain before that constructor; retain existing per-block processing and add startup-level regression coverage.

Audio-editing focused verification: the dedicated suite passes in 10.83 s and production UI suite in 2.56 s. Every rendered sample is checked for gain/fade curves, source offsets, split, duplicate and manual overlaps; project/undo/invalid-state and source immutability checks also pass. The reproduced Session startup gain overshoot is fixed, and the stronger first-block/no-overshoot regression passes. The actual 620×480 editor image was inspected. Native input/physical listening and paired reference audio remain unverified.

Complete local audio-editing verification: all twelve Mac acceptance suites pass in **171.99 s**. The source callback guard passes nine fixtures over 42 owned bodies; Python runs 39 tests (37 passed, two optional integrations skipped). The exact-source gain patch also passes fresh application, repeat application and unknown-source rejection. Cross-platform CI and package verification for this source are pending.

Arrangement gesture follow-up ownership: root owns MainComponent canvas fade handles/envelope painting, audio split/duplicate keyboard routing and production-handler regression checks. The existing stable-ID Session methods and undo model remain the only mutation path.

Direct Arrangement follow-up: native fade handles, an actual envelope overlay and Cmd/Ctrl+E/D audio split/duplicate pass the production UI suite in **2.65 s**. The complete audio-editing render/state suite still passes in **10.84 s**. Tests cover one-step gesture undo, both clamps, separately selectable meeting handles, stale clip replacement and real text-editor focus isolation. The rendered canvas was inspected. Engine/DSP code is unchanged from the twelve-suite local checkpoint; all-platform verification follows publication.

Final audio-editing platform verification: source `0a6e7e108ac8522720545e741d394193f2d851ad` passes clean builds, tests and packaging on all three platforms in [CI 38629832444](https://github.com/jamescolasacco/halftrack/actions/runs/38629832444). Its Mac package passes fresh-extraction source/license identity checks across 89 tracked files, plus its own audio-editing and production UI tests. The exact artifact hashes are recorded in [preview verification](preview-verification.md#audio-clip-editing-and-timeline-gestures--2026-10-14). These working features increase documented partial coverage to 25/246 ledger rows (10.2%); this is not a finished-clone percentage. Complete feature parity, reference sound equivalence, physical driver/input checks and the previously recorded missing devices/workflows remain unfinished.

Arrangement editing continuation ownership: specialist-agent capacity remains unavailable, so root owns Session audio clipboard methods, native Arrangement keyboard/context actions and trim/slip gestures, their render/state/UI tests, and documentation. Clipboard media must be independently retained before Cut; Paste collects it into the destination project and uses fresh native IDs. No other agent is editing these areas.

Audio clipboard verification: native Cut/Copy/Paste now works across audio tracks and project switches, retaining gain, fades, source offset and duration with fresh IDs. The expanded audio suite passes in 16.89 s, including complete-file PCM comparisons, original-media deletion after copying, portable Save As/reopen, cut/save/undo, invalid-target/position rejection and unchanged redo. Production keyboard/focus checks pass in 2.82 s. Single-source scope, message-thread copy cost and native-input verification limits are documented; cross-platform validation follows the combined editing changes.

Direct trim/slip verification: the production UI suite passes in 2.97 s. Tests cover both edges, retaining fades at minimum length, source-start/end clamps, Shift+Alt slip, one-step undo/redo, grid/Alt behavior and replacement during trim/slip/body drags. A test assumption above the pinned engine's 300 BPM ceiling was corrected to its actual supported tempo; a boundary assertion tolerates only 1e-12 seconds of native fade-getter rounding. One unrelated existing native-focus test failed transiently before a successful complete run. The actual timeline image was inspected; physical input/listening remains unverified.

Cross-track movement ownership: root owns the stable-ID Session audio move method, Arrangement vertical body dragging, bounded native timeline validation, and render/state/handler regressions. This continues the same owned editing area; the underlying engine's 48-hour maximum is enforced before mutations instead of allowing its setters to silently clamp requested positions.

Cross-track move implementation is present: stable identity and source references survive reparenting, destination mixing applies through the normal engine graph, and one gesture groups both owner and position changes. Automated output, save/reopen, undo, invalid-target and boundary tests are added; verification is in progress.

Cross-track focused verification: the expanded audio suite passes in 14.75 s and production UI suite in 2.54 s. Full-file renders verify destination gain with retained source offset/envelope; state tests verify stable identity, one-step owner/time undo, reopen, same-target no-op, invalid target preservation, exact 48-hour boundary and rejected overflow. Production drag tests verify subsequent events continue editing the same clip after changing owners. The source callback audit passes nine fixtures over 42 owned bodies. Python reports 39 tests, 37 passed and two optional integrations skipped. Full combined native tests and platform builds follow.

Selection follow-up ownership: root is adding a production regression for trimming a clip start past another clip. Native sorting changes indices, so range gestures must restore selection by persistent identity after each successful edit, just as cross-track body gestures do. This prevents subsequent keyboard actions from targeting the clip that inherited the old index.

Combined local verification: source `3c098b0` passes all twelve Mac acceptance suites in **174.81 s**. The final selection correction passes the complete UI suite in **3.06 s**, including trimming through native sort order. No DSP or engine behavior changed in that correction. Source/package/platform checks will use the final correction revision; full product parity remains unfinished.

Final editing package checkpoint: `fc70170` passes fresh binary/source/license verification and its extracted app passes both audio-clip and UI suites. All 89 owned files and 5,969 dependency source files match their exact build inputs. Artifact hashes are recorded in preview verification. Cross-platform run 38682551893 is pending. Recorded partial coverage remains 25/246 (10.2%): these changes deepen existing editing rows and do not establish full clone completion.

Windows portability-fixture correction ownership: root owns a narrow test-only change after CI 38682551893 compiled Windows successfully but failed immediate deletion of old generated media. The pinned cache retains idle memory mappings until its two-second purge; Windows cannot unlink a mapped file. The fixture now pumps messages for at most six seconds while requiring actual deletion before Paste/render checks. No production behavior or assertion about clipboard independence is weakened. Other Windows suites, including UI, passed.

Portability-fixture local verification passes the complete audio suite in 14.84 s. In the earlier platform run, Linux completes build/test/package successfully and macOS completes its test step successfully; Windows' other ten suites pass. The only source change after `fc70170` is the test's bounded wait for actual obsolete-media deletion. The final platform rerun will validate that correction.

Locator continuation ownership: root owns the Session locator facade/native marker integration, ruler marker row and navigation/editor controls, dedicated model/render/UI regressions, test registration and documentation. Existing audio editing revision 03e4ea5 remains in platform verification while this next feature is developed. No other agent is editing these areas.


Published editing revision `03e4ea5` now passes native builds, all tests and source-inclusive packaging on macOS, Windows and Linux in [CI 38684029036](https://github.com/jamescolasacco/halftrack/actions/runs/38684029036). Its freshly extracted Mac package also passes audio-clip and UI suites; the Windows-only fixture correction retains actual source-deletion assertions.

Named locators now use the native marker track with stable identity, absolute timing, add/edit/delete, navigation, save/reopen and undo. The production ruler exposes creation, seeking, snapping/free drag and native editing; locator controls stay in the track header after resizing. Model tests reject malformed IDs/timing without replacing the project and compare every sample of a generated export, proving distant locators do not extend or alter it. Full combined validation is running. Root owns all changes in this feature; physical input and reference playback parity remain unverified.

Locator checkpoint verification: all **13 Mac acceptance suites pass in 188.75 s**. The production UI suite passes in 3.14 s and the locator model/render suite in 1.15 s, including malformed canonical-ID aliases and resizing at 1100/1440 pixels. The rendered 1440-pixel ruler was visually inspected. The source callback audit passes nine fixtures over 42 owned callback/helper bodies; dependency/runtime guarantees remain outside this source check. ARR-009 adds one partial ledger row: 26/246 (10.6% partial coverage, not whole-product completion). Cross-platform verification follows publication.

Audio reversal ownership: root owns a focused native reverse operation in Session, clip snapshot capability/state, the audio clip editor control and mirrored waveform display, with full-buffer render/undo/persistence/UI tests. No other agent is editing these areas. The native engine renders reversed media in its background cache; original project media remains immutable.

Reverse startup regression ownership: root also owns an exact-source dependency patch for AudioClipBase channel metadata. The 1.57 s generated export reproducer outputs all-zero PCM immediately after reversal; allowing the native cache to finish first produces the correct samples, while immediate redo fails again. The graph captures a zero-channel configuration from an unfinished reversed cache. The patch uses the immutable original source's channel metadata while that cache is pending; the existing graph still waits for actual generated playback media. No arbitrary delay belongs in production or the regression test.

Reversal focused verification: the complete audio suite passes in **19.89 s**, including immediate export/redo, gain/fades, duplicates, save/reopen, immutable media and a 65,537-frame odd-length source spanning worker blocks. Production UI tests pass in **3.15 s**; the actual audio editor image was inspected. The channel patch passes fresh/repeated application with exact output hashes and rejects unknown input without mutation. All diagnostic waits were removed. Full combined tests are running; this deepens existing WRP-001 evidence without adding a partial-coverage row.

Combined reversal checkpoint: all **13 Mac acceptance suites pass in 181.37 s**. The owned callback source guard passes nine fixtures over 42 bodies; Python runs 39 tests with 37 passed and two optional integrations skipped. The silence regression is fixed at graph channel-layout construction, and the original immediate-export reproduction now passes without a diagnostic wait. Cross-platform verification and final packaging remain separate checkpoints.

MIDI file continuation ownership: root owns Standard MIDI File import/export in the Session facade, file-menu/native chooser integration, dedicated file/render/undo tests and documentation. File parsing and validation finish before native document edits. The current reversal package remains a committed checkpoint while this feature is developed. No other agent is editing these areas.

Locator platform checkpoint: `e011458` passes builds, tests and source-inclusive packaging on macOS, Windows and Linux in [CI 38686006807](https://github.com/jamescolasacco/halftrack/actions/runs/38686006807). Reversal revision `135aea0` is published with its own Mac source-inclusive package; [CI 38687818629](https://github.com/jamescolasacco/halftrack/actions/runs/38687818629) is running.

MIDI interchange now creates separate editable native tracks/clips from source-track/channel pairs and exports current clip events atomically. Generated tests preserve event payloads, beat positions, channels and note release velocities through import/export/save/reopen; imported synth output matches independently drawn notes sample by sample. Import is one undo gesture, invalid files preserve redo, and the production File-menu completion path opens imported notes in the piano roll. The native chooser captures stable export identity and keeps its exact approved destination path. Full combined tests are running, with a final shared-timestamp setup-order check pending.

MIDI combined verification: all **14 Mac acceptance suites pass in 185.09 s**. The final file-order/limit additions pass the expanded MIDI suite in **3.28 s** and production UI suite in **3.29 s**. Tests cover equal-time patch/system setup before notes, 65-track and oversized-file rejection, preserved existing export destinations, editable imported notes and Session source export. The callback source audit and Python fixture suite pass with their existing documented scope. MID-012 raises recorded partial coverage to 27/246 (11.0%); this is not a whole-product completion score. Native chooser/listening checks and reference-exact MIDI/audio parity remain unverified.

Published MIDI/package checkpoint: source `d3581ce` is pushed. Its fresh Mac package passes executable identity, all 92 owned files and 5,969 dependency-source comparisons, then runs its own locator, audio-clip, MIDI-file and UI tests successfully. Artifact hashes are in preview verification. Combined [CI 38688435182](https://github.com/jamescolasacco/halftrack/actions/runs/38688435182) supersedes the canceled intermediate reversal run and is pending. No generated or proprietary analysis artifacts are included.

MIDI clip clipboard continuation ownership: root owns stable-ID Arrangement MIDI Cut/Copy/Paste/Duplicate in Session and the native canvas, with model/render/UI coverage. The complete d3581ce implementation/package remains in cross-platform verification while this next feature is developed. MIDI clipboard copies preserve beat duration across project tempo changes and regenerate native/note identities. No other agent is editing these areas.

Combined platform checkpoint: implementation `d3581ce` passes clean build, test and package steps on macOS, Windows and Linux in [CI 38688435182](https://github.com/jamescolasacco/halftrack/actions/runs/38688435182). This confirms the published locator, reversal and MIDI-file package; it precedes the new whole-MIDI-clip work.

Whole MIDI clip focused tests pass: MIDI model/render suite **5.37 s**, audio suite **21.55 s**, production UI **3.38 s**. The generated oracle compares every sample after a 120-to-180 BPM project switch; copied note/controller/SysEx content, fresh identities, one-step undo, save/reopen, invalid-target redo and shared audio/MIDI clipboard ownership pass. Native shortcut tests cover C/X/V/D and text-focus isolation. Root also corrected clip context deletion to resolve the captured stable ID instead of the current selection, with stale-menu regressions. The internal selection helper now names both Arrangement clip types. Full combined tests are running; no new partial ledger row is claimed.

Whole MIDI clip combined verification: all **14 Mac acceptance suites pass in 201.43 s**. The owned callback source audit passes with its previously documented scope. The new implementation is ready for publication; cross-platform checks and a refreshed source-inclusive package will follow the next committed editing checkpoint. The native desktop-input walkthrough remains unverified.

MIDI cross-track movement ownership: root owns the Session stable-ID move operation, native Arrangement vertical drag dispatch, model/render/UI regressions and their documentation. MIDI retains its embedded events and uses the destination track's existing instrument and mixer; the source instrument stays on its track. No other agent is editing these areas.

MIDI cross-track verification passes the expanded MIDI model/render suite in **9.22 s**, audio regression suite in **21.76 s** and production UI suite in **3.61 s**. Every rendered sample matches an independently prepared destination-instrument/mixer oracle; native event bytes, note/clip identity, one-step multi-owner undo, save/reopen, invalid-target redo, exact timeline bounds and stale-pointer protection pass. Native desktop delivery remains a separate manual check.

Track ordering ownership: root owns stable-ID native track reordering, Track-menu and track-header context commands, a dedicated model/render suite, selection/UI regressions and documentation. All views consume the same engine order. Track objects, clips, devices, send targets and Session slots must retain their identities. No other agent is editing these areas.

Track-control ownership follow-up: root also owns the narrow shared row synchronization change. Reordering must move existing UI rows by persistent track ID, rather than rebinding an active name editor or fader to whichever track takes its former array index. Arrangement and Session mixer rows use this same synchronization path.

Track ordering focused verification passes the new native model/render suite in **2.70 s** and production UI suite in **3.96 s**. Every source/destination position passes native undo/redo and retained object/event/slot/device/send checks. Reordered dry audio plus returns matches an independent arithmetic oracle before and after save/reopen. Actual menu handlers retain Arrangement/Session selection, preserve the same open rename editor, and reject a deleted context target. Full combined testing follows.

Combined MIDI/track-order checkpoint: all **15 Mac acceptance suites pass in 205.91 s**. The owned callback source guard passes nine fixtures over 42 callback/helper bodies. Python runs 39 tests with 37 passed and two optional integrations skipped. These original implementations are ready for a refreshed package and platform validation; whole-product parity and physical input/listening remain unfinished.

Track duplication ownership: root owns native ordinary audio/MIDI track copying, identity renewal, track-menu/header actions, dedicated device/render/persistence/undo tests and related documentation. Copies must retain their own clip/note/device/slot identities, preserve existing send destinations, and start without an assigned recording input or active Session launch. Return/group/frozen track duplication is excluded from this operation. No other agent is editing these areas.

Whole-track duplication focused verification passes in **5.84 s**. Every audio sample matches independent gain arithmetic through copied Utility, a generated VST3, mixer and shared return. Subsequent device edits, deleting the original, portable reopen and undo/redo preserve independence. MIDI synth output doubles as expected, then copied Arrangement/Session notes and synth parameters edit independently. The generated device fixture proves an armed/monitored source retains its capture assignment while the duplicate receives none, and copying during real Session ownership does not launch the copy. Production UI/track-order suites pass in 3.80/2.24 s. Fixture setup was corrected to normalize its plug-in path and address the actual native insertion order; production duplication behavior needed no change.

Combined duplication checkpoint: all **16 Mac acceptance suites pass in 197.31 s**. The callback source guard and Python fixture suite retain their prior passing results and scope (nine/42 source checks; 39 Python tests with two optional skips). Track duplication is a working, separately tested feature; return/group/frozen duplication remains excluded.

Transport tempo ownership: root owns a focused 20–999 BPM consistency correction in the Session facade, native tempo editor, exact-source Tracktion maximum-BPM patch, project tempo validation and generated timing/delay/UI regressions. Inspection confirms the facade accepts up to 999 while the UI and pinned native setter cap at 300. This is an integration range mismatch, not evidence of a dependency algorithm defect. No other agent is editing these areas.

Tempo precision regression: raising the native maximum exposed an existing remapping dead zone. Changing 300 to 300.01 BPM left a MIDI clip at 0.300000000000 seconds instead of retaining beat 1.5, producing start beat 1.500050000000. Pinned source inspection identifies a 1 ms aggregate position threshold. The exact-source tempo patch also lowers that guard to 1e-12 seconds so intended small beat-synchronized changes are applied within the original undo transaction; absolute clips remain excluded. The failing generated timing check remains in the suite.

Published shared-editing checkpoint `bc4a2ff` passes builds, all tests and packaging on macOS, Windows and Linux in [CI 38692788610](https://github.com/jamescolasacco/halftrack/actions/runs/38692788610). Its fresh Mac package verifies all 93 owned files and 5,969 dependency files, and its own MIDI, track-order and production UI suites pass. This precedes the separate track-duplication and tempo changes.

Tempo focused verification passes the new timing/model suite in **5.86 s**, full native-effects suite in **20.56 s** and production UI suite in **4.04 s**. Six tempo cases include exact note onset/render duration, one-step undo, 100 small edits without beat drift, fixed absolute locators and malformed-project redo preservation. High-tempo delay full-buffer comparisons and native invalid-text handling pass. Both dependency edits pass fresh/repeat/partial-upgrade and unknown-source no-mutation checks. Full combined testing follows.

Combined precise-tempo checkpoint: all **17 Mac acceptance suites pass in 215.05 s**. The owned callback source guard passes nine fixtures over 42 bodies; Python runs 39 tests with 37 passing and two optional integrations skipped. The tempo regression first failed above 300 BPM, then exposed and fixed the independent small-change beat drift. REC-003 now has bounded partial evidence, giving 28/246 (11.4%) recorded partial coverage; this is not whole-product completion. Publication, final source-inclusive packaging and platform checks follow.

MIDI capture continuation ownership: root owns typed audio/MIDI input selection, MIDI arm/monitor UI, native recording integration and note-identity finalization, generated virtual-MIDI/device tests and documentation. Only explicitly selected devices may be enabled. Tests will use a unique generated virtual input and the existing synthetic audio device, without microphone or physical-controller capture. No other agent is editing these areas.

MIDI capture regression ownership: root also owns the narrow pinned-source MIDI import correction exposed by the controller fixture: native recording preserves note-on velocity and controller bytes but drops note-off velocity. The dependency patch will retain matched release velocity in the existing native note state, with unchanged GPL notices. Root is also checking all facade recording exits and monitor/arm undo so recorded identities cannot be bypassed by seeking or input changes.

MIDI device-policy ownership: inspection found pinned native discovery opens physical MIDI inputs by default and enables devices on a deferred rescan. Root owns explicit host-policy hooks: generated test engines omit physical MIDI enumeration entirely; newly discovered production inputs start disabled; selecting a controller enables it synchronously before native routing. Selecting the native All MIDI Ins aggregate explicitly enables its physical inputs. Already saved enablement remains a user preference. Both source patches reject unknown dependency content before mutation.

MIDI capture focused verification passes the generated input suite in **8.32 s**, track-duplication regression in **6.13 s**, and production UI suite in **4.04 s**. Note timing/velocity, complete synth PCM, captured controller/release bytes, overdub identity, one-step take undo, arm/monitor undo, seek-held-note finalization, empty takes and reopen behavior pass. Fixture timing now respects the native queued-start reference block and monitoring graph activation. Actual defects fixed include redo invalidation by pending track sorting, non-undoable arm state, monitor mode not reapplied by undo, and dropped release velocity. Tests also exercise first-selection enablement of a disabled virtual input. Full combined testing is running.

Combined MIDI capture verification: all **18 Mac acceptance suites pass in 228.67 s**. The source callback guard passes nine fixtures over 42 owned bodies; Python runs 39 tests with 37 passing and two optional integrations skipped. Fresh/repeated combined dependency patching and every unknown-policy-source no-mutation case pass. REC-008 adds bounded partial evidence, taking recorded partial coverage to 29/246 (11.8%), not a finished-product percentage. Cross-platform checks, refreshed packaging and physical controller verification are separate.

Metronome continuation ownership: root owns native click/count-in settings in Session, transport controls and settings menu, generated click timing/recording/render/undo tests and documentation. The MIDI capture package `0ba8960` has verified executable/source identity (99 owned and 5,969 dependency files), and its freshly extracted app passes MIDI capture and production UI suites. Its 1100-pixel Session layout was visually inspected: input/monitor/arm controls fit the MIDI strip. CI 38700402521 passes build, all tests and packaging on macOS, Windows and Linux. No other agent is editing these areas.

Count-in precision regression: the generated impulse oracle passes ordinary playback at 120, 187.5 and 500 BPM, but the first negative-time count-in click appears at frame 11,999 instead of 12,000. Native ClickGenerator truncates the floating-point block-relative click position. Root owns an exact-source change to round that position to the nearest sample, retaining the existing scheduler and waveform renderer. The failing complete-buffer count-in check remains the regression.

Count-in diagnosis correction: changing the block-relative click offset did not fix frame 11,999, and that tentative change was removed. The actual source is the graph's shared `timeToSample`: it adds +0.5 even for negative values, so -108,000 frames becomes -107,999. The core time type already rounds negative values symmetrically. The new narrow graph patch uses that same sign-aware rounding; click rendering remains unchanged. Direct negative-conversion and full count-in impulse tests cover the corrected cause.


Immediate-arm/count-in regression: message-thread diagnostics show the intended punch range is initially correct. A pending native destination/arm updater subsequently stops that newly prepared recording and punches again at the pre-roll position. Root owns a small public native flush hook; Session drains those input updates and the resulting graph changes before starting the take. Generated audio and MIDI tests intentionally arm and record without an intervening message-loop delay. Temporary dependency logging is removed.


Full recorded-PCM regression: after flushing arm updates, count-in capture still accepted one input block at the old playhead position before the queued seek reached the graph. This set first-callback state too early and retained 224 extra frames at the one-bar boundary. Root owns a native wave-input first-callback guard that waits for the queued position. The complete-buffer oracle uses the following input reference block, matching the existing MIDI timestamp convention; physical latency calibration remains unverified. Temporary worker diagnostics are removed.


Metronome focused verification passes in **25.48 s**, generated MIDI-input regression in **13.12 s**, and production UI in **4.23 s**. Tests compare every click and recorded audio frame, exercise zero/one/two-bar capture from timeline zero and six seconds, MIDI notes across count-in from zero and four seconds, early cancellation, one-step take undo/redo, click persistence and click-free export. Three exact-source patch fresh/repeat/unknown-source checks pass. The actual 1100-pixel transport/Session image was inspected; Click and settings fit without overlap. Full 19-suite testing is running. REC-005 raises partial feature-area coverage to 30/246 (12.2%), not a clone completion percentage.


Combined metronome checkpoint: all **19 Mac acceptance suites pass in 258.88 s**. The owned callback guard passes nine fixtures over 42 bodies; Python runs 39 tests with 37 passes and two optional integrations skipped. Click/count-in and the three reproduced recording/timing fixes are ready for platform validation and a source-inclusive package. Physical input/output calibration and whole-product parity remain unfinished.


Recording quantization ownership: root owns project-level recording grid settings, native-input raw-capture policy, quantizing only newly recorded note starts within the take transaction, Edit-menu controls, generated event/render/persistence/undo tests and documentation. The metronome source `37581fe` is committed/pushed and its source-inclusive package is verified; cross-platform CI 38707016143 is running. No other agent is editing these areas.


Recording quantization is verified on the final source: all **19 Mac acceptance suites pass in 275.87 s**, including the expanded MIDI-input suite in **29.12 s**, metronome in **25.11 s** and production UI in **4.07 s**. All seven grids retain note duration/velocity and controller timing; the sixteenth-note full PCM render matches independently drawn notes. Existing overdub material, one-take undo/redo, off-grid start/end bounds, project persistence, invalid settings and audio-only loop capture pass. The callback source guard and Python fixture suite pass with their existing documented scope. This deepens REC-008; partial feature-area coverage remains 30/246 (12.2%).


Master output ownership: root owns the native master-volume facade and validation, the real vertical master fader beside the existing meters, independent full-mix render/undo/persistence tests, production UI checks and documentation. Recording quantization is committed as `ad3b142`; publication will follow the ongoing metronome platform run so that validation is not canceled. No other agent is editing these areas.


Master output focused verification passes its new generated full-mix suite in **7.78 s** and production UI suite in **4.05 s**. Every stereo frame follows independent gain arithmetic at silence, -24, -6, 0 and +6 dB, including return audio. Continuous-drag undo, redo, save/reopen, finite clamping, no-op history and malformed-load preservation pass. The actual 1100-pixel Session image was inspected: the new fader fits beside the master meters. Full combined verification follows the CI issue below; physical listening remains unverified.

Platform regression ownership: root owns the Mac CI monitoring-redo diagnosis and generated-device timing tests. CI 38707016143 passes Windows and Linux, but macOS times out the metronome suite at 90 seconds and fails monitoring redo. Its remaining tests pass; wall times are roughly three times local. The previously recorded local results remain valid but do not establish this revision's cross-platform success. No other agent is editing these areas.


CI diagnosis checkpoint: delayed-dispatch monitoring passes eight consecutive local runs at 7 ms and one at 20 ms per generated callback; a separate 1.2-second idle interval also preserves redo. The Mac CI failure is not yet reproduced or claimed fixed. Tests now distinguish redo-history, routing-state and output-signal failure, and a dedicated delayed-dispatch case runs in CI. The expanded metronome and MIDI suites have a 300-second timeout to accommodate the observed slower Mac runner while retaining every signal assertion.


Monitoring release regression: a deterministic held-note test failed before the fix: switching monitoring Off left the instrument sounding. Session now releases sustain and notes through the updated native graph when an input becomes inactive, including Auto disarm, reassignment and undo/redo; unaffected tracks are left alone. The expanded input and delayed-monitor suites pass in **33.37 s** and **12.81 s**, followed by a successful no-clips/no-scenes regression. The earlier full combined build passed all **21 Mac suites in 290.08 s** before this fix; final full verification is running separately. The original intermittent Mac CI symptom is not claimed resolved until the new platform run confirms it. No dependency patch or owned callback change was required.


Tap-tempo ownership: root owns a message-thread tap estimator in Session, an actual transport button, deterministic timing/undo/persistence/render tests, UI handler checks and documentation. Published `03316ad` includes recording quantization, master output and monitoring release. Its Mac package verifies 104 owned and 5,969 dependency files against the exact source/binary; CI 38712728155 is running. No other agent is editing these areas.


Published monitoring checkpoint `03316ad`: all **21 Mac acceptance suites pass in 300.98 s**. A fresh source-inclusive package verifies 104 owned files and all 5,969 dependency files; its extracted executable passes master-volume, monitoring and production UI suites. Cross-platform CI 38712728155 remains pending. Tap tempo then passes the expanded timing/render suite in **6.70 s** and production UI in **4.04 s**. The actual 1100-pixel Session image was inspected with the Tap button and master fader visible without overlap. Full tap integration checks follow; this deepens REC-003 without adding a new ledger row.


Clip activation ownership: root owns stable-ID audio/MIDI clip activation in Session, Arrangement/Session context actions and visual state, generated offline and launcher tests, native UI checks and documentation. Tap tempo is committed locally as `0082ab5`; its full local suite and the preceding published platform run are still executing. No other agent is editing these areas.


Playing-clip activation regression: deactivating a running audio slot retains its full previous 0.125 level despite model mute and a graph rebuild. The native wave reader transfers its already prepared stretch buffers when its state hash is unchanged. Root owns the narrow source patch adding mute to that reader hash; a mute transition must not reuse audio prepared at the opposite mute state. Original/patched source hashes are `b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76` / `a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2`. This adds no callback work. The failed running-slot signal test remains in the suite.

Playing MIDI activation regression: after the audio-reader correction passes, the generated held-note test exposes a second native issue. The replacement looping MIDI node inherits active notes but initializes its mute-transition flag from the already-muted model, so no note-offs are sent. The exact-source activation patch also checks the existing active-note list before skipping note release. It uses the existing per-clip source ID and note-off path, with a bounded 128-element check and no new allocation, lock or I/O. Original/patched hashes: `5bfeb512499f922cbe1acb464366c37a55faec511b4b5ce959b6fd4e8ce17a95` / `e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820`. This does not certify the native MIDI buffer implementation as allocation-free.

Arrangement MIDI uses the pinned seconds-based MIDI node, separate from Session's looping node. Its replacement lost both the previous mute state and MIDI source identity. Root extends the activation patch to preserve source identity and copy a lock-free atomic mute flag during preparation; processing resets the flag on reactivation and resynchronizes the held-note sequence. Tests now exercise all four audio/MIDI × Arrangement/Session paths, repeated transitions, redo while sounding and isolation from another instrument track.

The running redo test additionally shows that slot mute undo does not always request a native graph replacement. Session now compares surviving clip activation before/after undo or redo and explicitly restarts the graph only if activation changed, using the existing dispatch boundary. Unrelated fader history does not trigger this restart. Frozen-track activation is rejected rather than showing an ineffective control.


Published recording/master/monitor checkpoint `03316ad` passes clean build, all tests and source-inclusive packaging on macOS, Windows and Linux in [CI 38712728155](https://github.com/jamescolasacco/halftrack/actions/runs/38712728155). Tap-tempo commit `0082ab5` subsequently passes all 21 local Mac suites in 302.51 s. Actual inactive Session/Arrangement snapshots at 1100×720 were reviewed: OFF labels, selection and master controls fit. Native OS keyboard/listening checks remain unperformed.

Clip activation focused verification passes: running/render/persistence suite **12.29 s**, delayed MIDI monitoring **13.74 s**, production UI **4.27 s**. The exact-source fixtures pass fresh and repeated application, all four partial upgrades, each unknown-file rejection with no partial mutation, and the pristine startup-gain/activation chain. The original failing playing-audio and held-MIDI checks remain in the suite. Full combined validation follows; ARR-012/SES-001 remain partial and no additional whole-feature coverage is claimed.

Combined clip-activation checkpoint: all **22 Mac acceptance suites pass in 313.23 s**. The owned callback guard passes nine fixtures across 42 bodies; Python reports 39 tests with 37 passed and two optional skips. Tap tempo and clip activation are ready for the next source-inclusive package and platform build. Physical listening, controller/MPE chasing and whole-product parity remain outside this result.

MIDI note splitting ownership: root owns the stable-ID batch split command, piano-roll marker/shortcut/menu integration, native model/persistence/render/UI tests and documentation. Split retains the original left identity and all ordinary native note properties, creates distinct right identities and forms one transaction. Notes with expression children are rejected pending explicit envelope splitting. No other agent is editing these areas.


Published activation checkpoint `853f431` has a verified source-inclusive Mac package. Its freshly extracted executable passes activation, tempo and production UI tests; all 106 owned files and 5,969 dependency files match the exact build inputs. [CI 38718039217](https://github.com/jamescolasacco/halftrack/actions/runs/38718039217) is running on all three platforms. Note splitting is being tested separately in the following local source.

Note-split focused verification passes: model/native-property/persistence/full-render suite **3.12 s**, MIDI-file regressions **7.50 s**, production UI **4.63 s**. Both piano-roll contexts, all-versus-selected splitting, selection of right halves, one-step undo, text focus and preservation of note release velocity/colour/additional state pass. The audio oracle is independently drawn split notes, not the unsplit phrase. Running held-note transitions are being checked before the full combined run.

Running note-split tests pass in **3.94 s**, including the existing tools cases: the actual 48 kHz/128-frame graph continues through the marker, plays the right half, releases without a stuck note, and restores the original with undo in Arrangement and Session. Complete combined validation follows.

Combined MIDI splitting verification: all **22 Mac acceptance suites pass in 316.68 s**, including the inactive-note full-render oracle and reentrant export guard. A final UI-only selection assertion was strengthened to require both new right halves to move, with every original unchanged; production behavior is unchanged. The current feature remains a limited, tested note-splitting workflow; note deactivation, expression-envelope splitting and whole-product parity remain unfinished.

Final split-selection regression passes the production UI suite in **4.81 s**. Root reviewed the command, native transaction boundaries, marker/selection lifetime and test oracle. No owned DSP callback changed in this feature; the existing nine-fixture/42-body source guard result and its dependency limitations still apply.

Clip naming ownership: root owns stable-ID display-name edits for audio/MIDI clips in Arrangement and Session, original native rename dialogs/context/keyboard actions, undo/persistence and media-preservation tests. The native name property is edited directly through its UndoManager so external source-renaming preferences cannot rename media. No other agent is editing these areas.


Note-splitting package `d7329ce` verifies all 106 owned files and 5,969 dependency files; the fresh extracted app passes note-tools, activation and production UI tests. Clip naming builds and is now checking the real modal callbacks, preservation of generated source bytes, all clip contexts, no-op/history and portable Unicode names. No new DSP path is added.

Clip naming verification passes the expanded clip model/playback/persistence suite in **11.75 s**, actual modal UI handlers in **4.77 s**, and combined session/MIDI/export regressions in **61.27 s**. Both views and media types preserve source path/bytes and musical identity. The previous full 22-suite result applies to note splitting; these focused suites validate the subsequent display-name-only feature. Cross-platform verification and final combined packaging follow.

Session scene duplication ownership: root owns stable-ID scene copying into the following row, native clip/slot/note identity renewal, original context/Session-menu controls and generated playback/persistence/UI tests. Duplication preserves clip names, activation, looping and media references, leaves input assignments and current launch ownership alone, and never auto-launches the new scene. Recording/export and the existing 256-scene ceiling reject before mutation. No other agent is editing these areas.

Tap-tempo/activation source `853f431` now passes clean builds, tests and packaging on all three platforms in CI 38718039217. The separate note-split and clip-name commits are published together at `d48a802` and are under [CI 38720910857](https://github.com/jamescolasacco/halftrack/actions/runs/38720910857). The `d48a802` package verifies exact source/binary inputs and its fresh extracted naming, splitting, activation and UI suites pass. Scene duplication is subsequent work and is not included in that package.


Scene-copy focused playback verification passes in **5.47 s** after correcting the fixture setup: generated synchronous sources must register before clip activation can prepare a native reader. A retained pre-copy signal assertion distinguishes fixture failure from interruption by the copy. The new scene retains independent IDs/settings, the original keeps sounding, copied MIDI/audio play, undo/redo and portable reopen pass. The 256-scene and reentrant export cases are included in the subsequent combined run.


Scene-copy combined verification: all **22 Mac acceptance suites pass in 318.79 s**, including the 256-scene ceiling, complete row history and reentrant export rejection. Review then found a UI-only edge case with a selected return track: the new scene ID must be selected before the return's clip-selection guard. That correction has an explicit native interaction check and receives a final UI/launcher run before packaging. No owned audio callback or dependency patch changed.


Final scene-copy source passes production UI in **6.96 s** and launcher/model/native-audio checks in **5.28 s** (12.24 s together). The selected-return regression passes. All remaining behavior was covered by the 318.79 s full run; subsequent model edits only expanded formatting/comments. Ready for a source-inclusive package.


Scene ordering ownership: root owns stable-ID row movement through the native SceneList, original Session/context controls, current-selection preservation, generated playing/queued clip tests, history/persistence and export rejection. All track rows, including returns, move together. No other agent is editing these areas.


Scene ordering passes production UI **4.93 s**, launcher/native playback **5.46 s**, and session/export **61.33 s**, 71.73 s together. Tests retain playing and queued identities after both directions of movement, measure audio on each side of the pending beat boundary, and preserve every audio/MIDI/return row through history and reopening. Scene-copy package `ee92b48` verifies all source inputs and its freshly extracted launcher/UI suites pass.

The preceding published editing CI `38720910857` passes Windows/Linux but fails Mac's delayed MIDI-monitor test at `held input note is audible`. The earlier normal MIDI-input suite passes in the same job. Root is investigating this independently of scene ordering; no cross-platform success is claimed for `d48a802`.


Monitoring activation ownership: root investigates the delayed-dispatch Mac CI failure while the independently verified scene commits remain separate. A generated immediate-keypress regression will test events sent directly after changing monitoring, with no arbitrary settling sleeps. Candidate causes are an unfinished graph rebuild, stale queued releases or a stale route; existing audible/silent assertions stay intact.


Immediate-monitor regression fails deterministically in 1.01 s at zero output before the fix. Flushing the pending edit alone still fails (0.95 s). Inspection of the pinned GPL input node identifies a second necessary step: a fresh node registers as a MIDI consumer while its active-node pointer remains null until its first audio block, so the first key is discarded. Root owns a narrow exact-source preparation patch that initializes ownership only for newly allocated state; shared replacement state retains its existing owner. No work is added to the audio callback.


Fresh consumer ownership makes the immediate test pass in 17.23 s. A delayed-first-callback regression then fails at zero output in 1.82 s: the native stale-buffer guard interprets elapsed preparation time as an audio interruption. The final patch uses an unread sentinel for fresh state and skips that guard only on its first callback, retaining it for subsequent blocks. Pinned input SHA256 `f893b43a88aa1897164b1f6fe45516b4e5e7e5db435afd09292da34870f91b16`; final output `b417dbcd6f6dafba3078eaa408b0074d0cd6ba50e7e43f123d5a4f283b4af4a2`. The callback gains only a scalar condition; existing native mutexes/clock calls remain outside the owned-callback guarantee.


Input activation verification: generated count-in, MIDI recording/quantization and delayed monitoring pass in **77.72 s**. The final expanded Auto-arm/redo source passes monitoring **18.32 s**, production UI **4.84 s**, launcher **5.45 s** and session/export **61.40 s** (90.01 s together). Fresh/repeated/unknown-source patch fixtures pass, and the owned callback source guard passes nine fixtures across 42 bodies with its existing dependency limitations. The six pre-fix dispatch schedules did not reproduce the intermittent CI symptom; the two new first-key regressions did fail before their respective corrections. Cross-platform rerun remains necessary.


MIDI note activation ownership: root owns selected-note activation through native note mute state, snapshot/clipboard preservation, original piano-roll appearance and controls, strict saved-state validation, atomic history, generated render/runtime tests and UI checks. Whole-clip activation remains separate. No other agent is editing these areas.


Running note deactivation fails with the target and independent channels both retaining a 0.204093754292 peak. The pinned seconds-based MIDI node removes future note-offs when a note is muted but does not reconcile formerly sounding notes against its replacement sequence. Root is selecting the engine's supported beat-based MIDI sequencer for all owned MIDI clips; this is the existing Session path, with shared active-note state and reconciliation. Configuration must run before graph dispatch for new/imported/restored clips as well as undo/redo. This is a playback-policy change requiring the full timing/render regression suite, not a claimed reference-equivalence improvement.


The beat-sequencer policy passes focused clip activation **11.72 s**, tempo **6.45 s**, MIDI files **7.36 s** and note tools **7.30 s** (32.84 s together). The formerly stuck target voice now releases while the independent track stays audible, with no new DSP or native source patch. A same-clip held sibling test additionally compares the remaining voice against an independent channel. The actual inactive-note view at 1100×720 was inspected: slate notes, bright OFF labels and orange selection outlines remain legible. Full combined verification follows.

Published source `96465bd` has a verified Mac package containing 107 owned and 5,969 dependency files; its fresh extracted monitoring, scene and native UI suites pass. Cross-platform CI [38725624663](https://github.com/jamescolasacco/halftrack/actions/runs/38725624663) is running separately from the subsequent note-activation work.


Combined note activation and beat-sequencer verification: all **22 Mac acceptance suites pass in 327.67 s**. This includes same-clip/independent-track held voices, exact silent all-inactive renders, complete enabled-note render comparison, note clipboard/splitting, active-state persistence/rejection, controller recording, tempo, latency, Session ordering/copying and reentrant exports. The source callback guard result remains applicable: no owned DSP callback changed. This deepens the MIDI editor subset without increasing whole-feature parity coverage.


Input-gap ownership: root extends the first-key regression to an already-running generated device paused between callbacks, with a fresh key arriving after the gap and a separate old queued key. This probes the original Mac CI failure's post-warmup shape while retaining all existing assertions; it will establish whether the stale-input policy distinguishes old queued events from freshly received ones. No physical hardware is used.


The established-input gap regression fails in **2.76 s**, at the new key's zero output. The native guard clears the entire queue based on the previous callback age. The correction retains recent arrivals while filtering old queued input, with a preallocated receipt-time array and bounded per-event arithmetic. Exact-source fixtures cover pristine, repeated, published and partially patched inputs plus unknown cpp/header rejection before either write. Pinned/final cpp SHA256: `f893b43a88aa1897164b1f6fe45516b4e5e7e5db435afd09292da34870f91b16` → `a1660920bf4c22133e1a5c33264c478ee466cedfccca26edf4be79248aa81c2f`; header: `976acfb6df698e01935bdb3cfad7761da884e07b41875edb325b2ee7a10879a9` → `82f9fae59e49a0293f5aa0e0ba8926faa359df5d33fafc92226480083fb049e5`.

Published `96465bd` CI `38725624663` completes with Windows/Linux passing and Mac failing delayed monitoring at `record-arm redo restores Auto monitoring`; the other 21 Mac suites pass. This input-gap correction now has its own deterministic pre-fix failure, but cross-platform success remains unverified until the next run.


The input-gap correction passes generated count-in **27.57 s**, MIDI input/recording **33.76 s** and delayed monitoring **18.86 s** (80.20 s total), retaining the fresh-key and old-key silence assertions and existing Auto/history/routing checks. No owned DSP callback changed. The exact-source patch preflight/upgrade fixtures pass. Cross-platform validation is next.


Audio export ownership: root owns WAV/AIFF/FLAC selection through the shared renderer, original export dialog, supported depth validation, generated decoded-sample/cancellation tests and UI overwrite-destination protection. No other agent is editing these files. The output remains the Arrangement mix; Session performance capture is separate.


Lossless export validation: every supported WAV/AIFF/FLAC depth passes complete decoded-sample comparison, actual container headers, exact rate/channel/duration, silent tails, invalid format/depth/rate rejection, cancellation preserving existing files, reentrant protection and undo/redo. The dedicated suite passes in **9.69 s**. Native UI choices/normalized-name overwrite pass in **6.09 s**, latency in **16.44 s**, and Session/mixer/recording/project regressions in **61.42 s**. The initial test decoder incorrectly selected codecs by the intentionally generic fixture filename; decoding its stream retained all original assertions and verified the actual container. No production behavior was weakened to satisfy it.

Published `121cd89` has an exact verified source-inclusive Mac package; its fresh extracted MIDI monitoring suite passes. Cross-platform run [38728613399](https://github.com/jamescolasacco/halftrack/actions/runs/38728613399) remains in progress. The following full local suite will cover the combined note activation, input correction and new export formats.


Session follow-action ownership: root owns deterministic beat-timed audio/MIDI slot transitions, stable-ID model/UI controls, native undo/save validation, generated playback comparisons and current documentation. Stop, replay, previous/next/first/last and cyclic next are the current implementation scope; weighted/random choices and scene-level follow actions remain separate. No other agent is editing these areas.


Export checkpoint `a56265b` passes all **23 local Mac acceptance suites in 342.14 s**. Its source-inclusive package verifies 108 owned and 5,969 dependency files. This includes the note-activation playback policy, input-gap correction and all export formats before the following follow-action changes.


Initial follow-action tests pass native audio/MIDI transitions and model/history/persistence checks in **4.30 s**, plus production UI in **6.11 s**. Expanded running-history testing initially used an incorrect unity assumption for a fully panned channel: the pinned mixer raises its remaining channel by √2. Its measured error was 0.051776797 from a 0.125 source. The corrected independent arithmetic now checks that pan law before applying follow edits, retaining the strict output tolerances.

Published input correction `121cd89` passes Windows/Linux CI; Mac still fails delayed monitoring at `monitor undo restores both model and actual MIDI routing`. The new immediate-key/gap regressions pass there; the remaining intermittent history failure is not claimed fixed. Root will isolate that path without weakening its audible-output requirement.


The companion audio fixture requires its own collected-file reader registration; that is now established and measured before any follow edit. Running history then retains both channels correctly. Tempo timing is tested with held MIDI voices at 60/120/240 BPM: changing tempo on the short audio loop invokes stretching, so a constant-PCM oracle was not a valid isolation of follow timing. Audio transition/sample checks remain at their source tempo, with unchanged tolerances.


Final deterministic follow-action suite passes in **5.50 s**, including running edits/undo/redo, cancellation by Off, an independently audible companion track, fractional replay phase, sparse-slot navigation/end stops/cycling, bar-queued initial launch, 60/120/240 BPM MIDI voice transitions, one-step and branched history, scene copies, portable reload, invalid-state preservation and export rejection. Production UI passes **6.11 s**; launcher and Session regression suites pass **5.54 s** and **61.24 s** on this production source. No new DSP or dependency patch is needed. Weighted/random and scene-level follow behavior remain missing.


Monitoring diagnostic ownership: root adds generated-input observations that distinguish restored model state, native route enablement, device-instance delivery and actual audio after undo/redo. A slower 35-ms dispatch run of the published `121cd89` package still passes, so host delay alone has not reproduced the remaining Mac CI symptom. A graph-replacement-before-key case will test the gap between preparation and callback ownership without changing existing audible/silent requirements. No production fix is inferred from the diagnostic observations.


Test-only monitoring diagnostics and the replacement-graph first-key case pass in **19.19 s** locally. The slower 35-ms dispatch run of the published package also passes. The underlying CI-only history failure remains unresolved; the next run now distinguishes model restoration, input-route enablement, consumer delivery, instance replacement and output. No speculative production change is made from the unconfirmed graph-lifetime hypothesis.


Export mix-options ownership: root owns selectable stereo/mono output and peak normalization through a validated export-settings packet, shared renderer/UI, full-file generated references and cancellation/history regressions. Mono must retain both channels with (left + right) / 2; a writer that only selects the left channel is not sufficient. No other agent is editing these files.


Published `69e0252` passes all **24 local Mac acceptance suites in 344.04 s**. Its cross-platform diagnostic run is [38733354566](https://github.com/jamescolasacco/halftrack/actions/runs/38733354566); no outcome is inferred while it runs.

Mono export fails its full-file independent sum oracle in **1.61 s**, with worst error **0.0627172318**. Inspection confirms the pinned renderer maps only graph channel zero to a forced-mono writer. The narrow correction averages graph channels through its existing channel-map path and retains the earlier latency fixes. Pristine, prior-patch upgrade, repeated and unknown-source rejection fixtures pass; current patched source SHA256 is `57e58a225e0f433fb96920f03731ac37abe805205420e49e5c4ef7f0276186a4`. Signal/UI/latency verification follows.

The export regression now also checks exact cancellation of simultaneous equal/opposite stereo samples after mono downmix. The production dialog was captured and inspected with FLAC, mono and -3 dB normalization selected; all controls and destination buttons fit.

Published `69e0252` passed macOS, Windows and Linux CI including the generated MIDI diagnostics. The preceding intermittent Mac history failure did not recur in this run; a single successful diagnostic run does not isolate its cause, so those regressions remain in place.

Bitcrusher ownership: root owns the appended native effect kind, original quantization/sample-hold DSP, existing device editor/menu integration, parameter persistence and undo, generated signal/real-time tests and documentation. No other agent is editing these areas.

Export checkpoint `62e2fed` passes all **24 local Mac acceptance suites in 351.62 s**. Its source-inclusive package verifies 109 owned and 5,969 dependency files, including the exact mono/latency patch. The published three-platform CI run is pending.

Initial Bitcrusher signal, persistence and production editor tests pass in **25.39 s**, with UI acceptance in **6.20 s**. The source callback audit passes nine guard fixtures and 42 owned callback/helper bodies. An added odd-length source regression is checking that a held sample rings for the remainder of its interval before exact silence.

Final Bitcrusher native-effect suite passes in **24.44 s**, including exact odd-length hold-tail exports. Its initial combined effects/UI/Session run passed in **93.10 s**, covering zero-allocation processing at parameter extrema, persistence, one-step gestures, bypass/removal, MIDI/return inserts and editor lifetime. The actual five-control dark editor was visually inspected. Reference-specific filters/jitter/dither and numerical parity remain unimplemented or unmeasured.

Noise Gate ownership: root owns the appended native device, linked-stereo envelope/hysteresis/hold behavior, existing editor/menu and persistence paths, independent sample tests, callback guards and documentation. No other agent is editing these areas.

Noise Gate native effects, production UI and Session acceptance pass in **96.34 s** (28.68/6.28/61.36 s). Independent sample tables and closed-form four-rate envelopes, all native device lifecycle/project/editor cases, callback allocation extrema and the nine-fixture/42-body source guard pass. The actual eight-control editor was visually checked. No external sidechain, lookahead, reduction meter or reference-exact envelope is claimed.

Peak Limiter ownership: root owns the appended native device, fixed lookahead and bounded stereo peak detector, latency reporting, existing editor/model integration, independent envelope and compensated-export tests, callback guards and documentation. No other agent is editing these areas.

Initial Peak Limiter native effects, existing VST3 latency and Session acceptance pass in **106.85 s** (28.75/16.56/61.52 s), including the independent five-rate peak-window oracle and allocation guards. The three-control native editor was visually checked. Further regressions cover very short/resampled exports and parallel/return alignment; the full combined suite is running.

Combined production-source verification passes all **24 local Mac suites in 381.19 s**, including the expanded native-effect suite in **49.04 s**. Limiter compensation preserves 1/7/48/49/64/257-frame files, 44.1/48/96 kHz exports, two serial devices on a seven-frame clip, parallel dry/processed tracks, a delayed return and bypass. No test threshold was weakened.

The final driven-limiter host regression passes with the complete **49.37 s** native-effect suite. It exports actual 20 dB input drive into a -6 dB ceiling, compares every compensated sample and verifies bypass/removal history. The production DSP is unchanged from the preceding all-24-suite run; the final change expands its test coverage.

Device-menu ownership: root owns the production asynchronous insertion callback and its UI regression. The current callback captures a row index; the new test reorders tracks while a menu selection is pending and checks which actual track receives the effect.

The device-menu regression failed against the original row-index callback in **9.64 s**, inserting the chosen effect on the wrong track after reorder. The callback now captures a stable track ID and resolves it against a fresh snapshot, with missing-target rejection before mutation. Expanded tests cover insertion undo, Utility and deletion/project replacement.

The stable-track fix passes reorder/deletion but its new-project test fails in **7.55 s**: native track IDs can recur in a different edit. Root also owns the narrow Session snapshot generation field and successful New/Load increments needed to distinguish project instances. This token is transient, not project content or undo state.

The project-generation/device-menu fix passes production UI and Session acceptance in **71.38 s** (9.96/61.41 s). The final rebuilt UI suite passes in **6.99 s**. The original row-index regression and the separate new-project ID-reuse case both failed before their respective corrections. Reorder, deletion/replacement, identical-ID reload, Save As, rejected load, insertion undo and fresh post-load insertion now pass. No project schema change or extra undo entry is introduced.

MIDI monitoring diagnosis ownership: the MIDI agent owns `tests/MidiInputTests.cpp`, the narrowly scoped pinned MIDI-node patch, and diagnosis documentation. Root retains Session/UI ownership. Latest Mac CI fails a held input after a monitoring transition; delivery probes passed earlier transitions. A controlled replacement-node lifetime regression will establish whether an active-consumer pointer can outlive its node before any production correction. Native builds/tests are serialized with root.


Automation integration ownership: root owns MainComponent entry points, CMake/test dispatch, integration checks, architecture/manual documentation and publication. The automation engine agent owns Session.h/.cpp and AutomationTests.cpp; the automation UI agent owns AutomationEditor.h/.cpp and production pointer-event tests. The MIDI agent independently owns the monitoring regression/diagnosis. Native test processes and builds are serialized through root to protect shared test preferences.

Checkpoint `b3a2fcc` passes Linux and Windows CI, but macOS again fails the held-note monitoring case in run `38742088890`. All other 23 Mac suites pass. The monitoring cause remains under investigation; a green local run is not evidence that the intermittent CI failure is resolved.

Automation-editor ownership: UI agent owns new src/AutomationEditor.h/.cpp only, with a graphical volume/pan lane editor and event-driven interaction tests. Root owns MainComponent/CMake integration and final documentation; the engine agent owns the Session automation model. This editor uses stable track IDs and the project-generation token to reject stale gestures.

MIDI consumer lifetime: the new controlled native graph regression fails against the published patch in **3.14 s** because a retired node leaves its identity as the shared input queue owner. Clearing only the retiring owner and atomically claiming a vacancy fixes the established case. The expanded regression and full MIDI monitoring suite pass in **19.49 s** with unchanged thresholds/timeouts. Fresh/repeated/published-upgrade/partial patch fixtures pass, and unknown cpp/header rejection preserves both files. This is a demonstrated lost-key cause; it does not establish that every historical intermittent CI failure has the same cause.

The MIDI input/recording suite also passes in **33.91 s** after the lifetime correction. Native build/test ownership is released to root for automation integration; no concurrent acceptance executable was used.


Automation validation: the first run rejected malformed curve values but exposed missing validation of the custom track mixer type. The second run passed the editor in 0.69 s and reproduced single-point track-copy corruption after actual playback. The third run passes native automation (12.41 s), the final editor handlers (0.71 s), tempo (6.51 s), track duplication (6.15 s), and Session (61.40 s). Generated tests cover independent 44.1/48/96 kHz mixer plateaus, a bounded analytic volume ramp, tempo/tap history, retained manual levels, malformed XML/native parameter data, legacy migration, point spacing, and copied one-point envelopes. The initial main-window integration passed in 7.10 s; a later existing native keyboard-focus test failed before automation checks. Its retained diagnostic shows an enabled, visible editor but an inactive application/window, zero modal components and no focused component. Root is investigating launch lifecycle rather than weakening the assertion.


Automation lifecycle verification: the strengthened regression dispatches native timers and confirms the removed mixer has actually left the engine plugin cache before undo. Both directions of repeated deletion/history retain curve points and manual levels. The native automation suite passes in **12.09 s**. The same run still fails the separate pre-existing main-window keyboard-focus assertion; deferred startup and normal macOS application launch do not fix it, so those speculative launch changes were reverted.


Read-only desktop metadata identifies a visible foreground UserNotificationCenter panel at the macOS modal-panel window level. JUCE activation reads `NSApp.isActive`, consistent with the retained inactive-application diagnostic. No alert content was inspected or system panel dismissed. A rerun after normal panel dismissal is needed to establish causality; the focus assertion is retained and independent CI will exercise it.


Selected-range export ownership: engine owns the export settings/model and generated range/audio tests in Session.h/.cpp and AudioExportTests.cpp. UI owns the export dialog and interaction checks in MainComponent.cpp. Root owns shared docs, integration, serialized native verification and publication. The bounded default exports an exact selected interval with no extra tail, after rendering the earlier arrangement to preserve instrument/effect history. Whole-arrangement export keeps its existing tail options. No live project clipping or history edits are permitted.


Automation checkpoint `74e4825`: **25 of 26 local suites pass in 394.65 s**. Native automation takes 12.44 s and its separate graphical editor 0.71 s. All recording, monitoring, tempo, track-copy, note/audio editing, effects, export, plugin, latency, launcher and Session suites pass. Only production main-window acceptance fails at its unchanged keyboard-focus assertion; this prevents its later integration checks from running locally. [CI 38764420835](https://github.com/jamescolasacco/halftrack/actions/runs/38764420835) is running for the exact committed automation source.


Selected-range engine `a350e83` passes the expanded export suite in **44.82 s**, native automation in **14.67 s** and latency in **16.46 s**. All tested selected/full-render slices have zero sample error, including three rates, one-frame windows, one-second delay with limiter compensation, held MIDI and mono sources. Independent selected-only normalization differs by less than 3.2e-7; above-full-scale floating-point intermediates remain unclipped. Public cancellation/reentrancy and deterministic worker cancellation in both conversion phases preserve output/history. The focused export UI passes through real mono FLAC writing but exposed a test synchronization assumption at Save As: project generation is unchanged, the modal window has finished, and the nested destination callback is still queued after one event-loop pass. Its completion is being checked explicitly.


Automation CI `38764420835` passes Linux. Windows passes 24 of 25 suites, including the complete main-window UI, but the new cache-eviction fixture fails before Undo: one synthetic audio block does not guarantee a replacement graph has been installed. The engine agent owns the test correction. Pinned native source shows `freePlaybackContext()` clears the player graphs and retained nodes; the fixture now uses that supported teardown, observes actual cache eviction with bounded message dispatch, then allocates a playback context before Undo. The required destroyed-object/history assertion remains unchanged. Windows rerun is required; macOS CI is still running.


Range UI verification passes in **4.00 s** after replacing a fixed single event-loop pass with bounded observable callback completion. A second retained regression reproduced a production message issue: a queued initial range notification erased the stale-project explanation while safely blocking export. Ignoring unchanged mode notifications fixes that message without changing project guards. Validated settings survive the real asynchronous modal callbacks and corrected-extension overwrite, produce a decoded 2,400-frame mono normalized FLAC, and cannot export after New or project replacement. Save As remains valid. The actual 640×550 original dark dialog was visually inspected, with zero tail shown for explicit ranges.

## Master-volume automation ownership — 2026-10-15

Root assigns Session.h/.cpp and tests/AutomationTests.cpp plus tests/MasterVolumeTests.cpp as needed to the engine agent for master-volume envelopes, native history and persistence validation. The UI agent owns AutomationEditor.h/.cpp and MainComponent.cpp integration and handler tests. A separate agent reviews native master parameter identity, audio rendering and lifecycle without editing. Root owns shared architecture, progress, manual verification, build wiring and publishing. Native builds/tests remain serialized. This extends the current mixer automation behavior; master-pan controls are not part of this change.

## REA continuation and Mac MIDI verification — 2026-10-20

The owner supplied morluto/rea for analysis assistance. A research agent owns docs/research/rea-evaluation.md and isolated ignored tooling/evidence; it will review the official implementation before execution. Engine/UI resume the master-volume ownership above. Root owns diagnosis and any narrow tests/MidiInputTests.cpp correction for the completed CI run: Windows/Linux pass, while macOS fails only a held-note monitoring assertion. No cause or fix is claimed yet. Native builds and acceptance processes remain serialized.


Root continuation evidence (2026-10-20): the prior `3bffb08` Mac CI failure is limited to MIDI monitoring's held channel-1 output assertion after monitoring is enabled; all other 26 suites pass, including automation, export and main UI. The unchanged local executable passes five default-dispatch repetitions, four exact CI 7 ms repetitions (19.34/18.54/18.58/18.52 s), and one 20 ms slower-dispatch run. This does not establish a cause or repair. Root adds only generated-input delivery/routing/peak/timing diagnostics at the failing assertion, retaining its signal threshold and production code. Master automation validation and REA evaluation continue independently.


REA integration `795b537`: isolated REA 6.7.0 with verified Node 24.15.0, existing Ghidra 12.1.4/JDK 21.0.8, and no global agent changes. Its recovered original-fixture clamp compiles unchanged and passes 20,005 cases. Two authorized component entries were examined with matching LLVM evidence and unchanged input hashes; indirect DSP dispatch and parameter meaning remain unresolved. [Report](research/rea-evaluation.md) records exact provenance and the next bounded processing-boundary question. No feature coverage or sound-parity credit is assigned for analysis output.

Master UI `f04cfb3` passes editor acceptance in 0.92 s and main-window acceptance in 9.76 s. Its generated Master screenshot was visually inspected; native pointer/listening checks remain not run. Initial model renders pass dry-plus-return gain arithmetic, master silence/+6 dB at three sample rates, and every selected-range sample against the full-render slice. A hosted playback-state assertion is under diagnosis; subsequent independent persistence review adds explicit-pan retention and malformed-pan rejection regressions before final acceptance.

Root delegates the narrow monitoring diagnostic extension in tests/MidiInputTests.cpp and tests/SessionTests.cpp to the MIDI reviewer: measure the first generated callback entry and first-block peak while retaining the same twenty callbacks, threshold and routes. No production policy change is authorized by this finding alone; a deliberate aged-event reproduction would demonstrate the existing stale-event policy, not prove the earlier CI cause. Engine retains the native process slot and coordinates edit/build boundaries.


REA processing-boundary follow-up ownership: a research agent owns only docs/research/synth-processing-boundaries.md and ignored evidence. It reviews retained adapter dossiers and the existing synth/event tests to identify a supported implementation or missing behavioral regression; original synth source and native build/test execution remain unchanged until root coordinates a concrete finding.

Master automation diagnosis: hosted signal checks reproduced one-point curves reverting to manual gain. Native automation streams omit single-point lanes, and adding a sibling lane also invalidates the other parameter's stream. Synchronously updating both mixer streams and following both curves through supported message-thread APIs passes the new hosted signal checks. This adds no callback or timer path. Separate red tests then reproduced loss of an unowned explicit master-pan value during baseline migration; validation continues before the complete feature is marked verified.


Synth boundary regression ownership: root assigns tests/MidiTests.cpp to the processing-boundary agent for real plug-in callback checks at 44.1/48/96 kHz, mixed host block sizes, destination/event offsets and queued parameter changes at explicitly shared host boundaries. Existing independent signal fixtures remain the sound oracle; these added comparisons test adapter timing and state continuity. No reference DSP equivalence or unsupported mid-block parameter automation is implied. The agent does not edit production synth code or run native processes until the serial slot is granted.


Master engine `b0aa4b8` passes master-volume (8.67 s), automation (18.12 s) and tempo (6.52 s), all three in 33.31 s. Actual hosted stereo output verifies single-point gain/pan/master behavior and Read/Clear/Undo. Three-rate dry-plus-return arithmetic, complete selected-range comparisons, retained manual levels, native history and all 22 invalid master-state cases pass. The explicit-pan migration and NaN-pan rejection regressions each failed before their corrections. No sound-reference equivalence or physical walkthrough is inferred. The source guard passes nine fixtures over 42 owned bodies; Python tooling runs 39 tests with two optional integrations skipped.


Integrated master/REA checkpoint: all **27 local Mac acceptance suites pass in 422.51 s**. Automation takes 18.91 s, its editor 0.93 s, export 44.86 s, monitoring at CI pacing 19.18 s, main UI 9.58 s and Session/synth 57.74 s. The initial 36-scenario synth matrix passes at three rates and four starting waveforms; a final test-only change places a wheel event exactly on the first 128-frame boundary and will be rerun separately. Independent read-only model/UI review found no additional concrete defect. Root inspected the production Master editor screenshot. Native physical listening/input remains NOT RUN.

Monitoring diagnostic commit `ac8a2a7` changes tests only. Focused input and monitoring suites pass in 34.96/19.30 s before the integrated run; it preserves the original threshold and twenty callbacks while recording delivery, routing, first-callback entry/duration and first-block signal on failure. It does not establish or repair the historical intermittent Mac CI cause.


Final synth-boundary regression: the first wheel event now lands exactly at sample 128 (fractional arrival 127.75), exercising rounding at the first fixed-128 callback end at all three rates. The rebuilt final Session acceptance suite passes in **61.57 s**. This strengthens tests only; the production synth was unchanged and no new synthesis defect or reference equivalence is claimed. The full integrated run immediately preceding this one-line fixture change passed all 27 suites.


Published checkpoint `99e82c6`: refreshed macOS archive verifies all 115 committed original files and all 5,969 exact dependency inputs, executable identity and licenses. The extracted app passes automation (19.67 s), editor (0.92 s), MIDI monitoring (18.51 s) and Session/synth (57.79 s). Package hashes and limitations are in [preview verification](preview-verification.md). Clean macOS/Windows/Linux CI is running; no result is inferred while builds are active.


REA dispatch follow-up ownership: while clean CI builds run, a research agent owns docs/research/rea-dispatch-trace.md and ignored local evidence only. It may trace the already identified processing wrapper's indirect destination using cached constructor/table evidence and bounded REA queries. No production changes or new parity credit follow without a resolved target, original implementation and output tests. Root retains build/package/publication ownership.


REA dispatch follow-up resolves the two processing destinations on the factory-created, initialized path: adapter to wrapper, then wrapper to an inner graph entry. Constructor/table/pointer/rebase evidence and a new REA dossier agree; the inner entry still has unresolved computed dispatch. The [trace](research/rea-dispatch-trace.md) records conditional scope and the next event-schema/timing question. No production DSP or feature-completion credit is assigned for this static result.


Mac CI diagnosis ownership: root reassigns the automation engine agent to src/Session.cpp and tests/AutomationTests.cpp for the `99e82c6` hosted master playback assertion after reopening a played one-point project. All other 26 Mac suites pass, including the previously intermittent MIDI test, and Windows/Linux pass completely. Expected signal magnitude and maximum error are both about 0.015736568, consistent with a silent sampled interval; this is a hypothesis until diagnostics distinguish model/graph/read readiness. Preserve the signal oracle and establish the cause before any correction. Native builds/tests remain serialized through this agent until release.


REA event/timing follow-up ownership: the research agent owns a new docs/research/rea-event-timing.md and ignored evidence for the inner processing record path near 0xe818 and helper 0x26148. This is read-only analysis through the pinned local REA/LLVM tools; no app code, native build, test process or public raw decompiler output. A proven contract may motivate a later implementation check, but unresolved schema is not feature completion. Root retains shared documentation and publication.


Hosted-source readiness correction `24ad657`: diagnostics observed the collected source unmapped before the callback burst and native cache misses while master state was correctly -18 dB. On the local machine mapping recovered before the measured tail, so the earlier CI tail silence is consistent with that race but was not reproduced independently. The test now prepares the native context, waits up to five seconds for the actual source mapping and fails explicitly on timeout; the reloaded fixture evicts its reader once to exercise cold readiness. No production playback change, output retry, warm-up extension or tolerance relaxation. Independent review passed, as did the focused 31.49-second run. Native ownership returns to root for packaging/integration.


Root documentation integration ownership: the REA research agent finished its owned event/timing report in `cdc254a`. Root now adds the reviewed MIDI-identification evidence and report cross-links, and records the bounded findings here; no application/test changes.


The [REA control-record report](research/rea-event-timing.md) verifies an entry-time 56-byte control FIFO drain and a separate timestamped MIDI path, plus scalar setters that invoke the control router directly. Root checked the new dossier/manifest hashes and all 14 retained LLVM ranges/rebase-list hashes. The ring producer on that concrete path, receiver semantics, smoothing and actual host scheduling remain unresolved. No Halftrack production change or feature-completion credit is claimed from this static result.


MIDI note-off CI diagnosis ownership: after `65e1246`, the automation engine agent owns tests/MidiInputTests.cpp, tests/SessionTests.cpp and, if independently justified, the narrow native MIDI input patch/regression. The failing assertion is early `selectionAndMonitoring()` note-off release, before the retained held-note diagnostics. A separate agent reviews read-only. Native builds/tests are exclusively assigned to the engine agent until release; root retains documentation, publication and packaging. Preserve strict audible/silent assertions and establish a reproducer before changing timing or routing.


Conditional patch ownership extension: engine may edit cmake/PatchMidiInputActivation.cmake after a controlled stale-note-off reproduction establishes loss in the current native filter. Preserve exact-source hash gates and add both release-state and stale-note-on regressions. Root owns dependency/architecture/progress documentation updates. This does not pre-assign a cause to the separate historical fresh-note assertion.


MIDI release correction `6d6fa88`: a controlled 160 ms callback interruption reproduced a stuck sounding voice (peak 0.049129717 instead of below 1e-7). The native input node now retains aged non-attack channel state at the resumed block start, including note/pedal releases, while suppressing stale note attacks and system messages. Old retained state cannot establish the fresh-event timing origin. The complete monitoring suite passes in **27.22 s**, and MIDI input/recording passes in **34.71 s**, with unchanged output thresholds and existing release windows. Six audible release cases and direct native byte/order/timestamp/system-event checks pass. Independent read-only review found no concrete defect. This establishes a reproduced release-loss cause; it does not identify every historical CI failure.

Separate MIDI clock ownership: engine retains the native slot and cmake/PatchMidiInputActivation.cmake/tests/MidiInputTests.cpp ownership for a second reproduced defect. A generated fresh key was lost after observing the cached millisecond counter frozen for 130.249 ms, beyond the native 110 ms age window. A read-only call-graph review supports an accurate receipt clock only for current non-track, non-hosted external MIDI paths; track/hosted audio-derived paths and callback clock access must remain unchanged. Root owns the resulting scope/limitation documentation and publication. The subsequent focused verification is recorded below.


Guarded receipt correction `e2356e4` fixes the observed frozen-cache fresh-key loss. A construction-time flag excludes real track and hosted device types; only audited non-audio external/configuration ingress reads accurate milliseconds, and processing retains its cached clock. Final tests `5cb6991` verify both real excluded types, exact event delivery and once-only consumption without comparing a process-global clock that another thread can update. The actual frozen-cache fresh-key output oracle is retained. Monitoring passes in **27.81 s**; pristine/repeated upgrades through four published patch states and unknown-source no-mutation checks pass. Both independent source reviews found no production defect. The owned source guard passes nine fixtures across 42 bodies; Python tooling runs 39 tests with two optional integrations skipped. The complete native suite then passed **27/27 in 422.17 s**, including MIDI monitoring/input in **25.95/32.26 s**, main UI in **9.72 s** and Session/synth in **57.74 s**. New three-platform CI is running.


Integrated MIDI checkpoint `645e82c`: source-inclusive Mac packaging verifies the exact tested executable, all **117 owned source files**, **4,424 JUCE files** and **1,545 Tracktion files**, including current dependency patches and notices. The extracted app passes monitoring at CI pacing (**27.14 s**), automation (**17.28 s**) and production UI (**9.65 s**). No raw reference binary or analysis payload is included. Package fingerprints appear in [preview verification](preview-verification.md). [Clean CI 39379186381](https://github.com/jamescolasacco/halftrack/actions/runs/39379186381) remains in progress; physical controller/listening and complete reference parity are not inferred.


REA parameter follow-up ownership: a research agent owns docs/research/rea-parameter-dispatch.md and ignored local evidence only, for a bounded read-only trace of the previously unresolved parameter-forwarding branch at 0x19ff8. It may reuse pinned REA/LLVM and known component evidence; no application/test/dependency changes or native Halftrack execution. Root retains shared docs, package and CI ownership. A resolved dispatch edge is not working-device or sound-parity completion.


REA parameter integration: research commit `f842954` resolves the factory-path forwarding edge and one finite-input index-6 mapping. Root independently verified the unchanged binary hash/address mapping, five pointer/rebase rows, jump-table byte/target, seven cached-source hashes, all 12 bounded LLVM range hashes and the new dossier. Selected clamp/comparison/metadata instructions agree with the report. The control name, actual numeric destination identity and audible receiver behavior remain unresolved; this produces no application change or parity credit. Root now owns cross-links in the completed evaluation and control-timing reports; the original report remains the research agent's separate commit.


All-platform MIDI verification — 2026-10-20: [CI 39379186381](https://github.com/jamescolasacco/halftrack/actions/runs/39379186381) completes successfully for source `645e82c`, including build, native tests and packaging on macOS, Windows and Linux. Native totals are **27/27 (1228.88 s)**, **26/26 (416.52 s)** and **26/26 (350.52 s)** respectively. The previously failing Mac monitoring suite passes in **73.43 s**; its input/recording suite passes in **112.09 s**, automation in **40.93 s**, master volume in **22.43 s** and Session/synth in **163.26 s**. Python tooling runs 39 tests with two optional integrations skipped on each platform; owned source audits pass. The verified Mac package remains `Halftrack-preview-darwin-arm64-645e82c.tar.gz`. Full reference feature/sound parity, physical timing/listening and whole-host real-time certification remain incomplete or unverified.


## Extended native audio effects — 2026-10-20

Continuation ownership: this session acts as root and owns `src/EffectProcessors.*`, the effect tables/dispatch in `src/NativeEffects.*`, the generic editor/menu changes, `tests/ExtendedEffectsTests.cpp`, the chain-limit guard in `Session::addNativeEffect`/`addUtility`/`addPlugin` and the related documentation. No other agent is active.

Ten original devices append kinds 7–16: Saturator, Overdrive, Auto Filter, Auto Pan, Chorus, Phaser, Flanger, Three-band EQ, Bus Compressor and Frequency Shifter. The device menu now groups built-in effects by category. Each has independent oracles (closed-form curves/gains, analytic SVF magnitudes, Linkwitz–Riley all-pass recombination, exact delay/echo trains, quadrature sideband rejection) listed in [the effects contract](research/native-effects-contract.md#extended-device-set-2026-10-20), plus the shared lifecycle/project/export/bypass/history checks that now iterate every kind. The native-effects suite passes **1,418+ checks in 73.4 s**; production UI (**9.89 s**) and Session/MIDI/persistence/real-time allocation (**61.68 s**) also pass. The owned callback source guard passes nine fixtures across **109 bodies**.

Two defects were found and fixed while integrating. First, engine export pre-rolls the graph before time zero, so a free-running LFO started at a different phase in exports than in a direct render; modulation phase now locks to the transport at each block start (reproduced by the Auto Pan export comparison before the fix). Second, the engine's default 16-plugin track limit made `addNativeEffect` report success without inserting; tracks now allow 64 engine plugins and every insertion path rejects a full chain before creating an undo transaction (regression in the native-effects suite). No reference numerical equivalence, oversampling or sidechain routing is claimed.

## Native MIDI effects — 2026-10-20

Root owns `src/MidiEffects.*`, the MIDI kinds/plugin path in `src/NativeEffects.*`, the MIDI placement/move rules in `Session::addNativeEffect`/`movePlugin`, `tests/MidiEffectsTests.cpp`, the MIDI real-time fixture and documentation.

Seven original MIDI effects (Arpeggiator, Chord, Scale, Pitch, Velocity, Random, Note Length) now insert before a MIDI track's instrument and reuse the native packet, editor, undo, bypass and persistence path. The new `midi-effects-acceptance` suite passes **2,043 checks in 25.1 s**: complete event-list oracles for every device, a brute-force scale check over all roots/scales/notes, and engine renders through the built-in instrument that match explicitly written notes within 2e-6 for seven devices. Native effects (**68.1 s**), production UI (**9.6 s**) and Session/MIDI/persistence/real-time (**62.0 s**) pass; the real-time fixture floods every MIDI kind at parameter extremes with zero intercepted allocations. The owned source guard passes nine fixtures across **136 bodies**.

During integration two test-only lifetime errors were found and corrected (a reference bound to a destroyed snapshot and a range-for over `snapshot().tracks[i].plugins`, which C++20 does not lifetime-extend); one test expectation about Velocity's Gate mode was corrected to the documented span mapping. The UI suite's keyboard-focus assertion failed once and passed on immediate rerun, consistent with the previously documented application-activation sensitivity. Engine-owned MIDI buffer growth remains a dependency behaviour; reference timing/numerical equivalence is unmeasured.

## Native instruments — 2026-10-20

Root owns `src/Instruments.*`, the instrument kinds/render path in `src/NativeEffects.*`, `Session::setTrackInstrument`/`showInstrumentEditor`, the instrument snapshot field, `tests/InstrumentsTests.cpp`, the instrument real-time fixture and documentation.

Four original instruments (Poly Synth, FM Synth, Drum Kit, Wavetable) can replace a MIDI track's built-in synth in one undo step. The new `instruments-acceptance` suite passes **149 checks in 10.6 s**, including an engine export matching a closed-form sine sample for sample, FM sidebands matching Bessel J0/J1/J2, drum pitch/decay/choke behaviour, aliasing-free wavetable mip levels, bit-identical undo/redo, persistence and duplication. Native effects (**67.8 s**), MIDI effects (**23.0 s**), production UI (**9.6 s**) and Session/MIDI/persistence/real-time (**58.1 s**) pass; all instruments run allocation-free at parameter extremes. The owned source guard passes nine fixtures across **175 bodies**.

Integration found one real defect: the generic editor's fixed 34-control arrays overflowed for the 39-control Poly Synth (segfault reproduced by opening its editor). The arrays are now sized from the packet type, with a compile-time check that every table fits. The device-chain instrument chooser UI follows separately.

Instrument chooser UI: the MIDI track's instrument panel now has a production chooser (Halftrack Synth plus the four native instruments) and an Edit button for the full editor; native instruments show a compact read-out of their first twelve controls. The device chain draws MIDI effects before the instrument panel and audio devices after it, labelled "PROCESSING MIDI". Production handler tests cover the chooser list, FM selection hiding the built-in controls, the editor button and undo restoring the built-in synth with its previous settings; the UI suite passes. A captured layout check found the chooser hidden behind the Edit button because the panel only re-laid out on size changes; it now re-lays out on every sync. Physical OS input remains NOT RUN.

## Audio warping and stretch alignment — 2026-10-20

Root owns `Session::setAudioClipWarp`/`estimateClipTempo`, the warp snapshot fields, `tests/WarpTests.cpp`, `cmake/PatchStretchAlignment.cmake` (plus downstream acceptance in the clip-gain/activation patches) and documentation. Audio clips can now be warped to follow the project tempo (stretch or efficient Signalsmith modes, editable clip BPM). The first warped renders showed onsets up to 95 ms early at 60 BPM and 48 ms late at 240 BPM, plus 1.45 ms/beat drift at 80 BPM. Burst-centroid measurements across seven tempos fitted `outputLatency × (1 − stretch)` exactly, tracing the cause to the pinned readers priming at the default ratio, and the drift to per-chunk rounding. With the patch, `warp-acceptance` passes with every onset within about 1 ms at 60–240 BPM; launcher, follow-action, clip-activation, audio-clip, tempo, export, sends, latency and Session suites pass (10 suites, 211 s). No UI controls are exposed yet.

## Interface rework toward the reference layout — 2026-10-20

The owner asked for the interface to follow the reference DAW's layout more closely. Halftrack keeps its own name, logo and drawing (AGENTS.md); layout, density and workflow conventions are matched instead. Stage 1: neutral gray palette (41 blue-tinted literals converted to equal-luminance grays; saturated clip/accent colours unchanged); a single control bar with tap, tempo, meter read-out and metronome on the left, centred song position, transport and editable loop start/switch/length (bars.beats.sixteenths, applied through `setLoop`), and CPU, view selectors and audio setup on the right. The separate section bar is retired; its creation and zoom actions remain in the renamed Create/View menus and keyboard shortcuts. Production UI, export dialog and automation editor suites pass. Later stages cover the Session grid/mixer, an inline device view, a clip detail view and a category browser.

Stage 2 (Session view): one-pixel-gutter clip grid with solid track-colour title bars and clips, dark clip text and launch triangles, faint stop squares in empty slots and a per-track clip-stop row; a pinned Master column with scene launchers and Stop All Clips (right-click or double-click below the scenes inserts a scene). Global launch quantization and Back to Arrangement move to the control bar. Mixer strips follow the reference order: input and monitoring, pan knob (centre detent), vertical volume fader with a two-channel meter, and a numbered track activator (lit while audible), solo and arm. The theme draws its own knobs and faders. UI tests now use exported grid geometry (`slotCentre`, `launchPoint`, `trackStopPoint`) instead of hard-coded pixels; production UI, export dialog, automation and plug-in editor suites pass.

Stage 3 (device view): the bottom detail area is now an inline, horizontally scrolling device chain in signal-flow order (MIDI effects, instrument, audio devices). Each device is a panel with a title bar (on/off switch, name, window and remove buttons, right-click move/activate/delete) above its own controls. Native effects and instruments embed a new compact layout of the generic editor (three rows of small labelled knobs and menus inheriting the app theme); external plug-ins and Utility keep a window button. The native instrument's full control set now appears inline in the instrument slot. New production-handler checks edit an inline instrument control and an inline effect control, bypass a device with its switch, and undo the edits; UI, export dialog, automation, plug-in editor, native effects and instrument suites pass.

Stage 4 (browser): the browser now spans the window height beside the detail view and has a category sidebar (Instruments, Audio Effects grouped by Dynamics/EQ & Filters/Delay & Reverb/Distortion/Modulation/Utilities, MIDI Effects, Plug-ins, Samples) with a per-category search field. Double-click or Return inserts the item: devices go on the selected track, instruments and MIDI effects create a MIDI track when an audio track is selected, samples import as before. Production-handler checks cover category selection and contents, effect insertion, search filtering, instrument insertion creating a MIDI track and undo. A piano-roll ruler test now derives its expectation from the integer pixel a mouse event actually carries (the wider browser exposed a 0.25-pixel truncation). UI, export dialog and automation editor suites pass.

Stage 5 (Arrangement headers): 72-pixel tracks (previously 112) with a header strip in the track colour holding the name, numbered activator, solo and arm; routing (input/monitor or return send position) and volume/pan lines below; and a slim stereo meter at the right edge. Production UI, export dialog and automation editor suites pass.

Stage 6 (clip view): an open clip's editor now appears in the bottom detail area in place of the device view, with the Session grid or Arrangement still visible above, as in the reference workflow. It defaults to 58% of the main height (at least 340 pixels) and a divider drags between 25% and 85%. The Clip button and Shift+Tab toggle between the clip and device views; Tab still switches Session/Arrangement. A send-control helper in the UI suite now shows the device view before locating device-view controls, since inserting a MIDI track opens its new clip. UI, export dialog, automation and plug-in editor suites pass.

Integrated checkpoint after the interface stages: all **30 local Mac acceptance suites pass in 520.2 s** (new: native MIDI effects, instruments and warp suites), with the owned source guard covering 175 bodies. Not yet pushed or run on Windows/Linux CI in this session.

Audio clip view: double-clicking an audio clip (or opening the clip view on a selected audio clip or Session slot) shows an audio detail panel instead of the note editor: a Clip box (name, start, length, file), a Sample box (Warp switch, Stretch/Efficient mode, editable Seg. BPM with halve/double, clip-gain knob, Reverse when allowed) and the clip's waveform with beat lines when warped. Its controls use `setAudioClipWarp`, `setAudioClipSettings` and `setAudioClipReversed`. The earlier gain/fade/range dialog remains in the clip context menu. Production-handler checks cover opening by double-click, warping onto the project tempo at the estimated source tempo, typed tempo, gain and undo; UI, export dialog, audio clip and warp suites pass.

## Group tracks — 2026-10-20

Root owns the group commands and snapshot fields in `src/Session.*`, `tests/GroupTests.cpp` and documentation. Top-level audio/MIDI tracks can be grouped into engine submix folders with Halftrack's constant-power mixer, reported separately from the unchanged track list. The new `groups-acceptance` suite passes 70 checks covering summing, gain, pan law, mute, solo, history, persistence and validation; see [the groups contract](research/groups-contract.md). Group rows/columns in the interface follow.

Group interface: the Arrangement now uses a lane model (group header lanes above their members; folded members take no height) for every row calculation, replacing fixed `index × trackHeight` arithmetic. A group header has a disclosure triangle, editable name, group activator and solo, and group volume/pan; its lane summarizes member clips as coloured bars. Shift-click on track headers extends the track selection, Cmd/Ctrl+G groups it and Cmd/Ctrl+Shift+G or Create > Ungroup Tracks removes the group. UI checks cover selection, grouping, lane geometry, folding/unfolding and ungrouping; UI, export dialog, automation editor, audio clip, track order and duplication suites pass. Session-view group columns remain to do.


## Clip launch modes — 2026-10-20

Root owns the launch commands and snapshot fields in `src/Session.*`, `tests/LaunchModeTests.cpp`, the Launch box UI and documentation. Session clips now carry a launch mode (Trigger, Gate, Toggle, Repeat), a legato switch and a per-clip launch quantization (Global, None, 8 Bars to 1/32). `pressSlot`/`releaseSlot` apply them to launch buttons; see [the launch modes contract](research/launch-modes-contract.md).

Generated-signal tests found an upstream defect: a repeat loop point on a block start skipped that block. `cmake/PatchLaunchRepeat.cmake` fixes it (see [dependencies](dependencies.md)). The new `launch-modes-acceptance` suite passes. The launcher, follow-action, clip activation, warp, groups, track duplication, Session and UI suites pass locally on the Mac. The clip view shows a Launch box for Session audio and MIDI clips. Mouse down/up on a launch triangle and Enter down/up send press/release.

## Sampler instrument — 2026-10-20

Root owns the Sampler across `src/Instruments.*`, `src/NativeEffects.*`, `src/Session.*`, the instrument panel UI, `tests/SamplerTests.cpp` and documentation. A new native instrument plays one audio file chromatically: Classic/One-Shot modes, root/transpose/detune, start/end region, loop, filter, ADSR, voices, velocity, pan and volume. `setInstrumentSample` loads a file, switching the track to a Sampler in the same undo step if needed. Samples are collected into the project's Assets on save and resolved relative to the project on load. The audio thread receives decoded samples through an acknowledged atomic hand-off. See [the sampler contract](research/sampler-contract.md).

The new `sampler-acceptance` suite passes 65 checks. The instruments, native effects (editor coverage of every kind), MIDI effects, track duplication, Session (including allocation audits) and UI suites pass locally on the Mac, and the source guard inspects 180 bodies. The instrument panel shows a waveform with draggable Start/End markers, a Load button and a drop target. Choosing a browser sample for a MIDI track loads it into the Sampler.

## Session group columns — 2026-10-20

Root owns `launchGroupSlot`/`stopGroup` in `src/Session.*`, the Session grid column model and group strip in `src/MainComponent.cpp`, tests and documentation. In Session view a group now has its own column before its members: a disclosure triangle folds them, group slots launch the members' row and the stop button stops them. The Session mixer has a group strip. Adding undo checks found that group gain and pan were not undoable; they now record manual levels like track faders (see [the groups contract](research/groups-contract.md)). Launch modes, groups, launcher, follow actions, automation, master volume, sends, track order/duplication, Session and UI suites pass locally on the Mac.

## Session recording — 2026-10-20

Root owns `recordSlot` and the take lifecycle in `src/Session.*`, the record-slot UI, tests and documentation. Armed tracks now record MIDI or audio into empty Session slots on the launch grid; the take loops and plays in phase when it ends (see [the Session recording contract](research/session-recording-contract.md)). The MIDI input suite checks exact event offsets from the punch-in and the take lifecycle. The Session suite records generated audio input into a slot, and a UI check records from the grid. MIDI input, MIDI monitoring, Session, launcher, launch modes, follow actions, groups, clip activation and UI suites pass locally on the Mac. Assigning an input stopped playback; this is fixed under [input changes during playback](#input-changes-during-playback--2026-10-20).

## Drum Sampler — 2026-10-20

Root owns the Drum Sampler across `src/Instruments.*`, `src/NativeEffects.*`, `src/Session.*`, the pad grid UI, tests and documentation. A 16-pad kit plays one sample per pad on C1–D#2, with per-pad tune, length, level, pan and choke groups. `setDrumPadSample` loads and clears pads with undo, and pad samples are collected and resolved with the project. The settings packet grew to 128 values. Sampler, instruments, native effects, MIDI effects, track duplication, Session (allocation audits) and UI suites pass locally on the Mac, and the source guard inspects 183 bodies. See [the sampler contract](research/sampler-contract.md#drum-sampler).

## Device control automation — 2026-10-20

Root owns device automation across `src/NativeEffects.*`, `src/Session.*`, `src/AutomationEditor.*`, `src/NativeEffectEditor.cpp`, tests and documentation. Every native effect, MIDI effect and instrument control can now carry an Arrangement automation lane: `setDeviceAutomation` stores knob positions in an engine curve without touching the manual value, and the device reads it at block rate. The automation editor offers every control after Volume and Pan, in the control's units and choice names, and right-clicking a device control opens its lane. The new `device-automation-acceptance` suite renders a Sampler's automated level (constant, Read off, knob-scale ramp, undo, tempo change, reopen, clear, invalid-file rejection). Automation editor, automation, native effects, instruments, sampler and UI suites pass locally on the Mac, and the source guard inspects 184 bodies.

## Freeze and Flatten — 2026-10-20

Root owns freeze across `src/Session.*`, `src/NativeEffects.h`, `src/SubtractiveSynth.h`, the track menu and arrangement drawing, tests and documentation. Tracks can be frozen (rendered through their devices, then locked), unfrozen and flattened into project audio with undo. Native instruments now declare that they sound without audio input, which the engine's freeze render requires. The new `freeze-acceptance` suite passes, and the full 35-suite run passes locally on the Mac in 140 s. See [the freeze contract](research/freeze-contract.md).

## Stem export — 2026-10-20

Root owns `exportStems` in `src/Session.*`, the export window's Rendered track choice, `tests/StemExportTests.cpp` and documentation. Export Audio can now render All Individual Tracks: one file per track and group (without return effects), per return (with the sends it receives) and the Master mix. Existing files are replaced only after confirmation, and the project is left unchanged. The new `stems-acceptance` suite checks each stem against generated sources exactly. Export UI, audio export, sends, groups and UI suites pass locally on the Mac.

## Session mixer layout — 2026-10-20

UI-only change in `src/MainComponent.cpp`. The Session view now follows the reference layout more closely. The Master column (scene launchers, Stop All Clips) sits directly after the last track column and stays at the right edge only once tracks scroll. A Master strip below it carries the master volume fader and output meter. Every track strip has a send knob per return (labelled A, B, C), and the mixer grows by a row for every three returns. UI checks cover the Master position, the master strip editing the real master level, and a send knob editing the real send level; the UI suite passes locally on the Mac.

## Crossfader — 2026-10-20

Root owns the crossfader across `src/Session.*`, the Session mixer UI, `tests/CrossfadeTests.cpp` and documentation. Tracks can be assigned to side A or B, and a project crossfader on the master strip fades between them on a dipless law. The new `crossfade-acceptance` suite checks renders at centre, fully A, fully B and halfway against the law, plus undo, reopen and invalid-file rejection. The source guard (184 bodies) and the Session (allocation audits), sends, groups, master volume, automation, stems, audio export, utility and UI suites pass locally on the Mac.

## Insert Silence and Delete Time — 2026-10-20

Root owns `insertTime`/`deleteTime` in `src/Session.*`, the Edit menu commands and shortcuts, `tests/TimeEditTests.cpp` and documentation. The Arrangement can insert silence or delete time across all tracks, locators and automation, using the loop range as the time selection. The new `time-edit-acceptance` suite checks clip splitting, MIDI note handling, locators, automation, an exact render resuming the split source, undo and warped-clip rejection. The full 38-suite run passes locally on the Mac in 142 s.

## Follow action B and chance — 2026-10-20

Root owns the change in `src/Session.*`, the follow dialog, `tests/FollowActionTests.cpp`, a hash-gated engine patch and documentation. Session clips now carry action A and action B with a chance of A, matching the reference Launch box. Any and Other are added as actions. The dialog edits both actions and the percentage. Generated-signal tests check 0% and 100% chances, both outcomes at 50%, Other never replaying the same clip, No action continuing play (needing the engine patch), validation, persistence and corrupted-weight rejection.


## Track Delay — 2026-10-20

Root owns Track Delay across `src/Session.*`, the Session mixer strip, `tests/TrackDelayTests.cpp`, the suite registration and documentation. Tracks can be delayed or pre-delayed by up to a second. Positive delays use a preallocated ring buffer in a new owned device, which the existing `applyToBuffer` source-guard root covers; negative delays use the engine's delay compensation. The new `track-delay-acceptance` suite locates impulses to the sample. All 40 suites and the source guard (186 bodies) pass locally on the Mac, run directly because of the launch prompt noted above. See [the track delay contract](research/track-delay-contract.md).

## MIDI controller lanes — 2026-10-20

Root owns controller lanes across `src/Session.*`, the piano roll in `src/MainComponent.cpp`, `tests/MidiControllerTests.cpp`, the suite registration and documentation. MIDI clips can hold pitch bend, channel pressure and CC 0–119 lanes, edited in a lane under the notes that also shows and edits note velocities. Time edits, MIDI files, duplicates and reopening keep the lanes. The new `midi-controllers-acceptance` suite measures bent pitch to 0.05 Hz and the sustain pedal's hold and release, and the UI suite covers the lane. See [the controller lane contract](research/midi-controllers-contract.md).

## Input changes during playback — 2026-10-20

Engine owns the fix in `src/Session.*` and its test in `tests/MidiInputTests.cpp`. Cross-area edit: the Session-recording check in UI-owned `src/MainComponent.cpp` loses its "let it settle" pump, which no longer has a purpose.

Assigning an input, arming and changing monitoring now keep a playing transport and its launched Session clips running, as in the reference. The cause was in `setTrackInput`: enabling a disabled MIDI or audio input rebuilds the engine's device list. That clears the playback graph and stops the node play head, but the transport keeps its playing flag, so the transport timer then stopped playback. Arming and monitoring only rebuild the graph and did not stop it; the stop seen after arming in the UI test came from the earlier assignment. `setTrackInput` now rebuilds the graph and resumes the play head where it stopped.

A new generated-signal test plays a launched clip while it assigns, arms, monitors, disarms and disconnects a disabled controller and a disabled generated audio input. After each change it checks that the transport position advances, the slot still plays and the output is audible. Without the fix both device-enable steps fail with silent output. MIDI input (41 s, 593 checks), MIDI monitoring (13 s, 173), Session (62 s, 1190) and UI (10 s, 643) suites pass locally on the Mac. Device-list rebuilds from other causes, such as MIDI hot-plug or audio-device changes, still stop the transport. Physical devices are unmeasured.

## Session takes and device-list rebuilds — 2026-10-20

Engine owns the guard in `src/Session.*` and its test in `tests/MidiInputTests.cpp`. Cross-area edits for root: the new `cmake/PatchInputTakeRescan.cmake` and its `CMakeLists.txt` line, the downstream hashes accepted by `cmake/PatchTracktion.cmake` and `cmake/PatchMidiDevices.cmake`, and `docs/dependencies.md` and `docs/open-questions.md`.

Assigning a disabled input on any track during a Session slot take lost the take, which a generated-signal run reproduced. Enabling the device rebuilt every playback context's device list. The engine stops all input recordings with their recordings discarded while rebuilding. The slot then stayed marked as recording, and ending the take produced no clip. A MIDI device appearing during the take took the same path. The existing guards did not apply because a Session take never sets the transport recording. `setTrackInput` now refuses while any Session take records, before it enables a device. The refusal also covers the recording track's own input, which its take stops on. A new patch extends the MIDI-discovery deferral to recording inputs, so a device added during a take is discovered after it. Arming and monitoring other tracks stay allowed and keep the take. The policy is recorded in [open questions](open-questions.md#input-changes-during-session-takes).

The new test records a MIDI take and a generated audio take. During the MIDI take it tries to assign a disabled controller and the disabled audio input, and to disconnect the recording track. It also arms and monitors another track. During the audio take it tries to assign a disabled controller. A virtual MIDI device is added during each take. Both takes keep all their material: four keys, and 40,960 samples of the generated input. It failed in three configurations. Without any fix the first refusal check failed. With the guard alone the discovery check failed. With the patch alone the device-enablement check failed. The patch chain was checked on isolated pinned sources: fresh, repeated and unknown-input runs. MIDI input (43 s, 640 checks), MIDI monitoring (14 s, 173), Session (62 s, 1190) and UI (10 s, 643) suites pass locally on the Mac. Audio-device changes during any recording, physical MIDI hot-plug and the other Arrangement-only recording guards are unverified for Session takes.

## Arrangement time selection — 2026-10-20

Root owns the selection across `src/Session.*`, the Arrangement canvas and Edit menu in `src/MainComponent.cpp`, `tests/RangeEditTests.cpp`, the suite registration and documentation. Dragging over empty lane space selects time across tracks. Split, Duplicate (Cmd/Ctrl+D), Delete and Loop Selection act on it, and Insert Silence and Delete Time now use it in place of the loop range. MIDI clips can be split, with crossing notes divided and controller values carried. The new `range-edit-acceptance` suite compares renders sample by sample, and the UI suite covers selecting, duplicating and deleting. See [the selection contract](research/arrangement-selection-contract.md).

## Racks — 2026-10-20

Engine owns racks across `src/Session.*`, the new `src/Racks.*`, the rack kinds in `src/NativeEffects.*`, the choke path in `src/Instruments.*` and `tests/RackTests.cpp`. Cross-area edits: the UI's new `src/RackEditor.*` and the rack device view, device selection and grouping in `src/MainComponent.cpp`, Map to Macro in `src/NativeEffectEditor.cpp`, rack lookups in `src/AutomationEditor.cpp` and `src/UtilityEditor.cpp`, and root's `CMakeLists.txt`, `src/Main.cpp` and documentation.

Instrument, Audio Effect, MIDI Effect and Drum Racks hold parallel chains of built-in devices and Utilities with volume, pan, activator, solo, key, velocity and chain select zones, sixteen macros and, in Drum Racks, 128 pad chains with choke groups and in-rack returns. Devices can be grouped into racks and one-chain racks ungrouped. They replace the earlier Audio Effect Rack from main, reverted on the merged side with its author's agreement. The engine's `RackType` was not used, because its wiring has no key, velocity or selector zones, per-pad notes or chokes. Halftrack's own `RackEngine` runs the chains inside the rack device's callback, adopting new layouts without allocating or locking.

The new `racks-acceptance` suite renders generated signals against computed sums, zone and fade arithmetic, exact pad samples and choke ramps, checks undo, save/move/reopen and eight malformed files, and audits heap calls in the rack callback with its own engine, whose settings and temporary files stay in the run's folder so parallel runs from other checkouts cannot share them. The UI suite drives the rack editor, the Drum Rack pad grid and device grouping. The source guard covers the rack callback and the choke path. Racks (461 checks), UI (728), utility, freeze, instruments and sampler suites and the source guard (206 bodies) pass locally on the Mac.

Freeze and Flatten could leave a stray unnamed undo step, which the engine's undo timer opened after the freeze property changed. Flatten now opens its own transaction first, and Freeze holds an undo-transaction inhibitor while the render updates. See the [rack contract](research/racks-contract.md).

## Consolidate — 2026-10-20

Root owns `consolidateRange` in `src/Session.*`, Cmd/Ctrl+J and the Edit menu item, the consolidation block of `tests/RangeEditTests.cpp` and documentation. Consolidate replaces each selected track's material with one clip spanning the selection. MIDI clips join, and audio clips render (without devices) into a new file in Assets. The range-edit suite checks that a muted track with clip gain, a fade and a gap renders identically after consolidation, and that the MIDI join keeps every note and controller point.

## Time signature — 2026-10-20

Root owns the meter across `src/Session.*`, the control bar, rulers and grids in `src/MainComponent.cpp`, `tests/TimeSignatureTests.cpp`, the meter checks in `tests/MetronomeTests.cpp`, the suite registration and documentation. Projects can be in any meter from 1 to 99 over 1, 2, 4, 8 or 16, with beats kept as quarter notes. Launch quantisation, metronome accents, count-in, rulers, grids, clip creation, position fields and MIDI file export follow the meter. A 3/4 bar-quantized launch lands on sample 72000, and 3/4 and 5/4 clicks match exactly. See [the time signature contract](research/time-signature-contract.md).

## MIDI note tools — 2026-10-20

Root owns the note tools across `src/NoteTools.*` (new), `applyNoteTool` in `src/Session.*`, the piano roll's Transform and Generate menus and settings window in `src/MainComponent.cpp`, `tests/MidiToolTests.cpp`, the suite registration and documentation. Ten tools are available: Strum, Chop, Span, Recombine, Velocity Shaper, Arpeggiate, Ornament, Connect, Euclidean and Seed. The new `midi-tools-acceptance` suite compares every tool's output with hand-computed notes. See [the note tool contract](research/midi-tools-contract.md).

## Timeline navigation — 2026-10-20

UI change in `src/MainComponent.cpp`. A Follow switch (Cmd/Ctrl+F) keeps the playhead in view by paging the Arrangement and the piano roll, and turns off when you scroll by hand during playback. Z zooms the Arrangement to the time selection or selected clip, and X zooms back. The UI suite covers both. See [the navigation contract](research/timeline-navigation-contract.md).

## Clip scales — 2026-10-20

Root owns clip scales across `src/Session.*`, the Fit to Scale tool in `src/NoteTools.cpp`, the piano roll's scale controls and highlighting in `src/MainComponent.cpp`, tests in `tests/MidiToolTests.cpp` and the UI suite, and documentation. A MIDI clip can carry a root and one of sixteen scales. The piano roll lights its rows, and Fit to Scale moves notes to the nearest scale tone. See [the scale contract](research/scales-contract.md).

## Crash recovery — 2026-10-20

Root owns recovery across `src/Session.*`, `src/MainComponent.*`, `src/Main.cpp`, `tests/RecoveryTests.cpp`, the UI suite, the suite registration and documentation. Unsaved work is copied every 30 seconds to the application-data folder. After a crash, Halftrack offers to recover it as an untitled project, which renders identically to the lost session, and the saved project file is never overwritten. See [the recovery contract](research/recovery-contract.md).

## Device presets and default set — 2026-10-20

Root owns presets across `src/Session.*`, `src/MainComponent.*`, `src/Main.cpp`, `tests/PresetTests.cpp`, the UI suite, the suite registration and documentation. Halftrack's devices can be saved as named presets in the user library. Presets appear under their device in the browser and load in one undo step. Save as Default Set makes New start from the current project. See [the preset contract](research/presets-contract.md).

## Per-process test settings — 2026-10-20

Engine owns the storage change in `src/Session.cpp`. Cross-area edits for root: the `--self-test-settings-probe` child-process flag in `src/Main.cpp`, and the check in `tests/SessionTests.cpp`.

Test engines (`Session (false, …)`, with or without an injected synthetic device type) now keep Tracktion's `Settings.xml` and `Temporary` folder in a folder owned by the process: `Halftrack-test-settings-<uuid>` in JUCE's temporary directory. The folder is removed at exit. Before, every test process on the Mac, in every checkout, shared `~/Library/Halftrack Tests`. Its 2-second delayed saves overwrote each other, and settings carried over between runs. A snapshot on 2026-10-20 found a 484 KB file. It held 1,155 entries for virtual MIDI devices from earlier runs, leftover virtual devices that later runs loaded, a Hosted Device setup and a saved count-in mode. Its `Temporary` folder held 72 MB. Sharing that folder also crashed parallel runs at shutdown. Two crash reports from note-tools and follow-actions runs, after all their checks had passed, show `EXC_BAD_ACCESS` in `juce::File::getLastAccessTime` inside the `std::sort` of the engine's `TemporaryFileManager::cleanUp` during `~Engine`. The comparator reads each file's access time again on every comparison, so another process deleting or touching files mid-sort breaks the sort's ordering. With one `Temporary` folder per process, other processes no longer touch those files. The comparator is unchanged, so a file this process changes during its own cleanup could still break the sort; that has not been observed. The application keeps its normal settings.

The Session suite starts a second process that saves a one-input device setup through the engine. It then checks that the second process removed its folder, and that a new Session in the first process uses its own temporary folder and opens both generated inputs. A control build in which test engines shared one fixed folder failed the input check, and both folder checks. Test Sessions in one process still share the folder, so a setting one Session saves still reaches later Sessions in that process; this behavior is unchanged. What changes is that each test process starts from empty settings. Session (62 s, 1,196 checks), MIDI input (41 s, 640), MIDI monitoring (13 s, 173), recording (10 s, 34) and UI (16 s, 671) suites pass locally on the Mac. The full 38-suite run also passes, four suites at a time, in 179 s. Every settings file its engines opened was the process's own, apart from the scanner tests' private storages.

A first `ctest -j 4` attempt timed out on 19 suites. It was not caused by this change. After Halftrack processes are killed (every checkout's build shares the bundle ID), macOS shows a modal crash-recovery prompt that offers to reopen windows. It appears the first time a later process runs its event loop, and it blocks the suite. The rerun appended `-ApplePersistenceIgnoreState YES` to each ctest command, which suppresses the prompt. The settings probe's child process passes the flag too. The CMake test commands do not pass it yet.

## Automation in selection edits — 2026-10-20

Root owns the change in `src/Session.cpp`, `tests/RangeEditTests.cpp` and documentation. Duplicate Selection now copies the selected tracks' automation with their clips, and Delete Selection clears it inside the selection while keeping the values at the edges. The range-edit suite checks the exact point layouts and undo.

## Tempo automation — 2026-10-20

Root owns the tempo map across `src/Session.*`, the UI's time conversions in `src/MainComponent.cpp`, the Song Tempo lane in `src/AutomationEditor.cpp`, `tests/TempoMapTests.cpp`, the editor and UI suites, the suite registration and documentation. The tempo can now drop or ramp across the timeline, while clips, notes and automation keep their beats. Rulers, grids, snapping and position fields follow the map. The new `tempo-map-acceptance` suite checks rendered onsets through a drop and a ramp. See [the tempo map contract](research/tempo-map-contract.md).

## Exclusive solo and arm — 2026-10-20

UI change in `src/MainComponent.cpp`. Solo and Arm are now exclusive: a click releases the other tracks (and groups, for solo), and Cmd/Ctrl-click adds. Each click is one undo step. The UI suite covers both. See [the solo and arm contract](research/solo-arm-contract.md).

## Groove pool — 2026-10-20

Root owns grooves across:

- `src/Grooves.*` and `src/Session.*`, including the engine behaviour's playback hook and the groove watcher;
- the Groove section, Groove Pool window, Grooves browser and Extract Groove menu items in `src/MainComponent.cpp`;
- `tests/GrooveTests.cpp`, the UI suite, the suite registration and documentation.

MIDI clips can play with a groove from the project's pool, with Base, Quantize, Timing, Random, Velocity and Global Amount. The clip's notes are unchanged until Commit. Grooves come from Halftrack's generated swing and quantize library, from `.agr` files or from a clip's notes. Live playback follows groove edits and undo without stopping.

The piano-roll header now lays itself out to the editor's width, so the Scale controls stay visible beside the Groove section.

The new `grooves-acceptance` suite measures rendered onsets and accents. See [the groove contract](research/grooves-contract.md).

## Channel EQ — 2026-10-20

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. Channel EQ joins the native audio effects under EQ & Filters. It is the first device kind numbered after the racks; device loops now skip racks instead of assuming plain devices come first. See [the contract](research/native-effects-contract.md#channel-eq-2026-10-20).

## Tuner — 2026-10-20

Engine and UI change. It touches:

- `src/Tuner.*` (new) and the analysis tap in `src/NativeEffects.*`;
- `Session::tunerReading` in `src/Session.*`;
- the Tuner display in `src/NativeEffectEditor.cpp`, and the Utilities browser group in `src/MainComponent.cpp`;
- `tests/TunerTests.cpp`, the suite registration and documentation.

The Tuner is a Utilities device that passes audio unchanged and shows the nearest note, with cents or Hz and a target meter. The new `tuner-acceptance` suite checks detection accuracy and offline and live readings. See [the contract](research/native-effects-contract.md#tuner-2026-10-20).

## Edits during Session takes — 2026-10-20

Engine owns the guards in `src/Session.cpp` and the test in `tests/MidiInputTests.cpp`. Cross-area edits: `newProject` (project lifecycle, root) now ends Session takes through `stop()` before it replaces the edit, as `loadProject` already did. The other cross-area edits are in `docs/architecture.md` and `docs/open-questions.md`.

Session takes leave the transport out of recording, so edits guarded only by `TransportControl::isRecording` were audited against them. A new generated-signal test gives each group of edits a fresh project with a MIDI take from the generated controller. It plays a key before and after the edits and ends the take. On the unfixed code, run with failures logged rather than thrown, these edits lost the take:

- undo;
- removing the recording track;
- deleting its scene;
- audio or stem export;
- freeze;
- flattening a frozen recording track.

Creating a MIDI clip in the recording slot, or importing audio into it, was accepted, and the finished take then replaced that clip. After New Project during a take, input assignment stayed refused because the old take was never ended. On the unfixed code, these kept the take: reordering, duplicating, grouping and ungrouping tracks, clearing slots, MIDI import and saving. Removing other tracks and the other scene edits ran there only after the take was lost. Their code is unchanged, and they keep the take in the fixed run.

The fixes:

- Undo and redo, export, stems, freeze and flatten now also refuse during Session takes, through a shared `Impl::anyRecording()`.
- Removing the recording track, deleting a scene with a recording slot, and adding or importing a clip into that slot are refused for that track, scene or slot only.
- New Project ends the take.

With the fixes every check passes. The policy is in [open questions](open-questions.md#edits-during-session-takes) and the [Session recording contract](research/session-recording-contract.md).

MIDI input (55 s, 867 checks), MIDI monitoring (13 s, 173), Session (60 s, 1190) and UI (16 s, 668) suites pass locally on the Mac. The full 38-suite CTest run also passes (585 s, alongside another worktree's run). The build used a private copy of the pinned sources. Seeking, arming or monitoring the recording track itself, Arrangement clip and time edits, automation, follow actions and recording settings are not yet audited for Session takes.

## Remaining edits during Session takes — 2026-10-20

Engine owns the changes in `src/Session.cpp` and the test in `tests/MidiInputTests.cpp`. Cross-area edits: `docs/architecture.md` and `docs/open-questions.md`.

This finishes the audit of edits that were refused only while the transport recorded. The new generated-signal test, `playbackAndArrangementEditsDuringSessionTakes`, uses the earlier `SessionTake` fixture: a MIDI take from the generated controller with keys played around the edits. On the unfixed code, run with failures logged rather than thrown, two scenarios failed:

- A seek during the take moved the play head while the slot kept recording. Ending the take after one more key gave a 6.34-beat clip, and that key lasted 5.19 beats where it was held for about 0.21 beats.
- Disarming the recording track was accepted. The engine kept the take, but the slot recorded on a track that reported itself unarmed.

These kept the take on the unfixed code and need no change:

- monitoring the recording track, and replacing its instrument;
- Arrangement audio trim, reverse, split, duplicate, cut, paste and move;
- Arrangement MIDI duplicate, cut, paste and move on the recording track;
- Insert Time and Delete Time;
- track volume and pan, device and master automation;
- a follow action, the count-in and recording quantization.

The fixes:

- `seek`, and locator navigation through it, now ends any recording before moving the play head, through `Impl::anyRecording()`. The take keeps the keys played before the seek, and the transport stops, as during Arrangement recording.
- `setTrackArmed` refuses to disarm a track whose Session take records.

With the fixes every check passes. The policy is in [open questions](open-questions.md#edits-during-session-takes) and the [Session recording contract](research/session-recording-contract.md). Loop and tempo changes during a take are not yet audited, and reference behavior for seeking or disarming during a take is unmeasured.

MIDI input (61 s, 984 checks), MIDI monitoring (14 s, 173), Session (61 s, 1190) and UI (17 s, 671) suites pass locally on the Mac. The build used a private copy of the pinned sources.

## Audio-device changes during takes — 2026-10-20

Root owns the device-stop watch in `src/Session.cpp`, its scenarios in `tests/SessionTests.cpp`, the callback root in `tests/realtime_audit.py` and documentation. Measured first on the synthetic device: an input-channel change through the `AudioDeviceManager` and a same-layout driver restart each lost both Arrangement and Session slot takes. The transport stopped, Stop produced no clip, and slot takes still reported recording. Now any device stop during a take calls Stop before the engine's device rebuild. The take ends where the device stopped and is kept: 23,808 of 24,064 recorded frames in all four scenarios. A further take records on the rebuilt devices. The new test failed without the fix. Audio Settings is not disabled during recording (see [open questions](open-questions.md#audio-device-changes-during-takes)). The MIDI input (35.9 s), MIDI monitoring (25.8 s), UI (16.0 s) and Session (64.0 s) suites pass locally on the Mac, and the source guard inspects 185 bodies. Physical drivers and hot-unplug are untested.

## Automation capture and override — 2026-10-21

Root change. It touches:

- the override and capture machinery, `setAutomationArm`, `reEnableAutomation` and automation-only recording in `src/Session.*`;
- the control-bar buttons and live faders in `src/MainComponent.cpp`;
- `tests/AutomationCaptureTests.cpp`, updated checks in `tests/AutomationTests.cpp` and the UI suite, the suite registration and documentation.

Automated faders were locked until their points were cleared. Now moving one overrides its automation, as in Live, until Re-Enable Automation. With Automation Arm on, recording writes moves into the lanes. See [the contract](research/automation-capture-contract.md).

## Punch-In and Punch-Out — 2026-10-21

Root change. It touches:

- the punch switches, the take trimming in `stop()` and punch-aware capture in `src/Session.*`;
- the control-bar buttons in `src/MainComponent.cpp`;
- `tests/PunchTests.cpp`, the UI suite, the suite registration and documentation.

Arrangement recording can now be limited to the loop brace with separate Punch-In and Punch-Out switches, as in Live. See [the contract](research/punch-contract.md).

## Spectrum — 2026-10-21

Engine and UI change. It touches:

- `src/Spectrum.*` (new) and the stereo analysis tap in `src/NativeEffects.*`;
- `Session::spectrum` in `src/Session.*`;
- the display in `src/NativeEffectEditor.cpp`;
- `tests/SpectrumTests.cpp`, the suite registration and documentation.

The Spectrum is a Utilities analyser with Live's documented controls. See [the contract](research/native-effects-contract.md#spectrum-2026-10-21).

## Browser preview — 2026-10-21

Root change. It touches:

- the preview Edit, `previewFile`, `stopPreview` and `setPreviewVolume` in `src/Session.*`;
- the Preview switch and volume in the Samples browser in `src/MainComponent.cpp`;
- `tests/PreviewTests.cpp`, the UI suite, the suite registration and documentation.

Samples can be auditioned from the browser without touching the project. See [the contract](research/preview-contract.md).

## MIDI mapping — 2026-10-21

Root change. It touches:

- the MIDI remote consumer, mapping storage, learning and controller handling in `src/Session.*`;
- `tests/MidiMappingTests.cpp`, the suite registration and documentation.

MIDI controllers can be learned onto mixer and device controls and then drive them. The Map Mode user interface follows. See [the contract](research/midi-mapping-contract.md).

## MIDI Map Mode — 2026-10-21

UI change in `src/MainComponent.cpp` and `src/NativeEffectEditor.cpp`, with the UI suite and documentation. The MIDI switch (Cmd/Ctrl+M) highlights mappable controls in blue. Clicking one learns it, and the browser area lists the mappings with Min, Max and remove. See [the contract](research/midi-mapping-contract.md#map-mode).

## Pedal — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. Pedal joins the Distortion devices. See [the contract](research/native-effects-contract.md#pedal-2026-10-21).

## Beat Repeat — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h`, `tests/ExtendedEffectsTests.cpp` and the shared device test's render reference, with documentation. Beat Repeat joins the Delay & Reverb devices. See [the contract](research/native-effects-contract.md#beat-repeat-2026-10-21).

## Filter Delay — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. See [the contract](research/native-effects-contract.md#filter-delay-2026-10-21).

## Erosion — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. See [the contract](research/native-effects-contract.md#erosion-2026-10-21).

## Resonators — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. Resonators opens a Resonance category. See [the contract](research/native-effects-contract.md#resonators-2026-10-21).

## Vinyl Distortion — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. See [the contract](research/native-effects-contract.md#vinyl-distortion-2026-10-21).

## CC Control — 2026-10-21

Engine-area change in `src/MidiEffects.*`, `src/NativeEffects.cpp`, `src/Session.h` and `tests/MidiEffectsTests.cpp`, with documentation. See [the contract](research/midi-effects-contract.md#cc-control-2026-10-21).

## Align Delay — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. See [the contract](research/native-effects-contract.md#align-delay-2026-10-21).

## Dynamic Tube — 2026-10-21

Engine-area change in `src/EffectProcessors.*`, `src/NativeEffects.*`, `src/Session.h` and `tests/ExtendedEffectsTests.cpp`, with documentation. See [the contract](research/native-effects-contract.md#dynamic-tube-2026-10-21).

## Note Echo — 2026-10-21

Engine-area change in `src/MidiEffects.*`, `src/NativeEffects.cpp`, `src/NativeEffectEditor.cpp`, `src/Session.h`, `tests/MidiEffectsTests.cpp` and `tests/RealtimeTests.cpp`, with documentation. The real-time test now covers every MIDI effect kind, not only kinds 17–23. See [the contract](research/midi-effects-contract.md#note-echo-2026-10-21).

## MIDI Monitor — 2026-10-21

Engine and UI change in `src/MidiMonitor.*` (new), `src/NativeEffects.*`, `src/NativeEffectEditor.cpp`, `src/Session.*`, `src/Main.cpp`, `CMakeLists.txt` and `tests/MidiMonitorTests.cpp` (new), with documentation. A device without controls is now valid. Adding a device and validating its settings check the kind's range instead of treating an empty control list as unknown. The editor suite skips the control checks for such a device. The suite's flag is `--self-test-midi-monitor-device`, because `--self-test-midi-monitor` already runs the MIDI input-monitoring suite. See [the contract](research/midi-effects-contract.md#midi-monitor-2026-10-21).

## Transport and insert marker — 2026-10-21

Priority change: at the owner's request, core application work now comes before new stock devices.

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/TransportTests.cpp` (new), with documentation. The punch suite's automation check now uses the positions its moves were made at; it had failed about one run in six under load. See [the contract](research/transport-contract.md).

## Missing plug-in placeholders — 2026-10-21

Engine and UI change in `src/Session.*` and `src/MainComponent.cpp`, with the persistence suite and documentation. Opening a project no longer fails when an external plug-in cannot load. See [the contract](research/missing-plugins-contract.md).

## Missing media — 2026-10-21

Engine and UI change in `src/Session.*`, `src/NativeEffects.cpp`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/MissingMediaTests.cpp` (new), with documentation. Opening and saving no longer fail when audio files or samples are missing. See [the contract](research/missing-media-contract.md).

## Plug-in automation — 2026-10-21

Engine and UI change in `src/Session.*`, `src/AutomationEditor.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/PluginAutomationTests.cpp` (new), with documentation. See [the contract](research/plugin-automation-contract.md).

## Arrangement Automation Mode — 2026-10-21

UI change in `src/ArrangementAutomation.*` (new), `src/MainComponent.cpp` and `CMakeLists.txt`, with documentation. See [the contract](research/arrangement-automation-contract.md).

## Session performance capture — 2026-10-21

Engine change in `src/Session.cpp` and `tests/LauncherTests.cpp`, with documentation. See [the contract](research/performance-capture-contract.md).

## Capture MIDI — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/CaptureMidiTests.cpp` (new), with documentation. An input change can replace the engine's input instances, so the MIDI-mapping remote and the capture taps now re-attach after each one. See [the contract](research/capture-midi-contract.md).

## Warp markers — 2026-10-21

Engine change in `src/Session.*`, `tests/WarpTests.cpp`, `cmake/PatchWarpTiming.cmake` (new) and `CMakeLists.txt`, with documentation. A Tracktion patch fixes warp-marker timing. The clip-gain, clip-activation and stretch-alignment patches now accept its output hash. See [the contract](research/warp-contract.md#warp-markers-2026-10-21).

## Take lanes and comping — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/TakeLanesTests.cpp` (new), with documentation.

- **Recording.** A recording pass on an armed track now replaces the main lane's earlier material under it, as in the reference. Earlier recordings overlapped and both played.
- **A fix.** Comping and performance capture removed clips while iterating the track's live clip list; both now iterate a copy.

See [the contract](research/take-lanes-contract.md).

## Track output routing — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `cmake/PatchClipLauncher.cmake`, `cmake/PatchRenderSendsOnly.cmake` (new) and `tests/RoutingTests.cpp` (new), with documentation.

- **Audio To.** Tracks play into Master, Sends Only or another audio track, from the Session mixer.
- **An engine fix.** Renders summed Sends Only tracks into the mix, although live playback silenced them; they now match.
- **Loading.** A track that sends to a return may now also play into another track. Only returns are refused a track output.

See [the contract](research/routing-contract.md).

## CI timing fixes — 2026-10-21

Engine and test change in `src/Session.cpp`, `tests/CaptureMidiTests.cpp` and `tests/TransportTests.cpp`.

- **Capture MIDI** timed notes by the wall clock, so the phrase's rhythm depended on how fast the audio device ran; on Windows and Linux CI the synthetic device does not run in real time. Notes now keep the engine's stream-time stamps and map to beats as recorded MIDI does. With a real device this is the same timing, minus the message thread's delay.
- **Two tests** assumed playback starts on the first pumped block. They now pump until the play head reaches the position under test.

## Scene tempo and time signature — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/SceneTempoTests.cpp` (new), with documentation.

- **Scenes** can carry a tempo and a time signature, set from the scene menu and shown in the scene cell.
- **Launching** such a scene makes them the project's: at once when stopped, otherwise at its launch boundary, as its clips start.

See [the contract](research/scene-tempo-contract.md).

## Audio From another track — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `cmake/PatchClipLauncher.cmake`, `cmake/PatchRenderSendsOnly.cmake`, `cmake/PatchRenderTrackInputs.cmake` (new) and `tests/AudioFromTests.cpp` (new), with documentation.

- **Audio From.** An audio track can take another track's output as its input, from the input chooser's Tracks section. It is heard as the track monitors it, and it can be recorded.
- **An engine fix.** Renders left out the routed audio; they now include it while it is monitored.
- **Loops.** Track outputs and track inputs both refuse routes that would feed back.

See [the contract](research/audio-from-contract.md).

## Audio engine switch — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/AudioEngineTests.cpp` (new), with documentation.

- **Audio Engine On** in the File menu (Cmd+Option+Shift+E) closes and reopens the audio device. Turning it off keeps any take, and nothing plays until it is on again.

See [the contract](research/audio-engine-contract.md).

## Audition graph fix — 2026-10-21

Engine change in `src/Session.cpp`.

- **Auditioning** a take lane, or ending it, rebuilt the graph on the engine's next timer tick. On the slower Windows CI machine the first blocks after ending an audition could still play the auditioned lane. Both now flush pending clip-list updates and rebuild the graph before returning.

## Ledger re-score — 2026-10-21

Documentation only, in `docs/feature-parity.md` and `docs/progress-metric.md`.

- **Twelve stale rows** (REC-001, PLG-001/003/006/007/013/014/016, PRJ-001/003, UX-004, QA-001) read NOT IMPLEMENTED although existing tests exercised them. Each now cites its evidence and gaps.
- **Others stay unimplemented.** The same audit kept AUD-001/005/006/007/008/012, REC-006/007/011, PLG-005/008/009/010/015, PRJ-007/014, UX-010 and QA-002/004 unimplemented, since no test exercises them.

## Audio From taps — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `cmake/PatchRenderTrackInputs.cmake` and its accepting patches, and `tests/AudioFromTests.cpp`, with documentation.

- **Taps.** A track taking another track's audio chooses its Pre FX, Post FX or Post Mixer tap from the input chooser's submenu for that track. Pre FX and Post FX stay audible while the source is muted by solo, as in the reference.

## Simultaneous multitrack capture — 2026-10-21

Test change in `tests/MultitrackRecordingTests.cpp` (new), `src/Main.cpp` and `CMakeLists.txt`.

- **Two takes in one pass.** Recording one track from another track and a second from the generated input keeps two aligned takes in separate files, each holding only its own signal. One undo removes both. No behaviour needed changing.

## Session Record and overdub — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/SessionRecordTests.cpp` (new), with documentation.

- **Session Record** in the control bar records new clips into the selected scene on armed tracks.
- **Overdub.** Armed MIDI tracks whose slot already holds a clip play it and add the notes played where they fall in its loop. Pressing the button again plays the takes and ends overdubbing.

See [the contract](research/session-recording-contract.md#session-record-and-overdub).

## Note chance and velocity range — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `cmake/PatchMidiVariations.cmake` (new) and the patches it chains after, and `tests/NoteChanceTests.cpp` (new), with documentation.

- **Chance and range.** Notes can have a chance to play and a velocity range, set in the piano roll's Chance lane and by Cmd-dragging velocities. They vary over 64 repeating passes of the clip's loop.
- **Engine fixes.** The looping MIDI generator skipped variations, and stopped for good at an empty pass. Both are fixed in the patch.

See [the contract](research/note-chance-contract.md).

## Released-note and punch timing fixes — 2026-10-21

Engine and test change in `src/Session.cpp`, `tests/SessionRecordTests.cpp` and `tests/PunchTests.cpp`.

- **A released note was lost.** Pressing Session Record again, or capturing MIDI, right after a key was released could leave its note-off waiting for the message thread, so the overdubbed or captured note was dropped. Both now hand on pending notes first.
- **Two tests assumed timing.** The punch test placed its MIDI notes by a block count from the lagging snapshot position, and the Session Record test waited a fixed 400 blocks for a take. Both now wait until the state under test is reached. They failed on Linux CI.

## Re-Pitch warping — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `cmake/PatchRepitch.cmake` (new), `cmake/PatchReverseChannels.cmake` and `tests/RepitchTests.cpp` (new), with documentation.

- **Re-Pitch** in the clip view's warp mode chooser plays a clip faster or slower to follow the tempo, so its pitch moves with it.
- **An engine fix.** The engine gave every tempo-following clip a time stretcher, so Re-Pitch clips need a property that keeps it off.

See [the contract](research/repitch-contract.md).

## Verification suites — 2026-10-21

Test change in `tests/SharedMediaTests.cpp`, `tests/WavDecoderTests.cpp` and `tests/RenderAgreementTests.cpp` (all new), `src/Main.cpp` and `CMakeLists.txt`. No behaviour changed.

- **Shared media.** Editing copies of a clip, consolidating, and editing a project saved elsewhere rewrite no existing audio file.
- **Independent decoding.** Exports at 16, 24 and 32 bits parse correctly in a decoder written in the test, and match an analytic sine within 1.5 LSB.
- **Realtime and offline agreement.** Playback through the synthetic device matches the offline export exactly, apart from the 39-sample ramp that starting playback applies.

## Recording format — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/RecordingFormatTests.cpp` (new), with documentation.

- **File type and bit depth.** Audio takes record as WAV, AIFF or FLAC at a chosen bit depth, set in Audio & MIDI Setup.
- **A lost-take fix.** Recording into an unwritable project folder started and lost the take without a warning. It is now refused with the reason.
- **A teardown crash.** Destroying a Session whose clips the engine resolved on its way out read through `Session::impl` after it was cleared. The Edit's file callbacks now hold the Impl itself.

See [the contract](research/recording-format-contract.md).

## Device rate and buffer changes — 2026-10-21

Engine and test change in `src/Session.cpp`, `tests/SessionTests.cpp` (the synthetic device), `tests/DeviceSettingsTests.cpp` (new), `src/Main.cpp` and `CMakeLists.txt`.

- **Playback carries on.** Changing the audio device's sample rate or block size while playing used to stop playback. The engine restarts the device, rebuilds its playback context, and its transport timer stopped the transport. Halftrack now resumes playback from where it stopped, within a second of the restart, unless the user stops it or a take was recording.
- **The synthetic device** now offers 44.1, 48 and 96 kHz with 128, 256 or 512-sample blocks; it still opens at 48 kHz with 256-sample blocks.

## Browser collections — 2026-10-21

UI change in `src/Collections.*` (new), `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/CollectionsTests.cpp` (new), with documentation.

- **Collections.** The browser sidebar lists Favorites and six coloured collections above the categories. Keys 1 to 7 or a right-click tag the selected item, and a collection lists and loads its items. They are saved in the user library.
- **A narrow control bar.** In a narrow window the transport ran into the view buttons. The punch switches, loop fields, Follow, capture and automation buttons now give way in turn.

See [the contract](research/collections-contract.md).

## Plug-in presets — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/PluginPresetTests.cpp` (new), with documentation.

- **Presets for plug-ins.** Save Preset now works for plug-ins too. The preset keeps the plug-in and its state, is listed under the plug-in in the browser, and loads in one undo step.

See [the presets contract](research/presets-contract.md#plug-in-presets).

## Multi-clip editing — 2026-10-21

UI change in `src/MainComponent.cpp`, with documentation.

- **Several clips in the editor.** Shift-clicking Arrangement MIDI clips shows them together in the piano roll, the others' notes dimmed where they fall. Clicking a dimmed note edits its clip with that note selected, and Fit covers the shown clips.

See [the contract](research/multi-clip-editing-contract.md).

## Clip envelopes — 2026-10-21

Engine change in `src/Session.*`, `src/NativeEffects.*` (cross-area: native devices read modulated controls), `cmake/PatchClipEnvelopes.cmake` (new), `CMakeLists.txt`, `src/Main.cpp` and `tests/ClipEnvelopeTests.cpp` (new), with documentation.

- **Clip envelopes.** A clip can modulate its track's volume, pan, native device controls and plug-in parameters while it plays, and a Session clip can automate them. An envelope follows its clip or loops on its own length.
- **The editor** (UI, in `src/MainComponent.cpp`). An audio clip's Envelope chooser shows the envelope over the waveform. A MIDI clip's envelopes show in the piano roll's lane. Points are added, dragged and deleted with the mouse.
- **One-point automation under modulation.** Track automation of a single point held its value only until a clip modulated the same control; the audio thread then used the manual value. An engine patch keeps the held value.
- **A crash after undoing a track duplicate.** A plug-in kept for undo still read the deleted clip's curve. The engine now detaches it.
- **Test fixes.** On slow machines, the punch test now checks the MIDI note against where it was sent. Plug-in presets now load the scanned plug-in of the same identity, since Linux CI could not instantiate the stored location.

See [the contract](research/clip-envelopes-contract.md).

## Warp modes and transposition — 2026-10-21

Engine and UI change in `src/WarpStretchers.*` (new), `src/Session.*`, `src/MainComponent.cpp`, `cmake/PatchWarpModes.cmake` (new), `cmake/PatchStretchAlignment.cmake` (accepts the new hashes), `CMakeLists.txt`, `src/Main.cpp` and `tests/WarpModeTests.cpp` (new), with documentation.

- **Warp modes.** Audio clips warp in Beats, Tones, Texture and Complex Pro besides Re-Pitch and Complex, each with its own settings in the clip view.
- **Transpose and Detune.** A warped clip transposes by semitones and cents at its speed.

See [the contract](research/warp-modes-contract.md).

## Slice to New MIDI Track — 2026-10-21

Engine and UI change in `src/Session.*`, `src/WarpStretchers.*` (the transient detector shared with Beats warping), `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/SliceTests.cpp` (new), with documentation.

- **Slicing.** An audio clip's menu slices it to a new MIDI track at its transients, warp markers or a beat division. The new track's Drum Rack has a pad per slice, played back from a MIDI clip in the clip's place.

See [the contract](research/slicing-contract.md).

## MPE — 2026-10-21

Engine and UI change in `src/Session.*`, `src/Instruments.*` and `src/SubtractiveSynth.*` (RPN 0 bend sensitivity per channel), `src/MainComponent.cpp`, `cmake/PatchMpeTiming.cmake` (new), `cmake/PatchMidiReleaseVelocity.cmake` (accepts the new hash), `CMakeLists.txt`, `src/Main.cpp` and `tests/MpeTests.cpp` (new), with documentation.

- **Note expression.** Notes carry their own pitch bend, slide and pressure, drawn in the piano roll's Note Expression lanes or recorded from inputs switched to MPE.
- **MPE playback.** Clips with expression play each note on its own channel with a 48-semitone bend range, which the instruments now honour.
- **An engine fix.** Expressive notes in MIDI clips played at half their time at 120 BPM; they are now placed in beats.

See [the contract](research/mpe-contract.md).

## Input configuration — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/InputConfigTests.cpp` (new), with documentation.

- **Input Config.** Audio & MIDI Setup sets each pair of input channels as one stereo input or two mono inputs, and switches inputs on and off.

See [the contract](research/input-config-contract.md).

## Robustness suites — 2026-10-21

Test and engine change in `tests/GraphEditTests.cpp` and `tests/FuzzTests.cpp` (new), `src/Session.cpp` (load validation), `cmake/PatchClipEnvelopes.cmake`, `tests/PunchTests.cpp`, `src/Main.cpp` and `CMakeLists.txt`, with documentation.

- **Graph edits during playback.** 96 edits while clips play keep playback going and the output sound.
- **Malformed input.** 300 mutants of a saved project load and round-trip or are refused without changing the open project.
- **Two faults the fuzzing found:**
  - a clip envelope naming a missing device crashed the engine on load;
  - a mixer level outside the fader's range loaded, then saved a project that would not reopen.

  Both are refused on load now, and the engine skips such a curve.
- **The Linux punch test** lets new MIDI routing settle and retries a recording that stops in its first blocks, recorded as an open question.

See [the contract](research/robustness-contract.md).

## Stuck notes and large sessions — 2026-10-21

Test change in `tests/StuckNoteTests.cpp` and `tests/LargeSessionTests.cpp` (new), `src/Main.cpp` and `CMakeLists.txt`, with documentation.

- **Stuck notes.** A held note ends within its release whenever playback, its clip, its track or its instrument goes away. No stuck notes were found.
- **Large sessions.** A 50-track session with 768 clips and 12,288 notes saves, reopens and renders well inside the limits set.

See [the robustness contract](research/robustness-contract.md).

## Preferences — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `tests/PreferencesTests.cpp` (new) and `tests/GrooveTests.cpp`, with documentation.

- **Preferences.** A Preferences dialog sets exclusive arm and solo, the default warp mode, Auto-Warp Long Samples, and new Session clips' launch mode and grid. They are kept in a versioned file that survives older and damaged versions.
- **A test fix.** An AddressSanitizer pass over every suite found the groove suite reading a freed snapshot through a range-for over a temporary, and nothing else.

See [the contract](research/preferences-contract.md).

## Info View — 2026-10-21

UI change in `src/MainComponent.cpp`, with documentation, and a test fix in `tests/GraphEditTests.cpp`.

- **Info View.** A box under the browser says what the control under the pointer is and does, and when it is unavailable. View > Info or ? toggles it.
- **A test fix.** The graph edit suite failed on Linux and Windows CI because the reported position is refreshed by a 50 Hz timer and lagged there. It now reads the position after that timer has run with no blocks pumped, which is exact, and holds it to 20 ms.

See [the contract](research/info-view-contract.md).

## Linked-track editing — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/LinkedTrackTests.cpp` (new), with documentation.

- **Linking.** Link Tracks and Unlink Track(s) in a track header's menu link audio and MIDI tracks, and a header indicator shows and selects a link.
- **Linked edits.** Clip moves, trims, splits, deletions, activation, fades, time-selection edits, arming, take lanes and comping on a linked track apply to its linked tracks as one undo step. They are refused whole when a linked part cannot be done.
- **Auditions.** Clip edits now end a take audition before looking clips up, so they never act on an audition's temporary copies.

See [the contract](research/linked-tracks-contract.md).

## Browser indexing — 2026-10-21

Engine-independent change in `src/BrowserIndex.*` (new), `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/BrowserIndexTests.cpp` (new), with documentation.

- **Indexing.** The Samples browser indexes its folder and every folder in it on a worker thread.
- **Search.** Search looks through the index as it grows, and shows where each result is.
- **Problems.** Unreadable folders, truncation and moved results are reported.

See [the contract](research/browser-index-contract.md).

## Parameter identity and Configure — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/ParameterIdentityTests.cpp` (new), with documentation.

- **Configure.** A hosted plug-in's panel shows the parameters chosen in Configure as controls that can be automated and MIDI mapped.
- **Identity.** Hosted mappings and configured parameters are kept by the plug-in's own parameter IDs, so a reordered or vanished parameter is never driven by mistake. An identity suite covers reordering, duplication, deletion, undo, reopening and unavailable plug-ins.
- **A fix.** The identity suite found that edits made while a mapped controller moved joined its undo step. They are now steps of their own.

See [the contract](research/parameter-identity-contract.md).

## Computer MIDI keyboard — 2026-10-21

Engine and UI change in `src/Session.*`, `src/ComputerKeyboard.h` (new), `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/ComputerKeyboardTests.cpp` (new), with documentation.

- **Playing.** M or the KEYS switch turns on the computer MIDI keyboard. A to ' play notes into a Computer Keyboard input, and Z X C V set the octave and velocity.
- **The input.** Tracks choose Computer Keyboard like any MIDI input, and All MIDI Ins includes it. Turning the keyboard off ends its notes.

See [the contract](research/computer-keyboard-contract.md).

## Key Map Mode and mapped buttons — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/KeyMapTests.cpp` (new), with documentation.

- **Buttons.** Buttons can be MIDI mapped: track activator, solo, arm and clip stop, clip slots, scenes, device switches and the transport. A controller presses one as its value rises above 63.
- **Key Map Mode.** KEY or Cmd/Ctrl+K maps computer keys to buttons and controls. The overlay marks Session View slots and scenes too.
- **Test fixes.** The suite found the snapshot recursing through a device button's existence check, now read from the engine. The UI suite's text-focus check waits up to a second for the app to activate.

See [the contract](research/key-map-contract.md).

## Auto-Arm — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/AutoArmTests.cpp` (new), with documentation.

- **Auto-Arm.** Selecting a MIDI track arms it, giving it All MIDI Ins and Auto monitoring when it has no input, so the computer keyboard or a controller plays it at once.
- **Moving and ending it.** Selecting another track moves the arm. Arming by hand ends it. It is not an undo step.

See [the contract](research/computer-keyboard-contract.md#auto-arm).

## Audio device loss and a hardware check — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `tests/DeviceLossTests.cpp` (new) and `tests/HardwareCheck.cpp` (new, manual), with documentation.

- **Device loss.** When the chosen audio interface disappears, playback stops instead of moving to the system's fallback output, and a notice says so. The interface is reopened when it returns.
- **A hardware check.** A manual `--hardware-check` mode measures a real interface without touching the user's settings or opening inputs. It was run on a MOTU M4: output timing and level, an unplug and replug, and a MIDI take showing that the late start seen in a test was the simulated device's.

See [the contract](research/audio-hardware-contract.md).

## Dependency manifest and a recording errors patch — 2026-10-21

Change in `cmake/PatchRecordingErrors.cmake` (new), `cmake/PatchWaveInputStart.cmake`, `CMakeLists.txt`, `scripts/dependency_index.py` (new), `tests/test_dependencies.py` (new), `tests/RecordingFormatTests.cpp` and `docs/dependencies.md`.

- **Recording errors.** An empty folder tree called "The directory…" in the repository led to an engine bug: the take file lookup turned its own error messages into file paths. A new patch returns them as errors.
- **Manifest.** The dependency manifest gains a generated index of the pinned archives and of every hash each patch accepts or writes. A CI unit test keeps the manifest matched to the build.

## Control audit — 2026-10-21

Test change in `src/MainComponent.cpp`, with documentation.

- **Audit.** The UI suite walks the main window in three views: the Arrangement View, the Session View and the MIDI clip editor. Every visible, enabled control must have a handler. One control, the note length the editor reads when drawing, is a marked exception.

See [the contract](research/control-audit-contract.md).

## Soak test — 2026-10-21

Test change in `tests/SoakTests.cpp` (new), `src/Main.cpp` and `CMakeLists.txt`, with documentation.

- **The session.** A three-minute session with an edit every five seconds and a take each minute.
- **What it holds.** The play head keeps time, memory stays flat and open descriptors stay constant. An unlooped first version showed memory growing as playback mapped in more of the media, not from a leak.

See [the robustness contract](research/robustness-contract.md#long-sessions).

## Hosted instruments and plug-in timing — 2026-10-21

Engine and UI change in `src/Session.*`, `src/MainComponent.cpp`, `src/Main.cpp`, `CMakeLists.txt`, `tests/TestProbePlugin.cpp` (new) and `tests/PluginTimingTests.cpp` (new), with documentation.

- **Instruments.** Instrument plug-ins now replace a MIDI track's instrument instead of playing beside the built-in synth.
- **Timing.** A VST3 probe instrument shows the host delivering notes and beats at exact samples, both offline and live at two block sizes.

See [the contract](research/plugin-timing-contract.md).

## Plug-in classification — 2026-10-21

Engine and UI change in `src/Session.*` and `src/MainComponent.cpp`, with documentation.

- **Classification.** Plug-ins are classified as instruments, audio effects or MIDI effects from their descriptions. The browser lists them under their makers with their role, format and known channels. MIDI effect plug-ins go ahead of a MIDI track's instrument.
- **Focus checks.** The UI suite's text-focus checks now report a skip when the system keeps another app in front, as it does while someone uses the machine. Wherever the app can become active, as on CI, they still must pass.

See [the contract](research/plugin-timing-contract.md#classification).

## Plug-in architectures — 2026-10-21

Engine change in `src/PluginBinary.*` (new), `src/Session.cpp`, `src/Main.cpp`, `CMakeLists.txt` and `tests/PluginBinaryTests.cpp` (new), with documentation.

- **Architectures.** A plug-in built only for another CPU architecture (an Intel-only plug-in on Apple silicon, say) is now explained when inserted or opened, instead of only failing to load. The explanation says what it is built for and what to install.

See [the contract](research/plugin-timing-contract.md#architectures).

## Screen reader titles — 2026-10-21

UI change in `src/MainComponent.cpp` and `src/NativeEffectEditor.cpp`, with documentation.

- **Titles.** Every visible control in the main views has a spoken title: track controls with their track's name, device controls with their device's, the rest with their readable names. The major regions have titles and descriptions.
- **Audit.** The UI control audit checks the titles in three views.

See [the contract](research/control-audit-contract.md#screen-readers).

## Packaging and version reporting — 2026-10-21

Root change in `CMakeLists.txt`, `cmake/WriteBuildInfo.cmake` (new), `src/BuildInfo.*` (new), `src/Main.cpp`, `scripts/verify_package.py` (new), `tests/test_verify_package.py` (new) and the CI workflow. The UI change is in `src/MainComponent.cpp`, with documentation.

- **Version.** The CMake project version names the bundle, the application, `--version` and About Halftrack. Each build records its source commit, marked `-modified` when tracked files had uncommitted changes.
- **About Halftrack.** It sits in the application menu on a Mac and in the Help menu elsewhere. It shows the version line as selectable text, the license, and where settings live.
- **Package check.** CI installs each package into a fresh folder, checks its licenses, source and versions against the commit, runs the installed `--version`, then removes it.

See [the contract](research/packaging-contract.md).

## Plug-in sidechains — 2026-10-21

Engine change in `src/Session.*`, `cmake/PatchSidechainTaps.cmake` (new), the earlier node-builder patches (to accept its hash), `tests/SidechainTests.cpp` and `tests/TestSidechainPlugin.cpp` (new). The UI change is in `src/MainComponent.cpp`; the root change in `CMakeLists.txt` and `src/Main.cpp`. Documentation updated.

- **Sidechains.** A hosted plug-in with a sidechain input takes another track's audio there, at its Pre FX, Post FX or Post Mixer tap, from a column at the left of its device panel. Post FX is the default, as in every sidechain of the reference's demo sets.
- **Buses.** Hosted plug-ins report their main and auxiliary buses.
- **Loops.** Loops through sidechains are refused, as are track inputs and outputs that would loop through one.
- **Delay compensation, renders and project changes.** A delayed source stays in step to the sample. Renders, stems, freezes, saving, duplication and removing the source all keep the sidechain.

See [the contract](research/plugin-sidechain-contract.md).

## Freezing Sends Only tracks — 2026-10-21

Engine change in `src/Session.cpp` and `tests/FreezeTests.cpp`, with documentation. No engine patch: the sidechain taps work was patching the same render-graph file at the time.

- **Sends Only.** A Sends Only track now freezes. Its freeze render had been silent, because renders leave a Sends Only track's own output out of the mix, so the engine unfroze it. While a freeze renders, Session points the track at Master outside history, then back. Frozen, it still plays Sends Only.
- **Silent start.** A newly frozen track, whatever its output, was silent for its first seconds of playback: the engine's file cache had failed to open the freeze file before the render wrote it, and retried 4–7 s later. Session now has the file validated after each freeze render.
- **Start check.** The test first required the freeze to sound in the first block of playback. That failed once with four suites running at a time, so it now allows 200 blocks, still far short of the seconds the cache took before.
- **Found, not fixed.** Undoing an Unfreeze leaves any track unfrozen. A track taking a frozen track's audio Post Mixer (the default tap) hears nothing; Pre FX and Post FX work. Exports render frozen tracks from their clips, so the earlier export comparisons never played a freeze; the contract's evidence now says so.

See [the contract](research/freeze-contract.md#corrections).

## MIDI output ports — 2026-10-21

Engine change in `src/Session.*`, `cmake/PatchMidiPorts.cmake` (new), the earlier patches on its four files (to accept its hashes) and `tests/MidiPortTests.cpp` (new). The UI change is in `src/MainComponent.cpp`; the root change in `CMakeLists.txt` and `src/Main.cpp`. Documentation updated.

- **Ports.** Each MIDI output port has Track and Sync switches, set from a MIDI Ports menu in Audio & MIDI Setup. Both start off, as in the reference.
- **Empty MIDI tracks.** A MIDI track's instrument can be removed (No Instrument). The track then plays its MIDI to a port on a chosen channel (MIDI To), from its strip's output chooser.
- **Clock.** Sync sends MIDI Clock with Song Position, Start, Continue and Stop.
- **Tests.** Measured through a virtual port in real time. Test Sessions see only the ports they make.

See [the contract](research/midi-ports-contract.md).

## Interface themes — 2026-10-21

UI change in `src/Theme.*` (new) and the interface sources: `src/MainComponent.cpp`, `src/NativeEffectEditor.cpp`, `src/AudioClipEditor.cpp`, `src/UtilityEditor.cpp`, `src/RackEditor.cpp` and `src/AutomationEditor.cpp`. Engine change to Session's preferences (`src/Session.*`), with `tests/ThemeTests.cpp` (new), `CMakeLists.txt` and `src/Main.cpp`. Documentation updated.

- **Themes.** Dark, Light and High Contrast, chosen in Preferences and applied at once. Every interface colour is authored for Dark and mapped into the others.
- **Contrast.** Light keeps every gray contrast ratio, and each theme's text, selection, meter and disabled contrast is measured.
- **Zoom.** A Zoom Display preference scales the interface.

See [the contract](research/themes-contract.md).

## Effect plug-ins at the end of the chain — 2026-10-21

Engine change in `src/Session.cpp`, `tests/SessionTests.cpp` and `tests/TrackDuplicateTests.cpp`, with documentation.

- **Insertion.** An audio effect plug-in now joins the end of the track's chain, after the devices already there and before the track delay, sends and mixer, as built-in devices do and as the reference does when no device is selected. It used to go first: after the instrument on a MIDI track, after the input on a return. MIDI effect plug-ins still go ahead of the instrument, and instrument plug-ins still replace it. Saved projects keep their order.
- **Tests.** The session suite checks the order on an audio track, a MIDI track and a return. The duplicate suite addresses its Utility and VST3 in their new order.

See [the contract](research/plugin-timing-contract.md#where-an-effect-plug-in-goes).

## Audio input permission — 2026-10-21

Engine change in `src/Session.*`, `src/InputPermission*` (new; AVFoundation on macOS) and `tests/InputPermissionTests.cpp` (new). The root change is in `CMakeLists.txt` and `src/Main.cpp`, with documentation.

- **Refused permission.** Arming, monitoring and recording a hardware audio input are refused with what to do in System Settings, instead of recording silence.
- **Undetermined permission.** The system's prompt is asked for once, without waiting.
- **Withdrawn permission.** Permission withdrawn after arming refuses the recording.

See [the contract](research/input-permission-contract.md).

## Regression evidence — 2026-10-21

QA change in `scripts/evidence_report.py` (new), `tests/test_evidence_report.py` (new), `docs/evidence-map.md` (new) and the CI workflow, with documentation.

- **Evidence map.** Every partial ledger row is tied to the suites that exercise it, or to the hand checks it rests on. A tooling test keeps the map in step with the ledger and the build.
- **Run evidence.** CI writes CTest's results as JUnit. A report gives each row's commit, environment, commands, and expected and actual results.

See [the contract](research/regression-evidence-contract.md).

## Similar sounds — 2026-10-21

Engine change in `src/SoundSimilarity.*` (new) and `tests/SimilarityTests.cpp` (new). The UI change is in `src/MainComponent.cpp`; the root change in `CMakeLists.txt` and `src/Main.cpp`. Documentation updated.

- **Show Similar Files.** Added to a sample's browser menu. It lists the sounds under the folder nearest it, measured on a worker thread.
- **The measure.** It is Halftrack's own: envelope, spectral shape and 16 log bands.
- **Evaluation.** It was evaluated on a generated library in CI and, by hand, on the reference's categorised drum one-shots.

See [the contract](research/similarity-contract.md).

## Packs — 2026-10-21

Engine-independent content handling in `src/Packs.*` (new), with `tests/PackTests.cpp` (new). The UI change is in `src/MainComponent.cpp`; the root change in `CMakeLists.txt` (which now links JUCE's cryptography module, for SHA-256) and `src/Main.cpp`. Documentation updated.

- **The format.** Halftrack's own content packs: a zip with a manifest of hashed files and versioned requirements.
- **Installing and uninstalling.** Packs install verified and atomically into the User Library, refusing damaged, unsafe, unmet and downgrading installs. Uninstalls and upgrades keep the user's own files.
- **The UI.** The File menu installs packs; the browser lists, opens and uninstalls them.

See [the contract](research/packs-contract.md).

## Audio-thread separation — 2026-10-21

QA change in `tests/RealtimeDevice.h` (new) and the separation test in `src/MainComponent.cpp`; the root change in `CMakeLists.txt` and `src/Main.cpp`. Documentation updated.

- **Real-time device.** A test device calls the engine from a real-time thread of its own.
- **Stress.** While it records with a clip and a synth playing, the interface searches, paints and resizes continuously.
- **Checks.** Callback deadlines and the take's every sample are checked, and the timings are printed.

See [the contract](research/separation-stress-contract.md).

## Mackie Control surfaces — 2026-10-21

Engine change in `src/MackieControl.*` (new; the protocol), `src/Session.*` and `tests/MackieTests.cpp` (new). The UI change is in `src/MainComponent.cpp` (Control Surface in Audio & MIDI Setup); the root change in `CMakeLists.txt` and `src/Main.cpp`. `tests/MidiInputTests.cpp` now allows test-made ports in its no-physical-inputs check. Documentation updated.

- **Strips.** A Mackie Control surface on chosen ports drives eight bankable strips: volume, pan, Rec, Solo and Mute.
- **Master and transport.** It also drives the master fader, the transport, Cycle and Click, all through the Session's own edits.
- **Feedback.** Moving faders, lights, rings and names follow.

See [the contract](research/control-surfaces-contract.md).

## macOS CI timing — 2026-10-22

QA area.

- **Full speed.** Self-test suites now hold off App Nap. With no window and no real audio, macOS could throttle a suite's timers and message dispatch: the soak passed in about a minute on Linux and Windows but timed out after 600 s on macOS. Locally a 20-second soak went from 15.2 s to 12.4 s.
- **Soak takes.** The soak suite expects one take a minute, so a shorter `HALFTRACK_SOAK_SECONDS` run passes.
- **Lane click.** The UI check of a lane click during playback now waits until the play head moves, up to 400 more blocks. Its failure message says whether the transport was playing.

## Tempo and transient analysis — 2026-10-22

Engine and UI areas.

- **Tempo from content.** A sound's tempo and first beat come from its onsets: an autocorrelation comb, a beat grid, then a line through its transients.
- **Auto-Warp Long Samples.** It now warps a long sample at the tempo found, starting on the first beat with a warp marker there. It falls back to whole bars without a steady beat.
- **Transients.** The clip view shows a warped clip's transients, and a double-click beside one pins a marker on it.
- **Measured.** Generated loops cut to misleading lengths are found exactly. On 69 reference library loops of 6 s or more, 78% come within 0.5 BPM; the length guess does better on those, so short samples keep it.

See [the contract](research/warp-contract.md#tempo-and-transient-analysis-2026-10-22).

## Clip loops and edges — 2026-10-22

Engine and UI areas.

- **Loops.** MIDI and warped audio clips loop: the Loop box beside the note editor (and in a warped clip's view) sets Start, End, Loop, Position and Length in bars.beats.sixteenths.
- **Edges.** In the Arrangement, a looped clip's right edge repeats its loop and its left edge trims it round the loop. Unlooped clips trim in beats, keeping their content in place.
- **Editors.** The note editor and waveform dim what the clip does not play, show the loop brace and start, and follow the play head round the loop. The Arrangement draws each pass with a notch.
- **Copies.** Duplicates and pastes keep the loop and start.
- **One-shots.** A Session clip with Loop off plays once and stops.
- **Measured.** Each case is rendered and its beats compared (`tests/ClipLoopTests.cpp`), and the UI suite drives the box and the edges.

See [the contract](research/clip-loops-contract.md).

## Time selection clipboard — 2026-10-22

Engine and UI areas.

- **Copy and cut.** Cmd/Ctrl+C and Cmd/Ctrl+X copy and cut a time selection across its tracks.
- **Paste.** Cmd/Ctrl+V pastes it at the insert marker from the selected track, replacing what was there and selecting what was pasted. The audio and the range's automation travel with the copy, into other projects too.
- **Measured.** Rendered stems show each track playing just the copied beats where pasted (`tests/RangeClipboardTests.cpp`), and the UI suite drives the keys.

See [the contract](research/arrangement-selection-contract.md#cut-copy-and-paste-of-a-time-range-2026-10-22).

## Recorded placement and Driver Error Compensation — 2026-10-22

Engine, UI and root areas.

- **Placement fixed.** Takes were placed one block early: the engine stamped input with the device's stream time while the output rendered with it plays a block later. An engine patch stamps input where its output plays, so through a simulated loopback a recorded click lands exactly where it played.
- **Driver Error Compensation.** It moves recordings earlier for a driver that misreports its latency: a 200-sample misreport is put right by compensating 200 samples. Audio & MIDI Setup shows it with the input, output and overall latency.
- **Not yet measured.** Physical loopback, which needs a cable from an output to an input.

See [the contract](research/recording-latency-contract.md).

## Takes through CPU overloads — 2026-10-22

Engine change in `src/Session.cpp` and `tests/RecordingStallTests.cpp` (new); the root change in `CMakeLists.txt` and `src/Main.cpp`. Documentation updated.

- **Cause.** Under heavy machine load the separation suite once lost a block from a take. The engine's device manager silenced any callback once its load meter passed 0.98, and a silenced callback also skipped its input and its block of time.
- **Fix.** Sessions keep rendering at any load, so every input block the driver delivers reaches the take and the transport keeps time. No dependency patch is involved.
- **Measured.** `recording-stalls-acceptance` injects a 40 ms stall, six 8 ms callbacks in a row and a 100 ms stall into a 450-block take. Each drives the meter past 0.98. Before the fix, 6 callbacks fell silent and the take lost 6 blocks, its first wrong sample in the block after the first stall. After it, every sample is in order and the output never falls silent. The separation suite passed 3 of 3 runs at a load average of 6 to 8. It was not rerun at the original load of about 16.

See [open questions](open-questions.md#cpu-overloads-during-takes).

## Movies — 2026-10-22

Engine, UI and root areas.

- **Import.** On macOS, a QuickTime or MPEG-4 movie imports as a video clip: its sound is an audio clip that keeps the movie, both collected into the project.
- **Video Window.** View > Video Window shows the clip's picture under the play head and plays along with the transport.
- **Create Video.** Export Audio can also write a QuickTime movie of the picture with the exported sound.
- **Measured.** A generated movie whose frames show their numbers reads back frame for frame, and the exported movie's sound equals the mix (`tests/VideoTests.cpp`).

See [the contract](research/video-contract.md).

## Warp commands — 2026-10-22

Engine and UI areas.

- **Warp From Here (Straight).** Right-clicking a warped clip's waveform offers it: the audio from that point plays at the tempo found in it, pinned where the point already plays.
- **Warp as N-Bar Loop.** The same menu fits the clip's audio to the bars its length suggests.
- **Measured.** A 97 BPM loop after 3.2 s of noise is found and rendered with each hit a beat apart within 0.65 ms; a four-bar loop at 105 BPM fits exactly.

See [the contract](research/warp-contract.md#warp-commands-2026-10-22).

## External Sync — 2026-10-22

Engine and UI areas.

- **Following a clock.** An input's Sync switch in MIDI Ports shows EXT in the control bar. With it on, the transport follows that input's MIDI Clock: its tempo, its position while playing, and Start, Continue, Stop and Song Position. No undo steps are added.
- **Measured.** Through a virtual port in real time, the tempo follows 120 and then 90 BPM and the transport stays within 0.2 beats of the clock (`tests/ExternalSyncTests.cpp`).

See [the contract](research/midi-ports-contract.md#external-sync-2026-10-22).

## Capture and Insert Scene — 2026-10-22

Engine and UI areas.

- **Capture.** Session menu > Capture and Insert Scene (Cmd/Ctrl+Shift+I) inserts a scene after the selected one holding the clips now playing. It plays on at once, the old scenes keep copies, and it is one undo step.
- **Measured.** MIDI clips with held notes from two scenes and an audio loop are captured. Their render matches the uninterrupted render exactly (`tests/SceneCaptureTests.cpp`).

See [the contract](research/scene-capture-contract.md).

## Moving and copying Session clips — 2026-10-22

Engine and UI areas.

- **Grid editing.** A Session clip drags to another slot of its kind, and Option/Ctrl-drag copies it. Cut, Copy, Paste and Duplicate are in the Session menu and on Cmd/Ctrl+X/C/V/D, each one undo step. Duplicate fills the slot below or inserts a scene for the copy. Audio on the clipboard keeps its own file and pastes into other projects. Session and Arrangement copies share one clipboard.
- **Measured.** A playing clip moved within its track renders with 0.0 difference from the unmoved performance, its held note sounding on (`tests/SlotClipTests.cpp`).

See [the contract](research/session-clip-editing-contract.md).

## The Arrangement's editing grid — 2026-10-22

Engine and UI areas.

- **The grid.** The Arrangement's grid replaces the old four-step zoom rule. It is the reference's adaptive or fixed grid on its ladder, from 512 bars to 1/16384.
  - Adaptive widths scale with the view, and there are triplets and Snap to Grid.
  - Cmd/Ctrl+1 to 5 run Narrow, Widen, Triplet, Snap and Fixed. The context menu and the View menu offer them too.
  - The readout at the Arrangement's foot reads "1/16", "1/8T", "1/1" or "Off".
  - The grid is saved with the project, outside undo.
- **Bypass modifier.** Snapping is bypassed with Cmd on the Mac (Alt elsewhere), as in the reference. Option now only slides waveforms and copies.
- **Sources.** The defaults come from the reference's Sets. The rules come from static reading of its code, which a delegated agent did; no measurement in the app.
- **Measured.** `tests/EditGridTests.cpp` and the UI suite.

See [the contract](research/edit-grid-contract.md).

## Selecting several Arrangement clips — 2026-10-22

UI and engine areas.

- **Selection.** Shift-click and Cmd/Ctrl-click select several Arrangement clips. The group drags, copy-drags, duplicates, deletes, copies and cuts together, each one undo step.
- **Engine.** `Session::copyClipTo` leaves the copies of a copying drag. `copyRange`'s `onlyClips` keeps exactly the selected clips.
- **MIDI moves.** A moved MIDI clip now keeps its beats across tempo changes. It used to keep its seconds; a clip moved past a slow-down measured 0.605 beats instead of 1.
- **Measured.** The range clipboard suite and the UI suite.

See [the contract](research/arrangement-selection-contract.md#selecting-several-clips-2026-10-22).

## Each clip's editing grid — 2026-10-22

Engine and UI areas.

- **Per-clip grids.** Every clip has its own editing grid, as in the reference, saved with the clip and carried by copies.
- **Note editor.** Its Grid button replaces the 1/4–1/32 snap menu.
- **Snapping.** The note editor and audio clip view snap to the clip's grid at their own zoom.
- **Commands.** Cmd/Ctrl+1 to 5 and the View menu act on the focused editor's grid.
- **Measured.** The grid suite and the UI suite. The note-editing checks now fix the 1/16 grid they were written for.

See [the contract](research/edit-grid-contract.md).

## One clipboard for both views — 2026-10-22

Engine area.

- **Paste.** A copied Session clip pastes into the Arrangement, and a copied Arrangement clip into a slot, as with the reference's one clipboard. A MIDI clip keeps its beats and content start either way. Arrangement clips take the default launch settings in a slot.
- **Measured.** `tests/SlotClipTests.cpp`.

## Consolidate Time to New Scene — 2026-10-22

Engine and UI areas.

- **The command.** Create > Consolidate Time to New Scene turns the Arrangement's time selection into a scene with one looping clip per track, made as Consolidate makes them, leaving the Arrangement as it was. It is one undo step.
- **Measured.** `tests/SceneCaptureTests.cpp` and the UI suite.

## Loop recording into take lanes — 2026-10-22

Engine area.

- **Passes.** Recording round the Arrangement loop now gives each pass, audio or MIDI, its own take lane, the last pass ending on top in the main lane, as the reference's Comping chapter describes.
- **A loss fixed.** Before, the engine referenced only the first pass and left the rest on disk unused.
- **Short loops.** A Tracktion patch allows loops shorter than its two-second minimum.
- **Measured.** `tests/TakeLanesTests.cpp`.

See [the contract](research/take-lanes-contract.md#loop-recording-2026-10-22).

## Curved automation segments — 2026-10-22

Engine and UI areas.

- **Curves.** Automation points carry a curve for the segment after them, played by the engine. In Automation Mode, Option/Alt-dragging a segment curves it and Option/Alt-double-clicking straightens it, as in the reference. Drawing follows the engine's playback form of the curve, not `AutomationCurve::getValueAt`, which differs.
- **Measured.** A curved volume render matches the predicted level within 2% (`tests/AutomationTests.cpp`), and the UI suite checks the gestures.

See [the contract](research/arrangement-automation-contract.md#curved-segments-2026-10-22).

## Automation selections and Simplify Envelope — 2026-10-22

UI area.

- **Editing.** In Automation Mode, the breakpoints inside a time selection drag together, Shift-dragging a segment moves it, and the envelope's context menu offers Simplify Envelope over the selection, as in the reference. Automation lanes' backgrounds select time.
- **Measured.** The UI suite.

## Track heights and optimizing the view — 2026-10-22

Engine and UI areas.

- **Heights.** Arrangement tracks resize from the split line under their headers, all at once with Option/Alt, and with Option/Alt +/- and Option/Alt-scrolling. Heights are saved with the project.
- **Optimize.** H fits every track into the view and W fits the length, each toggling back, as in the reference.
- **Folding.** Tracks fold to their header strip with a triangle, U for the selected tracks and Option/Alt+U for all, saved with the project.
- **Measured.** The track order suite and the UI suite.

See [the contract](research/track-heights-contract.md).

## Follow Behavior — 2026-10-22

UI area.

- **Scroll follow.** Preferences > Follow Behavior offers Page (as before) or Scroll, as the reference's does; Scroll keeps the play head in the middle of the Arrangement and piano roll once it reaches it. It is saved with the preferences.
- **A fix.** Changing any setting in the Preferences panel no longer resets Driver Error Compensation, which the panel does not show.
- **Measured.** The preferences suite and the UI suite.

## Rendering selected tracks — 2026-10-22

Engine and UI areas.

- **Rendered Track.** Export Audio's Rendered Track offers Selected Tracks Only and each track or group by name, after Master and All Individual Tracks, as the reference's does. They render just those stems, without Master (`AudioExportSettings::stemTracks`).
- **A wait fixed.** The follow-action dialog's UI check now waits long enough for the dialog's closing callback.
- **Measured.** The stems suite and the export UI checks.

## Several Session slots — 2026-10-22

Engine and UI areas.

- **Selection and editing.** Session slots select together by Shift-click, Cmd/Ctrl-click and a rubber band from an empty slot, as in the reference. The group drags, copy-drags, deletes, copies, cuts, pastes in its shape and duplicates below itself, each one undo step (`Session::moveSlotClips`, `clearSlots`, `copySlotClips`).
- **Measured.** `tests/SlotClipTests.cpp` and the UI suite.

## MIDI step recording — 2026-10-22

Engine and UI areas.

- **Step recording.** With the clip's track armed and the transport stopped, Right Arrow in the piano roll adds the held keys at the insert marker, a grid step long, and moves the marker on. Held through another Right Arrow they lengthen; Left Arrow takes them out again, as the reference's step recording does. Each is one undo step.
- **Held notes.** `Session::heldInputNotes` reports the notes held now on an armed or In-monitored MIDI track.
- **Measured.** The Capture MIDI suite and the UI suite (computer keyboard notes, no microphone).

See [the contract](research/capture-midi-contract.md#step-recording).

## MIDI editor Preview — 2026-10-22

Engine and UI areas.

- **The switch.** The piano roll's Preview switch sits above the keys, as the reference's does. It is off at first and saved with the project, not as an undo step. While it is on, it sounds the keys, clicked, drawn, dragged and transposed notes on the clip's track.
- **Step recording now needs it on**, as in the reference.
- **Key tracks.** Shift-clicking a key selects every note of that pitch.
- **Engine.** `Session::previewNote` plays through the engine's live MIDI injection; that path's lock is noted in the real-time audit.
- **Measured.** The new note-preview suite, which listens to the synthetic device's output, and the UI suite.

See [the contract](research/note-preview-contract.md).

## Start Recording on Scene Launch — 2026-10-22

Engine and UI areas.

- **The preference.** Start Recording on Scene Launch, off by default as in the reference. With it on, launching a scene records into its empty slots on armed tracks.
- **A fix.** When one press starts several Session takes from a stopped transport, they now all start where the transport stood. The engine starts the transport with the first take, so the others used to wait for the launch grid, and a MIDI take could miss the first notes.
- **Measured.** The Session record suite, the preferences suite and the UI suite.

See [the contract](research/session-recording-contract.md#start-recording-on-scene-launch).

## Plug-in sources and Rescan — 2026-10-22

Engine, UI and root areas.

- **Plug-Ins settings.** These replace the generic plug-in list dialog, as the reference's settings do. They offer Use Audio Units v2, the VST3 system folders and a custom VST3 folder with Browse, plus Rescan; Alt/Option-click forgets the list first.
- **Rescan.** It runs in the background through the plug-in list's scanner (the separate process in the app). It adds new plug-ins once and drops those whose file has gone or whose source is off.
- **At launch.** The app rescans at each start unless Alt/Option is held (`src/Main.cpp`).
- **Measured.** The new plug-in-sources suite, using the generated VST3 fixture in a custom folder, and the UI suite.

See [the contract](research/plugin-sources-contract.md).

## macOS CI timeouts — 2026-10-22

QA and root areas.

- **Range clipboard** renders only the stem it reads (79 s to 18 s locally), not every track's. It had overrun its 120 s timeout on the hosted macOS runner.
- **Soak.** The macOS runner plays a one-minute soak (`HALFTRACK_SOAK_SECONDS=60`) instead of three. Its dispatch between synthetic blocks is several times slower than a desktop Mac's, so three minutes overran 600 s. Linux, Windows and local runs keep three.
- **The lane click while playing** UI check now waits up to ten seconds of wall-clock time for the play head to move, not a count of blocks. If it fails, it says whether playback had started and which device is open; macOS CI has failed it since 4a45303, with playback found stopped.

## Default clip fades — 2026-10-22

Engine, UI and root areas.

- **Default fades.** These follow a static reading of the reference's code (raw material ignored in `artifacts/reverse-engineering/fades/`). With Create Fades on Clip Edges on, the default, as in the reference:
  - new audio clips, and the two edges every split makes, fade over 4 ms along the smoothstep 3x² − 2x³;
  - touching clips with default edges crossfade, the later one playing the material before its start, so a split renders as the unsplit clip.

  Setting a length or curve makes an edge an ordinary fade, and the audio clip editor's Default switches restore it.
- **Engine patch.** `cmake/PatchClipFades.cmake` adds the smoothstep curve and two behaviour hooks. The node builder uses them so nothing derived is stored, and the earlier patches on those files accept its hashes.
- **Tests.** Nineteen suites, and the UI suite, compare clip renders or fade values with hard edges. They now switch the preference off first, through a shared test helper (`useHardClipEdges`); the new suite covers the fades themselves.
- **Measured.** The clip-fades suite (exact smoothstep edges at 48 kHz, a split within 1e-7 of the unsplit sine), the audio-clips editor checks and the UI suite.

See [the contract](research/clip-fades-contract.md).

## Four-bar count-in — 2026-10-22

Engine, UI and root areas.

- **Count-In** offers None, 1 Bar, 2 Bars and 4 Bars, as the reference's menu does.
- **A fix.** A bar of count-in now lasts the meter's bar in crotchets. In 6/8 it was two bars, and 7/8 could not be expressed. An engine patch (`cmake/PatchCountIn.cmake`) adds the four-bar mode and the bar length.
- **Measured.** The metronome suite and the UI suite.

## Metronome sounds — 2026-10-22

Engine and UI areas.

- **Sound.** The metronome's menu offers Classic (the default), Click and Wood, as the reference's does, kept with the preferences. The ticks are Halftrack's own, synthesised offline (`src/MetronomeSounds.cpp`) to the character of the reference's samples. The Session writes them beside the preferences and points the engine's click track at them.
- **The menu** now has the reference's Count-In and Sound sections, and "Enable Only While Recording".
- **Measured.** The metronome suite (each tick's level, pitch, silence at its ends and repeatability, and the files the click track plays) and the UI suite.

See [the contract](research/metronome-contract.md).

## Metronome rhythm — 2026-10-22

Engine, UI and root areas.

- **Rhythm.** The metronome's menu offers Auto, 1 Bar and divisions from 1/2 to 1/16T, as the reference's does. Divisions that do not divide the bar are disabled, and play as Auto if the meter changes. These follow a static reading of the reference's code.
  - Auto ticks on the meter's denominator, so 6/8 now ticks eighths.
  - The first tick of each bar is accented.
- **Storage.** The rhythm is kept with the project, as one undo step.
- **Engine patch.** `cmake/PatchMetronomeRhythm.cmake` lets the click generator follow the rhythm.
- **Measured.** The metronome suite (clicks sample by sample at several rhythms and meters) and the UI suite.

See [the contract](research/metronome-contract.md#rhythm).

## Application-wide count-in — 2026-10-22

Engine area.

- **Scope.** The count-in is now an application setting, as the reference's is (static reading): kept with the preferences, not each project, and not an undo step. Projects that hold one from earlier versions still validate it, and it is ignored.
- **Measured.** The metronome suite (undo, a new project and the next launch keep it) and the MIDI input, preferences and UI suites.

## Output routing — 2026-10-22

Engine, UI and QA areas.

- **Output Config.** Audio & MIDI Setup gains Output Config, as the reference's has: each output pair is stereo or two mono outputs, each switchable off, kept with the device settings.
- **Ext. Out.** Tracks play straight to an output with Audio To Ext. Out, bypassing the master.
- **Master Out.** The master strip's Master Out moves the whole mix to another output, kept with the project.
- **Tests.** The synthetic test device can offer four outputs (`makeFourOutputSyntheticTestDeviceType`), and the UI suite now uses it.
- **Measured.** The new output-routing suite, which measures each output channel, and the UI suite.

See [the contract](research/output-routing-contract.md).

## Playback through MIDI device changes, and slow-CI fixes — 2026-10-22

Engine and QA areas.

- **A fix.** Plugging in, or removing, a MIDI device no longer stops playback. The engine rebuilt its playback devices and left the play head stopped; an engine patch (`cmake/PatchPlaybackContinues.cmake`) resumes it.
- **Found through macOS CI.** The UI lane-click check failed there because the engine's periodic MIDI rescan found the suite's own virtual ports mid-playback. `HALFTRACK_TEST_DISPATCH_MS=15` reproduces it locally.
- **External sync** now keeps its blocks in step with the wall clock however slowly messages are dispatched (a new `pumpSyntheticTestDeviceWithoutDispatch`), so its real-time tempo checks hold on the slower macOS runner.
- **Measured.** The Capture MIDI suite (a device added while playing), the UI suite and the external sync suite, each also with slowed dispatch.

- **A second fix.** Recording and stopping before the device's next block started playback a moment later: the engine's stop left a posted play in place, the play head started on that block and the transport followed it. The same patch now withdraws the posted play at a stop. Found through the punch suite failing under parallel load, where the first fix had kept that stray playback going through a MIDI rescan; the transport suite now records and stops at once and finds the transport stopped.

## Metronome with Punch-In — 2026-10-22

Engine and QA areas.

- **The click with Punch-In.** As the reference's manual says, the metronome is heard only from the punch-in point while recording with Punch-In; an ordinary Play clicks from the start again.
- **Measured.** The metronome suite compares the click sample by sample, before and after the punch-in point. It failed without the change.

## The Record button and Start Playback with Record — 2026-10-22

Engine, UI and QA areas.

- **Record toggles.** As the reference's Arrangement Record button does, pressing Record (or F9) while recording ends the recording where it was pressed, keeping its takes, and playback carries on; pressed while playing, it records from there. Stop used to be the only way to end a take.
- **Start Playback with Record.** A new preference, on by default. Off, Record pressed while stopped lights and waits until Play or a Session launch starts the transport; Shift reverses it, as the manual says.
- **Engine use.** Ending a loop recording while playing stops the inputs unlooped first: the engine's way, switching the loop off, would stop the transport.
- **Measured.** The transport suite (takes and timing for each case, waiting, Shift, a launch), the take-lanes suite (a loop recording ended by Record, with its passes cut as at Stop), the preferences suite and the UI suite.

## Cut, Paste and Duplicate Time — 2026-10-22

Engine, UI and QA areas.

- **The reference's other time commands.** Cut Time, Paste Time and Duplicate Time act across every track, as Insert Silence and Delete Time do, each one undo step, from the Edit menu and Shift+Cmd/Ctrl+X, V and D.
- **A fix.** Insert Silence inside an automation ramp stretched the ramp across the gap, changing how the lane played before the insertion point. It now holds the lane's value there across the gap.
- **Measured.** The time-edit suite (layouts, refusals, undo, and a render in which a duplicate plays sample for sample as what it copies) and the UI suite. See [the contract](research/time-commands-contract.md).

## The cue: Cue Out, the cue volume and the Solo/Cue switch — 2026-10-22

Engine, UI and QA areas. Researched by static reading of the reference's code (findings in the ignored `artifacts/reverse-engineering/cue/`).

- **Cue Out and the cue volume.** The master strip has a Cue Out chooser and a CUE knob. The click and previews always play on Cue Out at the cue volume, −6 dB at first as the reference's default Set has it, which the Samples browser's preview volume now also sets. Both are kept with the project.
- **Cue mode.** With a Cue Out apart from the Master Out, the Solo/Cue switch turns the tracks' solo switches into cue switches. A cued track is heard on Cue Out from before its fader, pan and activator, as the reference's pre-fader listen is, while playing on as before. The switches keep their state between modes. Sharing outputs puts Solo back, keeping the choice.
- **Exclusive solo.** A plain click on an engaged switch now clears every switch, as the reference's does.
- **Engine.** A new patch, `cmake/PatchCue.cmake`, adds three behaviour hooks and the cue's sum on its device; every earlier patch on its two files accepts its results.
- **Measured.** The output-routing suite, per output channel of a four-output synthetic device, and the metronome, preview and UI suites. See [the contract](research/cue-contract.md).
- **Changed defaults.** The click and previews now play 6 dB quieter by default, at the reference's cue level.

## Solo in Place — 2026-10-22

Engine, UI and QA areas.

- **Solo in Place.** The reference's switch, kept with the project and not an undo step, in the View menu with Halftrack's other Options items. Off, soloed tracks are heard without the returns.
- **A fix.** Soloing a return beside a source now mutes the other returns, as the reference's does; they used to stay open.
- **Measured.** Sample-exact renders in the send/return suite, and the UI suite.

## Return tracks stay last — 2026-10-22

Engine, UI and QA areas.

- **Returns after the other tracks.** As in the reference, new audio and MIDI tracks go before the returns, moves cannot cross them (the track menus disable such moves), and a project holding returns among the other tracks opens with them last.
- **Callers.** Importing audio or movies to a new track, MIDI file import and Slice to New MIDI Track no longer assume the new track is the last one.
- **Measured.** The track-order suite (insertion, undo, refused moves, and a project file edited to put the return first) and the UI suite.

## The Main track's name — 2026-10-22

UI area. The reference's built-in default Set names its master track Main, as its Live 12 interface does. Halftrack's interface now says Main where it said Master: the Session grid's column, the mixer strip, Main Out, Audio To, the device chain, the automation editor, the export dialog, messages, and the stem of the whole mix (now "<project> Main.wav"). The Session API keeps its master names.

## Count-in start and the punch window — 2026-10-22

Engine and QA areas. From a static reading of the reference's transport code (findings in the ignored `artifacts/reverse-engineering/transport/`).

- **Where a count-in starts.** On the bar line nearest to its bars before the recording's start, so from mid-bar it is not whole bars long. Its first click is at its start: the engine's half-beat lead before it is gone. A new engine patch, `cmake/PatchCountInStart.cmake`.
- **The punch window.** A count-in comes only when recording starts inside the punch window; from before Punch-In's point the pre-roll plays at once.
- **The punch click.** The pre-roll is silent only with Enable Only While Recording, which is what the reference's manual describes.
- **Measured.** The metronome and MIDI input suites.

## Arrangement loop playback from the insert marker — 2026-10-22

Engine and QA areas. From a static reading of the reference's transport code.

- **Play with the loop on** starts from the insert marker, as the reference's does. From before the loop it rolls into it; from at or past its end it plays on without looping; jumps behave the same, no longer clamped into the loop. Before, the engine jumped to the loop's start.
- **Recording round the loop** starts where the transport is. A first pass from before the loop runs to its end. With Punch-In or Punch-Out each pass is trimmed to the brace, so the pre-roll is left out and every pass gets its take lane.
- **Engine.** A new patch, `cmake/PatchLoopPlayback.cmake`, on the play head, the transport and the recording parameters.
- **Measured.** The transport, take-lanes and punch suites, with the full gate.
- **Set Song Start Time Here.** The loop brace's menu item: while it is on, Play starts at the loop's start, the loop on or off. Kept with the project, one undo step.

## Mixer sections — 2026-10-22

UI area. View > Mixer Controls shows and hides the Session mixer's In/Out, Sends, Returns, Mixer, Track Delay and Crossfader sections, as the reference's View menu does. The defaults are those of its built-in default Set, so Track Delay and the crossfader are now hidden at first. The strips lay out and size themselves from the sections shown, and the choice is kept with the project. See [the contract](research/mixer-sections-contract.md).

## Triplet launch grids — 2026-10-22

Engine and UI areas. The global launch quantization offers the reference's grids: None, 8, 4, 2 and 1 Bars, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T and 1/32. It offered only None, 1/4 and 1 Bar before. Clips' and the preferences' grids gain the same triplets. The launcher suite checks a launch on the eighth-triplet grid, and the UI suite checks the menu.
- **Record quantization** gains the reference's combined 1/8 + 1/8T and 1/16 + 1/16T grids, each snapping a note to the nearer of its straight and triplet points. The Edit menu lists all of them in the reference's order and names. The MIDI input suite checks every grid against independent arithmetic.

## MIDI note mappings — 2026-10-23

Engine, UI and root areas (a new Tracktion patch). Following the reference manual's MIDI and Key Remote Control chapter:

- **Notes map like controllers.** In MIDI Map Mode a note-on maps the note. A mapped note presses a clip slot with its note-on and releases it with its note-off, toggles a switch, and switches a control between its Min and Max. The list and overlay name notes as the reference does ("Ch 1  Note C3", C3 being note 60).
- **Releases.** A clip slot follows its clip's Launch Mode whatever presses it: a controller releases it as it falls to 63 or below, and a computer key as it goes up, so Gate and Repeat clips play while held. A held mapped key no longer presses again as the key repeats.
- **Track input.** Mapped notes and controllers, and every note and controller while one is being learned, stop acting as track input, so a note that launches a clip does not also play or record on an armed track. A new engine patch, `cmake/PatchRemoteMidi.cmake`, lets the Session's MIDI remote take messages before the input's tracks.
- **Piano editor.** Its note names now put C3 at note 60, as the reference and the rest of Halftrack do.
- **Measured.** The MIDI mapping, key map and UI suites, including a virtual controller feeding an armed MIDI track; with the remote passing everything through, the track-input check fails.

## A locator as the song's start — 2026-10-23

Engine and UI areas. From a static reading of the reference's code (findings in the ignored `artifacts/reverse-engineering/tracks/`). A locator's menu offers Set Song Start Time Here: Play then starts at the locator, which takes the song's start from the loop brace or another locator. Moving the locator moves it and deleting it clears it; it is one undo step and kept with the project. The ruler marks the song's start. Measured in the transport, locator and UI suites ([contract](research/transport-contract.md)).

## Solo and arm leave the undo history — 2026-10-23

Engine and UI areas. From a static reading of the reference's code: its record arm and its solo/cue values carry the flag that keeps them out of the undo history, while the activator, renaming and colour stay undoable. Halftrack's track and group solo, cue and record arm are no longer undo steps; undo passes over them to the edit before. They still mark the project changed. Measured in the Session, sends, MIDI input and UI suites with the full gate ([contract](research/solo-arm-contract.md)).

## Remote controls follow the reference's rules — 2026-10-23

Engine area. From a static reading of the reference's remote-control code (findings in the ignored `artifacts/reverse-engineering/notemap/`): a note or key on a control now toggles a two-state switch, steps a choice to the next value and wraps, and sends anything else to Min from at or above the midpoint of its range, or to Max below it. A controller sets a switch (activator, solo, arm, device switch, Record, Loop, Metronome) on at 64 and above and off below, instead of toggling it as it rises. Measured in the MIDI mapping and key map suites.

## Track insertion and automatic names — 2026-10-23

Engine and UI areas. From a static reading of the reference's code (findings in the ignored `artifacts/reverse-engineering/tracks/`):

- **Insert** puts a new audio or MIDI track after the last selected track, inside its group, or after the other tracks when a return is selected or nothing is. A new return goes after a selected return. The new track is selected alone. Cmd/Ctrl+Option/Alt+T inserts a return.
- **Insert MIDI Track** makes an empty MIDI track, as the reference's does; it no longer makes and opens a clip.
- **Names.** Tracks without a name of their own show automatic names: their number and their MIDI instrument's name, their first clip's name, or Audio, MIDI or Group. Returns show their letter and their devices' names. Renaming to nothing, or to the automatic name, makes a name automatic again, and a leading # becomes the number. A new project's tracks, and imported audio and movie tracks, are named automatically.
- **Measured.** The track-order, video and UI suites, with the full gate ([contract](research/track-names-contract.md)).

## The built-in default set — 2026-10-23

Engine, UI and root areas. Without a saved default set, the app starts, and File > New makes new projects, from a project like the reference's built-in default Set: 1-MIDI and 2-MIDI without instruments, 3-Audio, 4-Audio, a return with the reverb and one with the delay, eight scenes, 120 BPM in 4/4. A project or audio files named at launch replace it. The Session's plain new project of two audio tracks stays for the suites. Measured in the track-order and UI suites ([contract](research/presets-contract.md)).

## Launch velocity — 2026-10-23

Engine, UI and root areas. From a static reading of the reference's code (findings in the ignored `artifacts/reverse-engineering/notemap/`; its level scales from the earlier `semantic-triage-volume-conversion-20261014`):

- **Velocity.** The Launch box's Velocity (0 to 100 %, saved with the clip) sets how much a launching note's velocity changes the clip. A note mapped to a slot launches it with its velocity, and any other launch with full velocity.
- **The effect.** An audio clip's gain moves along the reference's clip gain scale (−70 to +24 dB, built in `src/LevelScale.h` as the reference builds it), and a MIDI clip's note-on velocities scale, never below 1, until the next launch.
- **Engine.** A new patch, `cmake/PatchLaunchVelocity.cmake`, carries the gain and scale on the launch handle to the slot's node.
- **Measured.** The launch-modes suite renders the levels within 2e-7 of an independent model of the scale and reads the MIDI velocities through a MIDI Monitor. The UI suite edits Velocity.
- **Also.** The transport and external-sync suites allow for a loaded runner starting late after a jump or Continue.

## The loop brace — 2026-10-23

UI and engine areas. The Arrangement's loop brace now drags as the reference's manual describes. Its bar moves the loop and its handles move its ends, on the grid. A click selects the loop's time across the tracks. While it is selected, the arrow keys move it, and with Cmd/Ctrl resize, double or halve it. Loop changes are no longer undo steps, as the reference keeps its loop out of the undo history (static reading of its code). Measured in the UI and Session suites ([contract](research/transport-contract.md)).

## Empty MIDI tracks — 2026-10-23

UI area. Insert MIDI Track (Cmd/Ctrl+Shift+T, the Create menu and + MIDI) makes a MIDI track without an instrument, as the reference's does, shown as n-MIDI until an instrument is dropped on it, in one undo step. The UI suite's editor checks give their tracks the built-in synth themselves ([contract](research/track-names-contract.md)).

## Monitoring switches — 2026-10-23

UI area. A track's input monitoring is now the reference's three switches, In, Auto and Off, with the current one lit, in the Session mixer's In/Out section and the Arrangement track header, where it was a menu. The UI suite switches a MIDI track to Auto and In with them.

## Count-in for Session takes — 2026-10-23

Engine area. From a static reading of the reference's code: a Session take that starts the transport now counts in, as an Arrangement recording does. The transport starts on the bar line nearest the count-in's bars before the take, clicks from there to the take's start with the metronome on or off, and the take starts where the transport stood. Measured in the session-record suite: a one-bar count-in from bar 2 starts at 0 s with four beats to count, clicks at once, and the take holds only the note played after it ([contract](research/session-recording-contract.md)).

## MIDI From channels — 2026-10-23

Engine and root areas. A MIDI track can take one MIDI channel from its input, as the reference's MIDI From channel chooser does, for monitoring, recording, Capture MIDI and overdubs (`Session::setTrackInputChannel`). It is one undo step and saved with the project. A new engine patch, `cmake/PatchMidiInputChannel.cmake`, filters the track's input node and recordings. Measured in the MIDI input suite ([contract](research/track-inputs-contract.md)).

## Audio From and MIDI From — 2026-10-23

UI area. A track's input is now the reference's two choosers. Audio From picks No Input, Ext. In or another track, with the input's channels or the track's tap under it. MIDI From picks No Input or a MIDI input (All Ins among them), with All Channels or a channel under it. The Session mixer's In/Out section grows a row for them, and the Arrangement header shows them side by side ([contract](research/track-inputs-contract.md)).


## Mixer laws — 2026-10-23

Engine, UI and root areas. From a static reading of the reference's code, track, group, return, Main and cue volume now follow its volume scale: −70 to +6 dB, 0 dB at 0.85 of the travel, interpolated in amplitude between 101 levels, and silent at the bottom. Sends follow its send scale, −70 to 0 dB. A new engine patch, `cmake/PatchFaderLaws.cmake`, lets Session install both laws (`src/LevelScale.h`). Faders, knobs, automation, MIDI controllers and rack chain controls move along them, so CC 64 is −13.84 dB as in the reference. Projects saved before are converted on load and keep every level; projects are now saved with `halftrackFaderLaw="2"`. Measured in the new fader-laws suite: the model's levels, stored positions, a rendered fader and send, CC levels, and a generated legacy project rendering as the project it came from. The automation and rack suites render ramps along the law ([contract](research/mixer-laws-contract.md)).

## Curved mixer automation reopens — 2026-10-23

Engine area. Loading refused a project whose track or Main volume or pan automation had a curved segment, because validation still required straight points; it now accepts curves from −1 to 1, as editing does. The automation suite saves a curved volume segment, reopens it and renders it as before, and still refuses curves beyond ±1 ([contract](research/arrangement-automation-contract.md)).

## Send automation — 2026-10-23

Engine and UI areas. Sends have automation lanes, as the reference's Send parameter does: `Session::setSendAutomation` and `SendInfo::automation`, in decibels from Off to 0 dB, moving along the send law. Turning an automated send overrides its lane until Re-Enable Automation. Automation recording captures send moves, making a send that was Off. Sends are MIDI and key mapping targets. The level set by hand is kept as `halftrackManualSend`, and loading validates send lanes. In Automation Mode the Mixer offers each send, named for its return. Measured in the sends, automation-capture, midi-mapping and UI suites ([contract](research/arrangement-automation-contract.md#send-lanes-2026-10-23)).

## Pan text — 2026-10-23

UI area. From a static reading of the reference's pan formatter, pans read C within 0.01 of the centre and otherwise 1 to 50 L or R, so a hard pan reads 50L where Halftrack read 100L. This covers the track, group and automation editors' pans, rack chains and Drum Rack pads; typed values read back the same way. Clip envelopes name the mixer's pan Track Panning, the reference's parameter name. Checked in the fader-laws and UI suites ([contract](research/mixer-laws-contract.md)).

## Split Stereo pan mode — 2026-10-23

Engine and UI areas. From a static reading of the reference's code, tracks and groups pan through its panner. In Stereo mode a hard pan keeps the far side at 1e-5 rather than silence. Split Stereo Pan Mode, from the pan's context menu, pans each input channel with Left Split Stereo and Right Split Stereo, blending over 75 samples. The switch is one undo step for every selected track. The split pans are automatable, mappable and clip-envelope targets, and the Session strip, Arrangement header and group strips show them in the pan's place. Measured in the fader-laws suite with a two-level stereo source, and checked in the UI suite ([contract](research/mixer-laws-contract.md#pan-modes)).

## Selection linking — 2026-10-23

Engine and UI areas. From a static reading of the reference's code: with several tracks selected, moving one's volume, pan, split pans, a send or its activator moves the others'. Volume and sends keep their decibel offsets along their scales, pans their travel offsets among tracks in the same pan mode, and activators copy. A track held at an end gets its offset back. Mouse, MIDI, key and capture moves all link, in one undo step, and a double-click resets every linked control to its default. Measured in the fader-laws suite and checked in the UI suite ([contract](research/mixer-laws-contract.md#selection-linking)).

## Group and Main device chains — 2026-10-23

Engine and UI areas. Groups and the Main have device chains of audio effects, as the reference's tracks do: a group's process its members' sum and the Main's the whole mix, before their faders. Session's device commands take a chain id (a track's, a group's or `Session::mainChain`), and device lookups by id search every chain. Clicking a group's strip or the Main's strip shows its chain, and + Device and the browser add to it. Measured in the groups suite with a Utility at −6 dB on a group and on the Main, with refusals, reordering, undo and a reopen; the UI suite adds a device to each ([contract](research/groups-contract.md#device-chains-2026-10-23)).

## Nested groups — 2026-10-23

Engine and UI areas. From a static reading of the reference's code, groups nest. Grouping tracks or groups puts the new group where the first of them is, inside its group if it has one; ungrouping moves the contents into the group around it. Group slots and group stop act on every track inside, at any depth. The Arrangement shows each group's header lane and the Session view its column, with outer folds hiding everything inside, and a selected group can be grouped or ungrouped. Measured in the groups suite with renders of three levels, and checked in the UI suite ([contract](research/groups-contract.md#nested-groups-2026-10-23)).

## Moving tracks between groups — 2026-10-23

Engine and UI areas. From a static reading of the reference's move and drop code, Arrangement headers drag tracks and groups into, out of and between groups (`Session::moveTracks`).
- Over a header's upper half they go before it. Over a group header's lower half they go into the group, first, and over a track's lower half they go after it. Below every lane they go after the last track.
- An orange line shows the place.
- Returns stay among the returns, and a group cannot go into itself. Grouping that would empty another group is now refused too, as the reference's rule has it. (A later change lets moves empty a group, as the reference's do.)
- Pressing the start of a track's name no longer folds the track.

Measured in the groups suite with renders, refusals and undo, and checked in the UI suite ([contract](research/groups-contract.md#moving-tracks-2026-10-23)).

## Header clicks arrive once — 2026-10-23

UI area. Arrangement headers, group headers and the Main's Session strip listened to their own mouse events to see their children's, and JUCE then delivers a component's own events twice. The fold corner therefore folded and unfolded the track in one click, and the wheel over a header's background scrolled twice. They now take their children's events through `ChildMouseEvents`, which passes on the children's only. Checked in the UI suite.

## Deleting groups and empty groups — 2026-10-23

Engine and UI areas. These follow a static reading of the reference's Delete and move code.
- **Deleting.** `Session::removeTracks` deletes tracks and groups in one undo step, a group with everything inside it. After a click on an Arrangement header's name or background, Delete deletes the selection, a selected group included, and selects the track now in the first one's place.
- **Empty groups.** Moves and deletions may now leave a group empty, as the reference allows. `GroupInfo::position` places every group's header, so both views show empty groups.
- **Display order.** The stem export and the export window list groups in display order: a group holding only a group now has its stem.

Measured in the groups and stems suites, and checked in the UI suite ([contract](research/groups-contract.md#deleting-2026-10-23)).

## Group menu rules — 2026-10-23

Engine and UI areas. The Create menu now enables Group Tracks and Ungroup Tracks by the reference's rules. Group Tracks is enabled whenever `Session::canGroupTracks` allows the selection, a grouped track included, since grouping it makes a group inside. Ungroup Tracks is enabled only with a group selected. Checked in the groups and UI suites ([contract](research/groups-contract.md#nested-groups-2026-10-23)).

## Group sends — 2026-10-23

Engine and UI areas. Groups have a send per return, as the reference's group mixers do (`Session::setGroupSendGain`, `GroupInfo::sends`). The sends tap before or after the group's fader as the return's Pre/Post setting decides. They are automatable and mappable, saved and validated on load, and removed with their return. The Session mixer's group strips show their knobs.

A soloed group now counts as soloed sources. Under Solo in Place the returns stay open, fed by the group's and its tracks' sends; before, soloing a group muted them.

Measured in the groups suite with renders, and checked in the UI suite ([contract](research/groups-contract.md#group-sends-2026-10-23)).

## Group automation and mapping — 2026-10-23

Engine and UI areas. A group's mixer is automated and mapped as a track's, as the reference's group mixer is the ordinary track mixer.
- `setTrackAutomation` takes a group's id, and `GroupInfo` reports the lanes.
- The `mixer:<group>:…` keys are mapping targets, and moving an automated group control overrides its lane.
- In Automation Mode a group's header shows the device and control choosers. Its lane draws and edits the envelope: its volume, pans, sends and devices.

Measured in the groups suite with renders, an override and a reopen, and in the automation-capture suite recording a group's pan and send; checked in the UI suite ([contract](research/groups-contract.md#group-automation-and-mapping-2026-10-23)).

## Time signature markers and bars — 2026-10-23

Engine and root areas. The meter can change along the timeline at time signature markers, as in the reference's Arrangement.
- **API.** Session inserts, re-meters, moves and deletes markers, each one undo step. `Session::barAt` and `barStart` count bars through them. Projects keep them, and MIDI clip files carry them.
- **Engine patch.** `cmake/PatchMeterBars.cmake` makes a bar numerator × 4 / denominator quarter notes, and lets each marker start a bar where it is, leaving the bar before it fragmentary.
- **Correction.** Until now a bar of 6/8 launched as six quarter notes, a 1/4 launch grid in 3/4 was three quarters of a beat, and plug-ins counted half the quarter notes in 6/8.

Measured in the time-signature suite (launches through engine callbacks), the metronome suite (accents across a marker) and the plug-in timing suite ([contract](research/time-signature-contract.md#time-signature-markers-2026-10-23)). The markers' Arrangement interface is next.

## Time signature markers in the Arrangement — 2026-10-23

UI area. While the meter changes, the ruler shows a lane of time signature markers under the bar numbers, as the reference's does.
- **Editing.** Insert Time Signature Change (Create menu and ruler menu) adds one and asks for its meter. Markers drag along the grid, move with the arrows, retype with Cmd/Ctrl+R or a double-click, and go with Delete.
- **Following the markers.** Bar numbers stand at the bars' starts, and fragmentary bars are crosshatched. The grid and snapping run from each bar's start, and the position fields count bars through the markers.

Checked in the UI suite ([contract](research/time-signature-contract.md#arrangement-interface)).

## Fragmentary bar commands — 2026-10-23

Engine and UI areas. As the reference's manual describes, Delete Fragmentary Bar Time and Complete Fragmentary Bar fix a bar a marker cuts short. Each deletes or inserts its missing time across the Arrangement, so the marker falls on a bar line; they are offered in the ruler's menu. Insert Silence and Delete Time now move time signature markers too, and Delete Time removes those inside it. Measured in the time-signature suite with a clip moving with the marker, and checked in the UI suite ([contract](research/time-signature-contract.md#fragmentary-bars-and-time-edits)).

## MIDI file time signatures — 2026-10-23

Engine and UI areas. As the reference offers, importing a MIDI file with time signatures asks whether to import them. They become markers from the import's beat, in the import's undo step (`importMidiFile(..., timeSignatures)`, `Session::midiFileTimeSignatures`). Checked in the time-signature and UI suites ([contract](research/time-signature-contract.md#importing-midi-files)).

## Returns reorder by drag — 2026-10-23

Engine and UI areas. As the reference's drop rules keep returns and other tracks apart, a return's Arrangement header drags among the returns only, before a return or after the last. Other tracks stay before the first return. `Session::moveTracks` takes returns for that and refuses mixing them. Checked in the groups and UI suites ([contract](research/groups-contract.md#moving-tracks-2026-10-23)).

## Group automation lanes — 2026-10-23

UI area. In Automation Mode a group header's + moves its envelope into a lane below the header, as a track's does. The lane draws and edits it, and its header hides it. Checked in the UI suite ([contract](research/groups-contract.md#group-automation-and-mapping-2026-10-23)).

## Plug-in values set by hand — 2026-10-23

Engine area. A plug-in parameter now keeps its value set by hand beside its automation, as mixer controls do, and returns to it when its automation is undone or cleared. The plug-in's saved state is written then too, because renders start from it.

Before, the parameter stayed where the automation last put it. Renders heard either value depending on when the engine's delayed save ran, which failed `plugin-automation-acceptance` once on Linux CI. Checked in the plug-in automation suite ([contract](research/plugin-automation-contract.md)).

## Tempo nudge — 2026-10-23

Engine and UI areas. The Control Bar has the reference's momentary Nudge Down and Nudge Up buttons, after a static reading of its code. While one is held during playback, the tempo ramps away from the song tempo by up to 70% over six seconds, in 16th-note steps. The song keeps the beats it gained. The song tempo, the project and its history stay unchanged, and nothing happens while stopped or under external sync. Saving, rendering, undo and tempo edits let the nudge go first. Measured in the tempo suite through the synthetic device, and checked in the UI suite ([contract](research/tempo-nudge-contract.md)).

## Session column drags — 2026-10-23

UI area. After a static reading of the reference's Session header row, a track's or group's column title drags it sideways: before a column over its left half, after it over its right half. The height in the title picks the nesting level, 6 pixels a level from the top. Returns drag among the returns, or after the last over the Main column's left half. Both views now place drops with one rule taken from the reference's drop target. Pressing a track selected with others keeps them for the drag in either view; before, the press dropped the others, so only one track could be dragged. Checked in the UI suite ([contract](research/groups-contract.md#moving-tracks-2026-10-23)).
