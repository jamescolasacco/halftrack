# Time signature contract

A project has one time signature, set from the control bar as in the reference ([Arrangement View: Tempo and Time Signature](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-20). This is Halftrack's own implementation on the engine's tempo sequence. Meter changes along the timeline are not implemented.

## Interface

- `setTimeSignature(numerator, denominator)` accepts 1–99 over 1, 2, 4, 8 or 16 in one undo step. Other values, and changes while recording or exporting, are rejected.
- `Snapshot::timeSignature` reports it, and `beatsPerBar()` gives its length in beats.
- The control bar's meter field takes "3/4", "6 / 8" and so on; invalid entries leave the meter unchanged.

## Beats and bars

- Beats are quarter notes at every meter, and the tempo counts quarter notes per minute. A bar is numerator × 4 / denominator beats: 6/8 is three beats, and 7/8 is three and a half.
- MIDI notes, automation, warp and every beat-based value keep their timing when the meter changes.
- The engine is told that a beat does not depend on the meter.
- What follows the meter:
  - Launch quantisation's Bar, and the engine's bar grids.
  - Metronome accents, on the first click of each bar. Clicks fall on quarter-note beats.
  - Count-in bars.
  - Bar numbers on the Arrangement ruler and in the piano roll.
  - Bar lines on the Arrangement canvas.
  - The coarsest snap step.
  - New MIDI clips, which start on a bar.
  - Exported MIDI files, which carry a time signature event.
- Positions and loop fields read bars.beats.sixteenths, with beats counted in the meter's note value (eighths in 6/8) and sixteenths within that beat. Lengths count from zero.

## Projects

The meter is the engine's first time signature. Loading rejects these and keeps the open project:

- more than one time signature;
- a time signature away from the start;
- a numerator outside 1–99;
- a denominator other than 1, 2, 4, 8 or 16.

## Evidence

`tests/TimeSignatureTests.cpp` checks:

- defaults and rejection rules;
- undo and redo;
- a rendered note at beat 3 starting at 1.5 s in both 6/8 and 4/4;
- reopening 7/8;
- the 7/8 time signature event in an exported MIDI file;
- rejection of corrupted numerators, denominators and a second meter;
- a Session clip launched on the bar grid in 3/4 starting at exactly sample 72000 (beat 3) through accelerated engine callbacks.

The metronome suite matches every click and accent sample in 3/4 and 5/4. The UI suite edits the meter field, rejects an invalid meter, and checks 6/8 position formatting and parsing.

## Not implemented

- Meter changes along the timeline.
- Scene meters.
- Eighth-note click subdivision.
- Importing a MIDI file's meter.
- Any comparison with reference output.
