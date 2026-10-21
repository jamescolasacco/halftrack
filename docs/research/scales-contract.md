# Clip scale contract

MIDI clips carry a scale, after the reference's scale awareness ([Editing MIDI: Using Scales](https://www.ableton.com/en/live-manual/12/editing-midi/), consulted 2026-10-20). The piano roll highlights the scale, and the Fit to Scale note tool moves notes into it. This is Halftrack's own implementation with sixteen common scales. The reference's full scale list, Fold to Scale and scale-aware devices are not implemented.

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

The header's Scale switch, root menu and scale menu edit the clip's scale. With it on:

- rows of scale notes are lighter;
- root rows are tinted;
- rows outside the scale are darker.

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

The UI suite sets D minor from the header controls, opens Fit to Scale with that scale, undoes the change, and captures a snapshot.

## Not implemented

- Fold to Scale.
- A project-wide current scale.
- Scale-aware devices.
- Microtonal tunings.
- The reference's complete scale list.
- Any comparison with reference output.
