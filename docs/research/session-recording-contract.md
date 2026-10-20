# Session recording contract

Session recording follows the reference workflow of recording into an empty clip slot of an armed track: the take starts and ends on the launch grid and the new clip loops and plays as soon as recording ends ([recording new clips: Session View](https://www.ableton.com/en/live-manual/12/recording-new-clips/), consulted 2026-10-20). The descriptions are public workflow documentation. They are not measured identity with the reference application.

## Interface

`recordSlot(track, scene)` records the track's assigned, armed input into an empty slot.

- Unarmed tracks, occupied slots, Arrangement recording and export are rejected without changing state.
- With the transport running, the take starts at the next global launch boundary. From a stopped transport it starts at once and the transport starts.
- The track's other clips stop where the take begins.
- `SlotInfo::recording` reports the slot while it records.
- Calling `recordSlot` again on the recording track ends the take at the next boundary and plays it in phase from that boundary.
- `pressSlot` routes an armed track's empty slot to `recordSlot`, and any slot button on a recording track ends the take. A different filled slot then launches normally while the take stays silent.
- `stopLauncherTrack`, `stopAllLauncherClips` and transport stop also end the take and keep it silent. Transport stop ends it immediately.

The finished take:

- spans whole 1/960 beats from punch-in to stop (at least one beat) and loops over that length;
- is undone in one step;
- is a MIDI clip whose recorded notes get Halftrack note identities. A MIDI take with no events is an empty clip of the take's length.
- is a tempo-following audio clip at the project tempo for audio inputs, with its recording file in the project's Assets.

In the interface, an armed track's empty slots show a record circle, and a recording slot blinks red with "Recording".

## Implementation

The facade targets the input at the slot and arms that destination. It applies the destination to the graph synchronously before the transport starts, then starts a punch-in recording through the engine's `prepareToRecord`/`startRecording`, with the punch time converted from the grid boundary's monotonic beat. Ending a take uses the engine's asynchronous `stopRecording` at the boundary's unlooped time. A completion step (idempotent, so a transport stop overtaking a pending grid stop finishes the take once) normalizes the clip and queues it with `LaunchHandle::play` at the past boundary, which the engine plays in phase. Events before the punch-in are excluded by the engine.

## Evidence

`tests/MidiInputTests.cpp` drives a generated virtual MIDI controller through the synthetic device at 120 BPM with a one-beat grid. It checks:

- the rejections;
- a punch-in on the next beat that excludes an earlier key;
- two keys at exactly 0.2 and 1.2 beats from the punch-in with their lengths;
- a two-beat take that plays at once, stops with its track, and is undone and redone in one step;
- recording from an empty slot's launch button;
- a transport stop that keeps a one-note take silent;
- recording from a stopped transport, which starts playback.

`tests/SessionTests.cpp` records the synthetic device's generated audio input into a slot: a one-second, two-beat, tempo-following take in the project Assets that contains the signal and plays. A UI check records and ends a take from the grid buttons.

## Not implemented

- MIDI overdub into an existing clip.
- The global Session Record button and scene recording.
- Fixed-length recording.
- Count-in for slot recording.
- Recording while the transport loops.
- Recording quantization of slot takes.
- Capture MIDI.
- Any comparison with reference recording timing.

Assigning an input, arming a track or changing monitoring keeps the transport and launched clips playing (see [immediate MIDI input activation](../architecture.md#immediate-midi-input-activation)).
