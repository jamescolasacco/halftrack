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
- While any take records, `setTrackInput` is refused on every track with "Stop recording before changing inputs.", and no device is enabled. Arming and monitoring stay available and keep the take.
- A MIDI device that appears during a take is discovered after the take ends.
- While any take records, undo and redo are unavailable: `canUndo` and `canRedo` are false and `undo` and `redo` do nothing. Audio export, stem export, freeze and flatten are refused with the messages Arrangement recording gives ("Stop recording before exporting.", "Stop recording before freezing tracks.", "Stop recording before flattening tracks.").
- Edits that would delete or replace the take's slot are refused for that slot only. The recording track cannot be removed ("End this track's Session recording before removing it."). A scene holding a recording slot cannot be deleted ("End this scene's Session recording before deleting it."). A recording slot accepts no new MIDI clip or imported audio ("End this slot's recording before adding a clip.").
- Other edits keep the take. These are removing other tracks, and reordering, duplicating, grouping and ungrouping tracks, the recording track included. They also include deleting other scenes, moving, duplicating and adding scenes (the take moves with its slot), clearing slots, MIDI import, MIDI clip export and saving. A project saved during a take holds the state before the take ends. The engine drops the slot's temporary recording destination when the project is loaded.
- New Project ends takes in the replaced project, as Open does.

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

A second MIDI input test records a MIDI take and then an audio take from the generated audio input. During the MIDI take it tries three input changes: assigning a disabled controller, assigning the disabled generated audio input and disconnecting the recording track's input. During the audio take it tries assigning a disabled controller. A virtual MIDI device is also added during each take. Each change is refused without enabling a device, and each take keeps recording. Arming and monitoring another track also keep the MIDI take. The MIDI take keeps all four keys played across these changes, and the audio take holds the generated input for its full 40,960 samples. Discovery of each added device waits until its take ends. Before the fix, the take was lost and its slot stayed recording after the take was ended. With neither part of the fix, the first refusal check fails. With only the rescan patch, the device-enablement check fails, and with only the input guard, the discovery check fails.

A third MIDI input test, `editsDuringSessionTakes`, gives each group of edits a fresh project with a MIDI take from the generated controller. It plays one key before and one after the edits, then ends the take. After each allowed edit the take must end with both keys. Each refused edit must fail while the take keeps recording. A generated audio take checks the import refusal, and a last case starts a new project during a take. The test was first run on the unfixed code with failures logged rather than thrown:

- After three undos (through the launch grid, the arm and the input assignment), ending the take produced no clip.
- Removing the recording track or deleting its scene deleted the slot while it recorded, and the take was lost.
- Exporting audio or stems, freezing a track and flattening a frozen recording track each lost the take.
- Creating a MIDI clip in the recording slot, or importing audio into it, was accepted. The finished take then replaced that clip.
- After a new project, input assignment stayed refused because the old take was never ended.

On the unfixed code, reordering, duplicating, grouping and ungrouping tracks, clearing slots, MIDI import and saving kept the take. Removing other tracks and the other scene edits ran there only after the take was lost. Their code is unchanged, and they keep the take in the fixed run. With the fixes every check passes.

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

Assigning an input, arming a track or changing monitoring keeps the transport and launched clips playing (see [immediate MIDI input activation](../architecture.md#immediate-midi-input-activation)). That section also explains why input changes are refused while a take records. Audio-device changes during a take and physical MIDI hot-plug are not covered (see [open questions](../open-questions.md#input-changes-during-session-takes)). Some edits that are refused only during Arrangement recording have not been audited for Session takes: seeking, arming or monitoring the recording track itself, Arrangement clip and time edits, automation, follow actions and recording settings (see [open questions](../open-questions.md#edits-during-session-takes)).
