# Halftrack architecture

## Shared application

One C++20 JUCE application runs on macOS, Windows, and Linux. Tracktion owns edit/playback state, graph scheduling, streaming, recording, and rendering. Halftrack owns commands, its interface, project envelope, and built-in DSP. This is an independent implementation with original assets.

`MainComponent` consumes the message-thread-only `Session` facade. Snapshots are value copies for the UI. Position and audio-clip duration use seconds; MIDI note times use beats as specified below. IDs are persisted strings; array indexes are short-lived lookup conveniences, not persistent identity.

## Persistence ownership

The human-readable XML envelope is `<HALFTRACK version="1">`, containing one engine `<EDIT>` tree. Engine state is the single model for track, clip, note, parameter, and undo operations. Built-in synth settings live in its plug-in state. Save flushes engine state, copies it, collects audio into a sibling `<project> Assets` directory, writes a temporary project, then atomically replaces the target. Referenced media paths become relative to the project. MIDI clips do not require external assets.

Load parses and validates the envelope into a candidate edit before replacing the active one. Unsupported versions, missing media, and unavailable synchronous external plug-ins produce errors while preserving the active project. Monitoring/arming are reset on load to prevent accidental capture. Unknown asynchronous plug-in failures still need a stronger recovery story; no universal crash-recovery guarantee is made.

Undo/redo uses the engine UndoManager. Continuous UI gestures call `beginEditGesture` / `endEditGesture`; one drag is one undo. Snapshot dirty state combines engine notifications with an explicit user-edit flag because deferred engine initialization can otherwise clear a just-made edit.

## Threads and boundaries

Project commands and editor lifetime stay on JUCE's message thread. The engine supplies its graph update mechanism; this is not described as a blanket lock-free guarantee. Owned DSP accepts direct control changes through preallocated SPSC queues. The separate QA report states the tested and unverified callback paths. File-browser enumeration uses bounded detached filesystem-only workers with shared results; no worker retains a component or JUCE object. The UI polls current results without waiting for blocked filesystem calls.

Offline export constructs/releases the graph on the message thread, executes the render job on a worker, pumps a modal progress interface, and cooperatively cancels. A cancelled render never replaces the destination. Physical-driver timing and arbitrary third-party DSP remain outside automated synthetic-device evidence.

MIDI-device discovery is deferred while any engine context records. The pinned dependency previously cleared active playback contexts during discovery and discarded captured takes. `cmake/PatchTracktion.cmake` adds a message-thread recording guard with a 100 ms retry, covering automatic discovery and explicit rescans without changing the persisted scan interval. Discovery resumes after stop. The synthetic recording regression deliberately crosses the initial four-second scan deadline, requests another rescan during capture, and verifies both retained audio and post-stop discovery. This does not certify physical device removal or every graph rebuild path. See [the exact patch contract](dependencies.md#tracktion-recording-safety-patch).

`Session::isExporting()` stays true until the render worker and graph scopes have released the edit. During this interval, project, track, clip, note, parameter and transport mutations reject or do nothing; undo/redo and recursive export are also blocked. Read-only snapshots and `cancelExport()` remain available. The UI also guards native menus, keyboard dispatch and application close because a modal progress window still pumps native events. This prevents a reentrant command from replacing a graph that the worker is using.

## Platform seams

JUCE provides CoreAudio/AU on macOS, ASIO/system audio on Windows, and the available Linux audio backends. VST3 is shared. CLAP is planned, not enabled. Separate-process plug-in discovery is configured through the engine. The project format does not translate an AU state into a VST3 or CLAP state.

## MIDI interface

The engine agent maintains this section alongside `src/Session.h`.

- `TrackInfo.isMidi` identifies an instrument track. `TrackInfo.synth` contains `SynthSettings`: cutoffHz, resonance, attackSeconds, decaySeconds, sustain, releaseSeconds, level, and waveform (0 saw, 1 square).
- `ClipInfo.isMidi`, `startBeat`, `lengthBeats`, and `notes` supplement the existing seconds-based arrangement coordinates. A `MidiNoteInfo` contains stable `id`, MIDI `pitch` (0–127), clip-relative `startBeat`, `lengthBeats`, and normalized `velocity` (0–1).
- `addMidiTrack(name)` creates a track and built-in instrument with no clip. `addMidiClip(trackId, startBeat, lengthBeats)` creates an empty musical clip.
- `addMidiNote(clipId, pitch, startBeat, lengthBeats, velocity)`, `updateMidiNote(clipId, noteId, pitch, startBeat, lengthBeats, velocity)`, and `removeMidiNote(clipId, noteId)` edit actual engine notes by persisted IDs. Invalid IDs or out-of-clip notes return an error. Updates support the existing gesture boundary methods for one undo per drag.
- `setSynthSettings(trackId, settings)` edits the instrument state and participates in undo/redo. Snapshot reads and commands stay on the message thread. MIDI clips follow tempo changes; note durations remain in beats. Existing gain/pan/mute/solo and external effects apply to instrument tracks.

The built-in instrument uses sixteen preallocated voices, band-limited saw/square oscillators, a resonant low-pass filter, and an amplitude envelope. Direct parameter packets cross a preallocated SPSC FIFO and are consumed only at audio block boundaries. If the FIFO fills, the producer retains the latest state and retries outside the callback. The synth callback does not access the model, allocate, take locks, or perform I/O. The surrounding dependency graph is audited separately. MIDI keyboard audition and external MIDI-device capture are not exposed until their real scheduling paths exist.

## Reverse-engineering evidence boundary

Research tools live under `scripts/`; they are not linked into the application or invoked on the audio thread. `inspect_macho.py` reads universal/thin arm64 and x86_64 Mach-O metadata with strict bounds checks and records exact hashes, sections, symbol classes and declared function starts. Its fixtures are original synthetic binaries. Named text symbols are candidates, not proven function boundaries. Output files are create-only.

Reference copies, metadata inventories, persistent Ghidra projects and pseudocode stay under ignored `artifacts/reverse-engineering/`; local tool distributions stay under `.deps/reverse-tools/`. Selected static results become independently written behavior hypotheses, then observable tests and original application changes. Analysis coverage, render equivalence and hardware timing are separate evidence. A recovered function count never becomes a completed-feature count. See the [toolchain](research/reverse-engineering-toolchain.md), [vetted skills](research/reverse-engineering-skills.md) and [measurement design](research/behavioral-parity-measurement.md).

## Track mixer normalization and compatibility

New audio/MIDI tracks use the registered `halftrack-mixer` plug-in type, a narrow engine `VolumeAndPanPlugin` subclass. It preserves engine automation, smoothing and fader range, selects the sinusoidal law, then multiplies the first two output channels by sqrt(2) for unity-center normalization. The master retains its existing type. Existing serialized `volume` mixers retain their previous linear law, including after save/reopen; there is no automatic project migration. Mute/solo changes write the engine's state properties through its UndoManager so these edits participate in undo/redo.

The mixer callback's own bounded multiplication is included in the source audit. The inherited engine callback retains the separately documented locking limitation; this is not a whole-host real-time certification. Complete fixture-window render tests include silence and channel isolation; active clip-edge transients require their own comparisons.

`ExportSelectedFunctions.java` is a separate offline Ghidra script: explicit addresses, create-only output directory, finite preparation/decompile timeouts and per-address outcomes. Its outputs are local research evidence, never build inputs to Halftrack.
