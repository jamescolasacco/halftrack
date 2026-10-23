# Time signature contract

A project has a time signature, set from the control bar as in the reference ([Arrangement View: Tempo and Time Signature](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-20). Since 2026-10-23 it can also change along the timeline at time signature markers (see [Time signature markers](#time-signature-markers-2026-10-23)). This is Halftrack's own implementation on the engine's tempo sequence.

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

The meters are the engine's time signatures. Loading rejects these and keeps the open project:

- a first time signature away from the start, or two at one beat;
- time signatures out of beat order;
- more than 4096;
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

## Time signature markers (2026-10-23)

The reference's manual describes these (Arrangement View, Time Signature Changes, consulted 2026-10-23). Markers change the meter anywhere in the Arrangement. They are not quantized beyond the editing grid. One placed inside a bar leaves that bar short, a fragmentary bar, which the reference leaves as it is.

- **Session API.**
  - `Snapshot::timeSignatures` lists the markers in beat order, the first at beat 0. `Snapshot::timeSignature` is now the meter at the playhead.
  - `insertTimeSignatureChange(beat, meter)` adds a marker after the start, or changes the meter of one already at that beat.
  - `setTimeSignatureChange`, `moveTimeSignatureChange` and `removeTimeSignatureChange` act on a marker by index. The first stays at beat 0 and cannot go.
  - Each is one undo step, and moves keep the markers in beat order.
  - `setTimeSignature`, the Control Bar's fields, changes the marker in effect at the playhead, as the reference's fields change the time signature at the current play location.
- **Bars.**
  - Each marker starts a bar of its meter at its beat, and bars are numbered on through fragmentary ones.
  - `Session::barAt(markers, beat)` gives a beat's bar: its number, start, length, meter and whether it is fragmentary. `barStart` gives where a bar starts.
  - Playback counts bars the same way through the engine patch `cmake/PatchMeterBars.cmake`, so launch quantization, the metronome's accents and count-in follow the markers.
- **Files.** Projects save and reopen the markers. An exported MIDI clip carries the meter at its start and a time signature event at each marker inside it.

Correction: until this patch the engine counted a bar as the numerator in quarter notes, so a bar of 6/8 was six quarter notes and 7/8 seven. Bar launch grids and plug-ins' bar positions followed that, and plug-ins were told half the quarter notes in 6/8. The earlier statement here that launch quantisation's Bar follows the meter held only for meters over 4. Launch grids finer than a bar were fractions of the bar, so 1/4 in 3/4 was three quarters of a beat. Now a bar is numerator × 4 / denominator quarter notes, and 1/4 is a quarter note in any meter, as in the reference.

Evidence (`tests/TimeSignatureTests.cpp`):
- markers inserted, moved, re-metered, reordered and deleted, each one undo step, with the bars `barAt` and `barStart` report: a fragmentary second bar of two beats, and 7/8 bars of three and a half beats;
- the refusals;
- the Control Bar's meter changing the marker at the playhead;
- the markers reopening;
- a MIDI clip's file carrying 4/4 at its start and 6/8 at the marker inside it;
- through accelerated engine callbacks: a launch on the bar grid in 7/8 at beat 3.5, on the 1/4 grid in 3/4 one beat on, and across a marker inside a bar at the marker.

The metronome suite matches every click and accent across a 3/4 marker cutting a 4/4 bar short. The plug-in timing suite hears a probe plug-in count quarter notes in 6/8.

Not yet: the Arrangement's marker lane below the beat-time ruler, with Insert Time Signature Change, Edit Time Signature (Cmd+R), dragging, the arrow keys and Delete. Also missing are the ruler and grid following the markers (they draw the meter at the playhead), crosshatched fragmentary bars, Delete Fragmentary Bar Time and Complete Fragmentary Bar, the Control Bar's automation LED, and importing a MIDI file's meters.

## Not implemented

- The Arrangement interface for time signature markers (above).
- Scene meters.
- Eighth-note click subdivision.
- Importing a MIDI file's meter.
- Any comparison with reference output.
