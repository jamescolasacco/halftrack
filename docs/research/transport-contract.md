# Transport contract

Halftrack's Play, Stop, Continue and locate follow the reference's [Arrangement View chapter](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-21. The insert marker decides where playback starts.

## Behaviour

- **The insert marker** is the engine's transport start position. It is not an undoable edit and does not mark the project changed. Tempo changes keep its beat.
- **Placing it.** Clicking an Arrangement lane places the insert marker, snapped to the grid unless Alt is held. A time selection puts it at the selection's start. While stopped, the play position moves with it; while playing, playback runs on.
- **Play** (Space or the Play button) starts from the insert marker. **Record** does too, unless the transport is already playing.
- **The Record button** (and F9) follows the reference's Arrangement Record button and its manual's Recording chapter (consulted 2026-10-22):
  - Pressed while recording, the recording ends where it was pressed and playback carries on (`Session::stopRecording`). Its takes are kept, as at Stop, loop passes included.
  - Pressed while playing, it records from there.
  - Pressed while stopped, it records and plays from the insert marker. With **Start Playback with Record** off, it instead lights and waits: "recording will not start until the Play button is pressed or Session clips are launched". Play then records from the insert marker; Continue and a Session launch record from where the transport starts. Pressed again, or Stop, it stops waiting.
  - Shift reverses the preference, as the manual says ("holding Shift while pressing Arrangement Record will engage the opposite behavior").
  - `Session::recordButton (shift)` does all this; `Session::record` presses it without Shift; `Snapshot::recordWaiting` reports the wait.
- **With the loop on** (static reading of the reference's code), Play and Record still start from the insert marker. From before the loop the play head rolls into it and loops once inside; from at or past its end it plays on and never loops. A jump while playing goes where it is asked and behaves the same way. Before, the engine jumped to the loop's start, and clamped jumps into the loop ([dependencies](../dependencies.md#tracktion-loop-playback-patch)).
- **Stop** leaves the play position where playback stopped. **Shift+Space** continues from there.
- **A second Stop** (the Stop button pressed while stopped) or **Home** returns the insert marker and play position to the start. Escape also stops and returns to the start.
- **Locating** (the ruler, the position fields, locators, `Session::seek`) moves the play position and the insert marker together, jumping playback if it is running. It relocates the play head even when the engine's cached position already reads the target. That cache follows the play head on a timer, and offline it does not move at all.
- **Edit points.** Pasting clips, new MIDI clips, splitting MIDI clips (Cmd+E) and importing without a drop position use the insert marker.

## API

`Session::play`, `continuePlayback`, `stop`, `record`, `recordButton`, `stopRecording`, `seek`, `setInsertMarker` and `returnToStart`; `Snapshot::insertMarker`, `Snapshot::position` and `Snapshot::recordWaiting`; `Preferences::startPlaybackWithRecord`.

## Evidence

`tests/TransportTests.cpp`, through the synthetic device, checks:

- placing the marker while stopped and while playing;
- Play from the marker, and Stop leaving the play position;
- Continue, and Play returning to the marker;
- locating while playing, and Return to Start;
- that none of these is an edit;
- that recording starts at the marker;
- that a recording started and stopped before the device's next block leaves the transport stopped;
- that Record pressed while recording ends a take of the length recorded, with playback running on, and pressed while playing records from there;
- that with Start Playback with Record off, Record waits without playing; pressed again or Stop ends the wait; Play records from the insert marker; Shift reverses the preference either way; and launching a Session clip starts the waiting recording;
- that with the loop on, playback from before the loop plays from the insert marker and then loops; from past its end it plays on; and jumps before and past it do the same;
- that the marker keeps its beat when the tempo halves.

`tests/TakeLanesTests.cpp` ends a loop recording with the Record button: playback carries on round the loop, and its 2.5 passes are cut into lanes as at Stop.

The main UI suite checks lane clicks while stopped and playing, Space, Shift+Space, the second Stop and Home. It presses the Record button: lit and waiting with Start Playback with Record off, then recording and playing, then ending the recording with playback running on.

Missing: quantized scrub jumps, holding in the scrub area to loop, the marker shown only on the clicked track, and checks of sounding notes and delay tails across jumps.
