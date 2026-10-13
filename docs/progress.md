# Halftrack progress

Updated 2026-10-13. Current milestone: preview handed off; renewed implementation and local reverse-engineering research authorized. The numbered roadmap supplied later is not an active phase restriction.

## Working and evidenced

- Repository/folder named Halftrack, original SVG identity, AGPL-3.0-only source, pinned C++20/JUCE 8/Tracktion build.
- Native audio arrangement, generated-source waveforms, playback, mixer gain/pan/mute/solo, input assignment/monitoring, recording, owned media, atomic XML projects, VST3 hosting, offline WAV export.
- Combined local Mac acceptance suite passed in 15.47 seconds after MIDI/FIFO, editor-handler and export-guard integration. Includes synthetic recording, controlled VST3 state, portable project/media, rendered mixer samples, cancellation preserving an existing destination, tail duration, gesture undo, startup dirty-state regression.
- Earlier GUI check: four generated stems imported, playback position/meters advanced, CoreAudio output setup visible, Fit and unsaved Discard worked. No microphone was captured.

## In progress

- Engine MIDI tracks/clips/notes and built-in polyphonic synth pass deterministic DSP, undo, saturated FIFO and persistence tests. Export reentrancy now rejects edits/project replacement while the worker owns its graph, with a cancellation/preservation regression.
- UI: piano roll and synth editing compile. Browser relaunch is verified. Actual editor handler and hit-test regressions pass. Native automation grid input is inconclusive; physical mouse delivery remains unverified.
- Persistence/root: MIDI project round-trip checks, architecture/ownership contract, branding/research retirement, packaging and Signalsmith selection.
- QA: Linux CI, callback audit, manual test script; combined preview Mac and Windows CI passed; Linux failed at linking and a focused libatomic correction is in verification. First clean Mac CI exposed an older compiler in macOS 14; workflow now targets macOS 15 and requires AppleClang 17+.

## Missing, not stubbed controls

Session launch grid/scenes, full MIDI expression, automation, advanced routing/racks, full native effect/instrument inventory, CLAP hosting, full stretch/warp editor, hardware stress tests, crash recovery, and cross-platform GUI certification are not complete. No completion claim is based on a control being drawn. Existing unsupported controls are omitted rather than wired to fake processing.

## Known issues / verification remaining

- Browser relaunch and GUI audio save/reopen/export passed. MIDI grid input through native automation and plug-in editor lifetime checks remain pending.
- Whole-host strict callback safety is not met: the dependency has locks and recording fallback allocations; see docs/realtime-safety.md.
- Combined preview Mac and Windows CI passed. Linux link correction is pending a new CI result.
- No measured physical input latency, acoustic loopback, deadline stress, or arbitrary plug-in real-time safety evidence.
- Existing PCM tests exclude opening/closing ramp regions; they do not prove complete-file numerical identity or nonlinear processor parity.

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

The owner explicitly superseded the earlier no-decompilation rule and preview pause. Parallel research covers tooling, vetted agent skills and measurable audio/workflow parity. Root begins read-only Mach-O inventory of the installed reference and an isolated official Ghidra setup. Raw target material stays in ignored local artifacts. No claim of source recovery, completed clone or zero differences is made.

## Panning correction ownership and verification

The parity-research agent has root-assigned ownership of `src/Session.cpp` and `tests/SessionTests.cpp` for the panning correction. This is a coordinated edit to the engine/mixer area. New tracks will use an explicitly serialized constant-power mixer with unity center; existing serialized mixers retain their prior sound. No proprietary source or assets are used. Focused offline-render matrix tests and combined acceptance results will be recorded below after execution.

Root integration: three primary-source research reports now cover tool selection, inspected skills and behavioral measurement. Official Ghidra 12.1.4 archive SHA-256 matched the published digest; macOS ARM64 native components built successfully using JDK 21 and the supplied Gradle wrapper. The isolated ARM64 reference copy imported successfully with automatic analysis disabled and no dependent libraries imported. Local metadata inventory records both slices and embedded debug-section absence. Decompilation coverage is not yet claimed.

Original Mach-O tooling has 20 passing synthetic tests (45a8eb9). The actual reference inventory succeeded. Linux atomic linkage correction is a93b813; both are pushed. Root owns shared documentation; agents own their respective tool, mixer and CI changes. Raw reference artifacts remain ignored.

The panning task also has root-approved ownership of the narrow callback-root addition in `tests/realtime_audit.py`. The new multiplier is audited as owned source; the inherited engine mixer remains an explicitly excluded dependency call with its documented lock limitation.

Panning correction verification: the combined Release acceptance suite passes in **48.01 seconds** on Apple Silicon macOS. New audio/MIDI tracks serialize `halftrack-mixer`, which retains the engine's volume/pan automation and smoothing, selects its sine/cosine law, and multiplies the first two audio channels by sqrt(2). At user gain g and pan p, the diagonal gains are g·sqrt(2)·cos((p+1)·pi/4) and g·sqrt(2)·sin((p+1)·pi/4); center is unity and extremes are +3.0103 dB. Master behavior and existing serialized `volume` mixers are unchanged. Legacy projects intentionally retain their linear panning; this correction does not retroactively migrate their sound.

The generated stereo matrix test covers 21 pan positions, -12 dB and maximum +6 dB engine fader gain, exact zero/mute silence, solo, mute/solo/pan undo and redo, and both new-type and legacy save/reopen. Every rendered fixture sample is compared, with silent margins allowing the existing graph to settle; this does not characterize active clip-edge ramps. Those tests exposed an existing mute/solo undo omission: their engine cached values used no undo manager. The Session facade now writes the same state properties with the edit's undo manager, verified by state and rendered output. The source callback audit passes **9 self-tests and 17 owned callback/helper bodies**, while explicitly excluding the inherited engine mixer's known locks from its guarantee.

## Verified local analysis and mixer correction

The isolated ARM64 reference imported and seven explicitly selected utility-processing entries decompiled successfully with per-address manifests; no whole-program or zero-difference claim is made. The reference's 392,926 declared ARM64 function-start addresses exactly match Apple's independent LLVM inventory. Original binary SHA-256 is unchanged after analysis. See [local research results](research/reverse-engineering-results.md) for versions, scope and private artifact locations.

Commit c805766 passes the full local acceptance suite in 48.01 seconds, including 35 complete-window mixer renders with a worst PCM error of 1.28e-7 against independent math. New tracks get normalized sinusoidal panning; existing project tracks preserve legacy behavior. Mute/solo now undo correctly. Callback audit passes nine self-tests / seventeen owned bodies, with inherited dependency locks still explicitly unresolved. Final combined CI and portable package refresh remain pending.
