# MIDI controller lane contract

MIDI clips carry controller lanes beside their notes, after the reference note editor's velocity lane and MIDI envelopes ([Editing MIDI](https://www.ableton.com/en/live-manual/12/editing-midi/), [Clip Envelopes](https://www.ableton.com/en/live-manual/12/clip-envelopes/), consulted 2026-10-20). This is Halftrack's own implementation on the engine's MIDI controller events. Lanes play as steps: each point's value holds until the next point. The reference draws envelope ramps between breakpoints; ramps are not implemented here.

## Interface

- `setMidiControllerLane(clipId, controller, points)` replaces one lane in one undo step; no points removes the lane. Lanes are:
  - CC 0–119, with values 0–127;
  - `pitchBendLane`, with values −8192 to 8191;
  - `channelPressureLane`, with values 0–127.
- Points are beats from the clip start, strictly increasing, inside the clip. A lane holds at most 16384 points.
- These are rejected:
  - channel mode messages (CC 120–127) and out-of-range values;
  - repeated or unordered beats, and points past the clip;
  - frozen tracks, unknown clips, and edits while recording or exporting.
- `ClipInfo::controllers` lists the lanes that have points, by controller number. Program changes and polyphonic aftertouch stay in the clip untouched and are not listed.

## Behaviour

- Lanes play through the engine's MIDI clip playback to the track's instrument. The built-in synthesizer bends ±2 semitones, native instruments use their bend range, and both honour the sustain pedal (CC 64).
- Insert Silence moves points from the insertion point on, like notes.
- Delete Time removes points inside the deleted time and moves later points earlier. The last value set inside the deleted time is kept at the cut, unless a later point lands there, so notes after the cut keep the controller state they had.
- Copy, duplicate, Session slots, saving and Standard MIDI File export/import keep the lanes.

## Piano roll

A lane sits under the notes. Its menu offers:

- Velocity, the default;
- Pitch Bend, Mod Wheel, Volume, Pan, Expression, Sustain and Pressure;
- any other controller the clip uses;
- an Other controller submenu with every CC;
- Hide lane.

Editing:

- In the velocity lane, dragging a note's bar sets its velocity. A bar on a selected note moves every selected note together.
- In a controller lane, clicking adds a point on the note grid (Alt bypasses the grid), and dragging paints points along the grid or moves an existing point between its neighbours.
- Double-click or right-click deletes a point.
- Each gesture is one undo step.

## Evidence

`tests/MidiControllerTests.cpp` renders a sine C3 from the built-in synthesizer and measures pitch from interpolated zero crossings. It checks:

- 261.626 Hz without a lane;
- 293.665 Hz after an upward bend point at beat 2;
- 233.082 Hz under a full downward bend, each within 0.05 Hz;
- a sixteenth note sustained by CC 64 until the pedal lifts at beat 3, then silent;
- the rejection rules;
- undo and redo;
- controller ordering;
- Insert Silence and Delete Time;
- reopening;
- a MIDI file round trip;
- duplication.

The UI suite drags a velocity bar, draws and deletes a pitch-bend point, and hides the lane.

## Not implemented

- Ramps between breakpoints and curved segments.
- Unlinked envelope loops.
- Envelopes for device parameters in clips.
- Release velocity and note probability.
- Recording-time thinning.
- Any comparison with reference output.
