# Clip scale contract

MIDI clips carry a scale, after the reference's scale awareness ([Editing MIDI: Using Scales](https://www.ableton.com/en/live-manual/12/editing-midi/), consulted 2026-10-20). The piano roll highlights the scale, and the Fit to Scale note tool moves notes into it. This is Halftrack's own implementation with sixteen common scales. The reference's full scale list and scale-aware devices are not implemented.

## Interface

- `setClipScale(clipId, ScaleSettings)` sets a MIDI clip's scale in one undo step. Roots past B, unknown modes and unknown clips are rejected.
  - `enabled` turns the scale on.
  - `root` is a pitch class from C (0) to B (11).
  - `mode` is one of `scaleModeCount` scales, appended only.
- `ClipInfo::scale` reports it, and duplicated or copied clips keep it.
- `scaleIntervals(mode)` returns a 12-bit mask of the scale's semitones above its root. The modes are:
  - Major, Minor, Dorian, Phrygian, Lydian, Mixolydian and Locrian;
  - Harmonic Minor and Melodic Minor;
  - Major Pentatonic, Minor Pentatonic and Minor Blues;
  - Whole Tone, Half-whole Diminished and Whole-half Diminished;
  - Phrygian Dominant.
- `inScale(scale, pitch)` tests membership; every pitch is in a disabled scale.
- `fitToScale(scale, pitch)` returns the nearest scale pitch, with ties going down.
- The Fit to Scale note tool (Transform menu) applies that to the chosen notes. Its root and scale default to the clip's scale.

## Piano roll

The header's Scale switch, root menu and scale menu edit the clip's scale, as the reference's Scale Mode does. With it on, the scale's keys are marked beside the piano keys.

Options > Scale Highlighting (K), off at first, applies to every clip, as in the reference ([Editing MIDI](https://www.ableton.com/en/live-manual/12/editing-midi/), consulted 2026-10-24). With it on and a clip's scale on:

- rows of scale notes are lighter;
- root rows are tinted, and the root's keys more strongly marked;
- rows outside the scale are darker.

## The current scale (2026-10-26)

As the reference's manual describes, the clip in the foreground gives newly created MIDI clips their scale: "newly-created MIDI clips will inherit the most recently selected clip's scale setting". The reference keeps this as its Set's current scale.

- `Session::currentScale` and `setCurrentScale` hold it. The clip view sets it to the shown MIDI clip's scale (its switch, root and scale) whenever it refreshes.
- MIDI clips drawn in the Arrangement or a slot, captured, and recorded (Arrangement and Session takes) take it. Pasted, duplicated and imported clips keep their own.
- It is kept with the project (`halftrackCurrentScale`, `halftrackCurrentScaleRoot`, `halftrackCurrentScaleMode` on the Edit), outside the undo history. A new project starts without one, and invalid values reject a load, keeping the open project.

## Folding

View's Fold to Notes (F) and Fold to Scale (G), after the reference's, apply to every MIDI clip; the reference's menu builder puts them after Fold Selected Track, between separators (see the [menu bar contract](main-menu-contract.md)).

- Fold to Notes shows only the rows with notes, in the edited clip and the clips shown with it; a clip without notes shows every row. The note editor's Fold switch shows and sets it.
- Fold to Scale, available while the clip's scale is on, shows the scale's rows and any row with notes.
- With both on, the rows with notes show.
- Dragging a note up or down moves it by rows, so folded it lands on the next row shown. The rows stay as they are until the drag ends.
- They belong to the window and are not saved with the project.

## Projects

The scale is stored on the MIDI clip (`halftrackScale`, `halftrackScaleRoot`, `halftrackScaleMode`). Loading rejects values on anything but a MIDI clip, and non-integer or out-of-range values, and keeps the open project.

## Evidence

`tests/MidiToolTests.cpp` checks:

- membership in C major and A minor, and in a disabled scale;
- nearest-tone fitting, including ties and pentatonic gaps;
- the exact Major mask and mode names;
- the Fit to Scale tool's note results;
- rejection rules;
- undo and redo;
- duplication;
- reopening;
- rejection of an unknown saved mode.

The same suite gives a project D minor as its current scale and finds a drawn clip and a slot clip taking it, undo leaving it, the project reopening with it, an invalid saved root rejecting the load, an invalid scale ignored, and a new project starting without one.

The UI suite sets D minor from the header controls, finds it become the current scale, opens Fit to Scale with that scale, undoes the change, and captures a snapshot. It also checks F and G folding the rows to the notes and to the scale, a note dragged a row moving to the scale's next pitch, and K dimming the rows outside the scale.

## Not implemented

- Scale-aware devices.
- Microtonal tunings.
- The reference's complete scale list.
- Any comparison with reference output.
