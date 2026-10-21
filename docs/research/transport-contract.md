# Transport contract

Halftrack's Play, Stop, Continue and locate follow the reference's [Arrangement View chapter](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-21. The insert marker decides where playback starts.

## Behaviour

- **The insert marker** is the engine's transport start position. It is not an undoable edit and does not mark the project changed. Tempo changes keep its beat.
- **Placing it.** Clicking an Arrangement lane places the insert marker, snapped to the grid unless Alt is held. A time selection puts it at the selection's start. While stopped, the play position moves with it; while playing, playback runs on.
- **Play** (Space or the Play button) starts from the insert marker. **Record** does too, unless the transport is already playing.
- **Stop** leaves the play position where playback stopped. **Shift+Space** continues from there.
- **A second Stop** (the Stop button pressed while stopped) or **Home** returns the insert marker and play position to the start. Escape also stops and returns to the start.
- **Locating** (the ruler, the position fields, locators, `Session::seek`) moves the play position and the insert marker together, jumping playback if it is running. It relocates the play head even when the engine's cached position already reads the target. That cache follows the play head on a timer, and offline it does not move at all.
- **Edit points.** Pasting clips, new MIDI clips, splitting MIDI clips (Cmd+E) and importing without a drop position use the insert marker.

## API

`Session::play`, `continuePlayback`, `stop`, `record`, `seek`, `setInsertMarker` and `returnToStart`; `Snapshot::insertMarker` and `Snapshot::position`.

## Evidence

`tests/TransportTests.cpp`, through the synthetic device, checks:

- placing the marker while stopped and while playing;
- Play from the marker, and Stop leaving the play position;
- Continue, and Play returning to the marker;
- locating while playing, and Return to Start;
- that none of these is an edit;
- that recording starts at the marker;
- that the marker keeps its beat when the tempo halves.

The main UI suite checks lane clicks while stopped and playing, Space, Shift+Space, the second Stop and Home.

Missing: quantized scrub jumps, holding in the scrub area to loop, the marker shown only on the clicked track, and checks of sounding notes and delay tails across jumps.
