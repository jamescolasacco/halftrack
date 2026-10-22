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
- Seeking, including locator navigation, ends every take where the play head was and stops the transport, as during Arrangement recording. Each take keeps its keys and stays silent.
- The recording track cannot be disarmed during its take ("End this track's Session recording before disarming it."). Arming, disarming and monitoring other tracks, and monitoring the recording track itself, keep the take.
- Changing the recording track's instrument keeps the take. Arrangement clip edits (trim, reverse, split, duplicate, cut, copy, paste and move) and Insert or Delete Time keep it too, on any track. So do track, device and master automation, follow actions, count-in and recording quantization. Count-in and recording quantization do not apply to Session takes.
- An audio-device stop (an Audio Settings change, a driver restart, a removed device) ends Session and Arrangement takes where the device stopped, keeps them and stops the transport, as Stop does. Before this, the engine's device rebuild discarded the takes.

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

A fourth MIDI input test, `playbackAndArrangementEditsDuringSessionTakes`, uses the same fixture for the remaining edits that were refused only during Arrangement recording. It was also first run on the unfixed code with failures logged rather than thrown:

- A seek to 3 s during a take moved the play head while the slot kept recording. Ending the take after one more key gave a 6.34-beat clip, and that key lasted 5.19 beats where it was held for about 0.21 beats.
- Disarming the recording track was accepted. The slot kept recording on a track that reported itself unarmed, and the take kept both keys.
- Monitoring the recording track (on, automatic and off), replacing its instrument and restoring the built-in synthesizer kept the take. So did Arrangement audio trim, reverse, split, duplicate, cut, paste and move on other tracks, and MIDI duplicate, cut, paste and move on the recording track, followed by Insert Time and Delete Time. Volume and pan automation on the recording track, a device control on its delay, master volume automation, a follow action on its other slot clip, a two-bar count-in and quarter-note recording quantization all kept it as well.

With the fixes, the seek ends the take with both keys played before it, and the transport is stopped at 3 s. Disarming is refused, the monitored key is heard during the take (peak about 0.11), and the take ends with all three keys. Every edit in the last item keeps both keys, and its own change is in place afterwards.

`tests/SessionTests.cpp` records the synthetic device's generated audio input into a slot: a one-second, two-beat, tempo-following take in the project Assets that contains the signal and plays. A UI check records and ends a take from the grid buttons.

The same file runs four device-change scenarios: an Arrangement or slot take, then an input-channel change through the `AudioDeviceManager` or a same-layout driver restart. Each records 94 generated blocks before the change. Before the fix, all four lost the take: no clip after Stop, and slot takes still reported `recording`. Now each keeps a 23,808-frame take containing the signal (every block but the first), stops the transport, and records a further take on the rebuilt devices. The channel change is applied without storing it as the chosen setup, because the engine saves that to the shared test settings that later sessions load.

## Session Record and overdub

The control bar's Session Record button (`Session::sessionRecord (sceneIndex)`, on the selected scene) follows the reference's [recording chapter](https://www.ableton.com/en/live-manual/12/recording-new-clips/), consulted 2026-10-21:

- **First press.** Every armed track records a new clip into its empty slot in the scene, as a slot button does. An armed MIDI track whose slot holds a MIDI clip launches it (with the new takes from a stopped transport, otherwise on its launch grid) and overdubs.
- **Overdub.** Notes played into the armed track are added to the clip at its note-off. Each is placed where it was played in the clip's loop: the beats the clip has played less those from the note's stream time to the play head. Its length is the time it was held, at most the loop. A pass is one undo step, begun again if another edit came between its notes.
- **Second press.** New takes end on the launch grid and loop, and overdubbing stops while the clips play on. Stop, locating and replacing the project also end overdubbing.
- **Status.** `Snapshot::sessionRecording` lights the button, and `SlotInfo::overdubbing` marks an overdubbing clip, whose launch button shows a red record dot.
- **Skipped slots.** Audio tracks whose slot holds a clip, and unarmed tracks, are left alone.

`tests/SessionRecordTests.cpp` drives a generated controller and the generated audio input. It checks the following:

- a first press records MIDI and audio into the scene while an unarmed track's slot stays empty;
- a second press ends both takes on the bar, where they loop, with the played note in the MIDI take;
- overdubbing adds a note at the loop position where it was played (1.984 beats, at 1.984), undone in one step;
- a stopped clip launches to overdub while audio records anew.

The UI suite records and ends a take with the button.

## Takes started together

The engine starts the transport as soon as the first take of a press starts recording. So when one action starts several takes from a stopped transport, every take uses the position the transport stood at. Before this, each later take saw a running transport and waited for the launch grid; a MIDI take from beat 2.6 began a bar late and lost the note played at once. `tests/SessionRecordTests.cpp` presses Session Record from beat 2.6 and finds the note in the MIDI take beside the audio one.

## Start Recording on Scene Launch

The reference's Record, Warp & Launch option of this name is off by default. Its manual says that, off, launching a scene does not record into its empty record-enabled slots. With the preference on (`Preferences::startRecordingOnSceneLaunch`), `Session::launchScene`:

- records into the scene's empty slots on armed tracks, as Session Record does, after checking every input first;
- launches the other tracks' clips as before;
- leaves a track that is already recording into a slot alone, so its take ends with the launch as before, without a new take beginning (see [open questions](../open-questions.md#start-recording-on-scene-launch)).

`tests/SessionRecordTests.cpp` checks:

- that with the option off, launching an empty scene records nothing;
- that with it on, the armed MIDI and audio tracks record and the unarmed one does not;
- that the takes become looping clips, the MIDI one holding the note played;
- that launching a scene of clips afterwards plays them, recording nothing.

## Not implemented

- Audio overdub, and recording into the next empty slot when the scene's slot is full.
- Fixed-length recording.
- Count-in for slot recording.
- Recording while the transport loops.
- Recording quantization of slot takes.
- Any comparison with reference recording timing.

Assigning an input, arming a track or changing monitoring keeps the transport and launched clips playing (see [immediate MIDI input activation](../architecture.md#immediate-midi-input-activation)). That section also explains why input changes are refused while a take records. Audio-device changes during a take and physical MIDI hot-plug are not covered (see [open questions](../open-questions.md#input-changes-during-session-takes)). Loop and tempo changes during a take have not been audited (see [open questions](../open-questions.md#edits-during-session-takes)).
