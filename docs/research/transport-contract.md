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
- **The loop brace**, as the reference's manual describes it (Arrangement View chapter, consulted 2026-10-23):
  - Dragging its bar moves the whole loop and keeps its length.
  - Dragging its start or end handle moves that end.
  - Both drags snap to the grid unless the grid is bypassed (Cmd on a Mac), and a loop keeps at least one grid step.
  - Clicking the brace selects the loop's time across every track, as the reference's Select Loop does, and selects the brace.
  - While the brace is selected, Left and Right move it by a grid step and Up and Down by its length. With Cmd/Ctrl, Left and Right shorten and lengthen it by a grid step, and Up and Down double and halve its length.
  - Shift-dragging the ruler still draws a new loop and turns it on.
  - Moving or resizing the brace leaves the loop's switch as it was.
  - None of this is an undo step: the reference keeps its loop out of the undo history (static reading of its code), as Halftrack keeps the punch switches. `Session::setLoop` changes the loop without an undo step and marks the project changed.
- **Set Song Start Time Here**, in the loop brace's context menu (static reading of the reference's code). While it is on, Play and Record from a stop start at the loop's start, the loop on or off, leaving the insert marker where it is. It is kept with the project and each change is one undo step (`Session::setLoopIsSongStart`, `Snapshot::loopIsSongStart`).
- **A locator as the song's start.** A locator's context menu offers its own Set Song Start Time Here (static reading of the reference's code). Play and Record from a stop then start at the locator. One thing holds the song's start at a time: setting it on a locator takes it from the loop brace or another locator, and the loop brace's item takes it from a locator. Moving the locator moves the song's start and deleting it clears it. It is kept with the project as `halftrackSongStart` on the locator, each change is one undo step, and a project with two song starts is refused (`Session::setLocatorSongStart`, `LocatorInfo::songStart`). The ruler marks the song's start with a small play triangle in the locator row.
- **Stop** leaves the play position where playback stopped. **Shift+Space** continues from there.
- **A second Stop** (the Stop button pressed while stopped) or **Home** returns the insert marker and play position to the start. Escape no longer stops: as the reference's, it jumps to the selected track's title bar (see the [menu contract](main-menu-contract.md#keys-outside-the-menus)).
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
- that Set Song Start Time Here makes Play start at the loop's start with the loop off and the insert marker left elsewhere, as one undo step;
- that a locator's Set Song Start Time Here takes the song's start from the loop, Play starts at the locator and follows it when it moves, another locator or the loop takes it back (one undo step), and deleting the locator returns Play to the insert marker;
- that the marker keeps its beat when the tempo halves.

`tests/TakeLanesTests.cpp` ends a loop recording with the Record button: playback carries on round the loop, and its 2.5 passes are cut into lanes as at Stop.

The main UI suite checks lane clicks while stopped and playing, Space, Shift+Space, the second Stop and Home. On a fixed quarter-note grid it checks:

- dragging the loop brace's bar two beats later;
- dragging its start and end handles to the nearest beats;
- a click selecting the loop's time across the tracks;
- Right, Down and Up moving the brace, and Cmd/Ctrl+Up, Down and Left doubling, halving and shortening it;
- that none of these is an undo step, and that the loop's switch is unchanged.

The Session suite checks that loop changes leave the redo history. It finds Set Song Start Time Here in the ruler's menu on the loop brace only, and switches it on and off; on a locator the menu offers the locator's own item instead, which makes the locator the song's start, marked in the ruler. The locator suite reopens a locator holding the song's start and refuses a project in which the loop holds it too. It presses the Record button: lit and waiting with Start Playback with Record off, then recording and playing, then ending the recording with playback running on.

Missing: quantized scrub jumps, holding in the scrub area to loop, the marker shown only on the clicked track, and checks of sounding notes and delay tails across jumps.
