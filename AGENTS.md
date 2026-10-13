# Halftrack engineering rules

## Product and authorization

Halftrack is an independently implemented open-source DAW with its own visual identity. Original source and assets are AGPL-3.0-only. C++20, JUCE 8 under AGPLv3, Tracktion Engine under GPL, and CMake are the shared foundation. macOS comes first; Windows and Linux must stay buildable.

The first runnable preview was handed off. The latest user instruction explicitly authorizes renewed work, extensive reverse-engineering research, and local decompilation of the installed reference application. The previous preview gate and no-inspection rule are superseded for this work. The user requests the closest achievable behavior, including investigating exact-output differences; never promise unmeasured zero differences.

## Independent implementation

Local read-only binary inspection, disassembly and decompilation are now authorized by the owner. Retain exact hashes, architecture, tool versions, addresses and uncertainty in evidence. Use isolated copies/projects and keep raw binaries, extracted material and decompiler output in ignored local artifacts. Public commits contain original Halftrack implementations, generic tools, tests and factual research summaries. Keep third-party code/assets and their rights distinct from Halftrack's AGPL license. Do not describe binary-assisted work as a clean-room process.

Reference product names may identify research subjects and measurements in documentation; Halftrack keeps its own identity. Dependency source retains its original notices. Historical commits are not rewritten to disguise earlier research. Do not silently expand account, hardware or cloud access while researching desktop behavior. Record any unresolved compatibility scope in docs/open-questions.md.

## Architecture and audio

Engine owns the model and Session facade; UI and persistence consume the documented interface in docs/architecture.md. Use exact dependency pins. No speculative abstraction or separate platform application. Use Signalsmith Stretch (MIT) for stretching; do not introduce Rubber Band without explicit permission. VST3 and macOS AU use JUCE; CLAP requires an explicit host adapter. Plug-in scanning runs in a separate process.

Owned audio callbacks must not allocate, lock, perform file I/O, log, or make system calls. Use preallocated lock-free FIFOs for direct UI-to-owned-DSP messages. Tracktion model edits stay on the message thread and use its supported graph update mechanism; audit this separately instead of claiming the whole dependency or arbitrary plug-in callback is proven lock-free. Every owned DSP component needs an offline-render test against a known-good output. Keep callback lifetime and device/editor lifetime explicit.

Protect user sessions and audio. Tests generate their own fixtures and never capture the microphone. Stop and drain recording before replacing project state. Collect media into a portable project-owned directory. Save atomically; reject invalid loads without replacing the current project. Never overwrite existing user projects or exports without their UI overwrite choice.

## Ownership and working style

- Engine agent: src/Session.*, src/SubtractiveSynth.*, tests/MidiTests.cpp; model API in docs/architecture.md.
- UI agent: src/MainComponent.*; original native dark interface and piano editor.
- Root/persistence: versioned project-format contract and persistence tests, src/Main.cpp, CMakeLists.txt, packaging, repository policy/docs and integration. Coordinate before editing Session persistence functions.
- QA agent: CI, real-time audit, manual test script, independent validation. Coordinate with engine before changing engine tests.

Each agent makes small reviewable commits only for its own files. Record cross-area edits in docs/progress.md before making them. Root publishes commits. Do not stage another agent's work. No unrelated cleanup. Add behavior tests with each feature; no fake meters, placeholder DSP, or controls without actual behavior. Keep CI green and distinguish build, automated signal tests, GUI checks, and physical hardware measurements.

Record ambiguous requirements in docs/open-questions.md with the simplest reasonable default, then continue. Keep docs/architecture.md and docs/progress.md current at task completion. Before committing, check for generated outputs, local paths, personal inventories, user recordings, secrets, and inaccurate completion claims.
