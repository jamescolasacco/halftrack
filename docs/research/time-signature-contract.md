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

### Arrangement interface

- **The lane.** While the meter changes, the Arrangement ruler grows a lane just below the bar numbers, as the reference's does. It shows each marker's meter, the first included. The locators' scrub area moves below it, and the lane goes when the last change does.
- **Inserting.** Insert Time Signature Change, in the Create menu and the ruler's context menu, adds a marker at the insert marker (from the menu) or at the clicked place on the grid. It takes the meter already there and asks for the new one. At the start it edits the first marker.
- **Editing.**
  - Clicking a marker selects it. Dragging moves it along the grid, as one undo step, but never onto another marker.
  - The arrow keys move it a grid line, Delete deletes it, and Cmd/Ctrl+R (Edit Time Signature) or a double-click types its meter.
  - The marker's context menu offers Edit Time Signature and Delete Time Signature Change.
  - The first marker can be retyped but not moved or deleted.
- **Bars and the grid.**
  - Bar numbers stand at the bars' starts through the markers, and a fragmentary bar is crosshatched.
  - The canvas's grid lines and Arrangement snapping run from each bar's start, so a 1/4 grid restarts at a marker and a grid of a bar or more falls on bar starts.
  - The loop brace's drags and its Shift-drag definition snap the same way.
- **Positions.** The Control Bar's position and loop start read bars.beats.sixteenths through the markers, beats in each bar's meter. Typed positions read back the same way.
- **Meter field.** The Control Bar's meter field shows and changes the meter at the playhead. While the Arrangement has time signature changes, a red automation LED lights in its upper left corner, as the reference's fields show one.

The UI suite inserts a 3/4 marker from the Create menu and types its meter. It then checks:
- the lane appearing;
- positions through the fragmentary bar;
- snapping to the marker and inside the fragmentary bar;
- selecting, dragging, undo, the right arrow, Delete with the lane going and coming back on undo, and the marker's menu.

A snapshot (`arrangement-meters.png`) shows the hatched second bar and the lane.

### Fragmentary bars and time edits

As the reference's manual describes, right-clicking a fragmentary bar or the marker after it offers two commands. Each acts across the whole Arrangement as one undo step.
- **Delete Fragmentary Bar Time** (`deleteFragmentaryBarTime`) deletes the short bar's time, so the marker moves to where that bar started.
- **Complete Fragmentary Bar** (`completeFragmentaryBar`) inserts the time the bar lacks at its start, so the marker falls on the next bar line.

They are Delete Time and Insert Silence over that time, which now move the time signature markers after them as they move clips and locators. Delete Time removes markers inside the deleted time; the first marker stays at the start. Like those commands, they need a constant tempo.

Evidence (`tests/TimeSignatureTests.cpp`):
- completing a two-beat fragment moves the marker and a later clip two beats later, leaving the bar whole, and one undo puts both back;
- deleting the fragment's time moves them two beats earlier;
- Insert Silence moves the marker a beat later;
- Delete Time over the marker removes it;
- the first marker, unknown markers and whole bars are refused.

The UI suite completes a fragmentary bar from its menu.

Not yet: importing a MIDI file's meters, clip editors' rulers (which keep the meter at the playhead), and scene meters with markers present.

## Not implemented

- Scene meters while the Arrangement has markers.
- Eighth-note click subdivision.
- Importing a MIDI file's meter.
- Any comparison with reference output.
