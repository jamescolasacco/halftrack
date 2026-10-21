# Tempo automation contract

The project tempo can change over the timeline, after the reference's Song Tempo automation on the Master track ([Automation and Editing Envelopes](https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/), consulted 2026-10-20). This is Halftrack's own implementation on the engine's tempo sequence. Ramps are linear between points.

## Interface

- `setTempoMap(points)` replaces the tempo map in one undo step.
  - Requirements: the first point at beat 0, beats increasing at least a 960th apart, 20–999 BPM, at most 4096 points.
  - The tempo ramps linearly from each point to the next and holds after the last.
  - One point is a constant tempo.
  - Clips, notes, automation and the loop keep their beats; unwarped audio clips keep their start beat and play at their own speed.
- `Snapshot::tempoMap` reports the points, and `Snapshot::tempo` is the tempo at the playhead.
- `beatsAt(seconds)` and `secondsAt(beats)` convert through the map. The interface uses them for:
  - position and loop fields;
  - ruler bars and grid lines;
  - snapping and clip creation;
  - the piano roll playhead;
  - the automation editor's view.
- While the tempo is automated:
  - `setTempo`, the tempo field and tap tempo step aside; the field shows the tempo at the playhead.
  - Insert Silence and Delete Time are refused.
- The automation editor's Master destination has a Song Tempo lane on a logarithmic BPM scale.
  - A constant tempo shows as an empty lane.
  - Drawing a point creates a map, whose first point at beat 0 takes the first drawn tempo.
- Exported MIDI clips carry the tempo at their start and each tempo point inside them, written as steps.

## Engine behaviour

The engine applies a ramp in quarter-beat steps of constant tempo (up to 100 per segment). Rendering, playback and every conversion agree, because all of them use the engine's map. A ramp's duration therefore differs slightly from the exact integral of a linear ramp.

## Projects

Tempo points are the engine's TEMPO children. Loading rejects these and keeps the open project:

- a first point away from beat 0;
- decreasing or too-close beats;
- curve values outside −1..1;
- tempos outside 20–999 BPM;
- more than 4096 points.

## Evidence

`tests/TempoMapTests.cpp` renders a short note on every beat and checks:

- rejection rules;
- a drop from 120 to 60 BPM at beat 4, with every onset within 2 ms of the map (beat 8 at 6.000 s);
- clip, note and automation beats kept;
- the tempo at the playhead before and after the drop;
- a 60–180 BPM ramp over eight beats within 2% of 4 ln 3 seconds, with every onset on the map;
- undo and redo;
- the constant-tempo controls refused;
- reopening;
- three damaged maps rejected;
- exported tempo events;
- returning to a constant tempo.

The automation editor suite draws a Song Tempo point and undoes it. The UI suite checks a position label, beat snapping and the disabled tempo controls on a map.

## Not implemented

- Curved tempo segments.
- Insert Silence and Delete Time with a tempo map.
- Beat-based locators (locators stay at their times).
- Tempo changes in imported MIDI files.
- Scene tempos.
- Any comparison with reference output.
