# Halftrack progress

Updated 2026-10-13. Current milestone: one runnable preview including audio and MIDI creation; owner approval remains pending. The numbered roadmap supplied later is not an active phase restriction.

## Working and evidenced

- Repository/folder named Halftrack, original SVG identity, AGPL-3.0-only source, pinned C++20/JUCE 8/Tracktion build.
- Native audio arrangement, generated-source waveforms, playback, mixer gain/pan/mute/solo, input assignment/monitoring, recording, owned media, atomic XML projects, VST3 hosting, offline WAV export.
- Local Mac acceptance suite passed in 12.66 seconds on a6f7e2e. Includes synthetic recording, controlled VST3 state, portable project/media, rendered mixer samples, cancellation preserving an existing destination, tail duration, gesture undo, startup dirty-state regression.
- Earlier GUI check: four generated stems imported, playback position/meters advanced, CoreAudio output setup visible, Fit and unsaved Discard worked. No microphone was captured.

## In progress

- Engine: MIDI tracks/clips/notes, built-in polyphonic subtractive synth, deterministic DSP and undo tests.
- UI: piano roll and synth editing; browser launch stall fix built, relaunch verification pending.
- Persistence/root: MIDI project round-trip checks, architecture/ownership contract, branding/research retirement, packaging and Signalsmith selection.
- QA: Linux CI, callback audit, manual test script; current Windows CI ongoing. First clean Mac CI exposed an older compiler in macOS 14; workflow now targets macOS 15 and requires AppleClang 17+.

## Missing, not stubbed controls

Session launch grid/scenes, full MIDI expression, automation, advanced routing/racks, full native effect/instrument inventory, CLAP hosting, full stretch/warp editor, hardware stress tests, crash recovery, and cross-platform GUI certification are not complete. No completion claim is based on a control being drawn. Existing unsupported controls are omitted rather than wired to fake processing.

## Known issues / verification remaining

- New startup browser fix needs GUI retest. GUI save/reopen/export and editor lifetime checks remain pending.
- Windows/Linux results and final clean Mac build are pending.
- No measured physical input latency, acoustic loopback, deadline stress, or arbitrary plug-in real-time safety evidence.
- Existing PCM tests exclude opening/closing ramp regions; they do not prove complete-file numerical identity or nonlinear processor parity.

## Ownership log

Root revised repository policy and research/docs after the owner's new rules. Earlier branded research remains in history for transparency but is retired from the current tree; no history rewrite. Engine owns Session and synth; UI owns MainComponent; QA owns CI/audit/manual checks; root owns integration and persistence tests. Root will coordinate any edits to Session persistence functions with engine before writing them.
