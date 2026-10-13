# Halftrack progress

Updated 2026-10-13. Current milestone: one runnable preview including audio and MIDI creation; owner approval remains pending. The numbered roadmap supplied later is not an active phase restriction.

## Working and evidenced

- Repository/folder named Halftrack, original SVG identity, AGPL-3.0-only source, pinned C++20/JUCE 8/Tracktion build.
- Native audio arrangement, generated-source waveforms, playback, mixer gain/pan/mute/solo, input assignment/monitoring, recording, owned media, atomic XML projects, VST3 hosting, offline WAV export.
- Combined local Mac acceptance suite passed in 15.47 seconds after MIDI/FIFO, editor-handler and export-guard integration. Includes synthetic recording, controlled VST3 state, portable project/media, rendered mixer samples, cancellation preserving an existing destination, tail duration, gesture undo, startup dirty-state regression.
- Earlier GUI check: four generated stems imported, playback position/meters advanced, CoreAudio output setup visible, Fit and unsaved Discard worked. No microphone was captured.

## In progress

- Engine MIDI tracks/clips/notes and built-in polyphonic synth pass deterministic DSP, undo, saturated FIFO and persistence tests. Export reentrancy now rejects edits/project replacement while the worker owns its graph, with a cancellation/preservation regression.
- UI: piano roll and synth editing compile. Browser relaunch is verified. Actual editor handler and hit-test regressions pass. Native automation grid input is inconclusive; physical mouse delivery remains unverified.
- Persistence/root: MIDI project round-trip checks, architecture/ownership contract, branding/research retirement, packaging and Signalsmith selection.
- QA: Linux CI, callback audit, manual test script; previous audio-only Windows CI passed; combined three-platform run pending. First clean Mac CI exposed an older compiler in macOS 14; workflow now targets macOS 15 and requires AppleClang 17+.

## Missing, not stubbed controls

Session launch grid/scenes, full MIDI expression, automation, advanced routing/racks, full native effect/instrument inventory, CLAP hosting, full stretch/warp editor, hardware stress tests, crash recovery, and cross-platform GUI certification are not complete. No completion claim is based on a control being drawn. Existing unsupported controls are omitted rather than wired to fake processing.

## Known issues / verification remaining

- Browser relaunch and GUI audio save/reopen/export passed. MIDI grid input through native automation and plug-in editor lifetime checks remain pending.
- Whole-host strict callback safety is not met: the dependency has locks and recording fallback allocations; see docs/realtime-safety.md.
- Windows/Linux results and final clean Mac build are pending.
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

Local acceptance and production editor-handler tests passed in 15.47 seconds. Native grid mouse delivery remains inconclusive through automation. The three-platform CI run is [38445146571](https://github.com/jamescolasacco/halftrack/actions/runs/38445146571); at handoff macOS had compiled and was testing, while Windows/Linux builds were running. CI is not yet declared green. The complete feature sweep remains unfinished and awaits draft feedback. No additional implementation is underway.
