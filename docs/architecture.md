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
