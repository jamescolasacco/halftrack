# Slice to New MIDI Track contract

Halftrack's Slice to New MIDI Track follows the reference's audio clip command of that name. It is offered in an Arrangement audio clip's menu.

## Behaviour

- **Slices** begin at one of:
  - the clip's transients, found as Beats warping finds them;
  - its warp markers;
  - every 1/16, 1/8, 1/4 or 1/2 note, or every bar of its beats.

  There are at most 128.
- **Clips it can slice.** The clip must be unlooped and in the Arrangement, and either unwarped or warped without markers. A clip with warp markers is sliced at its markers.
- **The new track.**
  - A MIDI track is created after the clip's track, named after the clip.
  - Its Drum Rack has one pad per slice from C1 (note 36) up. Each pad's Sampler plays its slice of the source file as a one-shot at the source's own speed and pitch.
  - Its MIDI clip covers the clip's place, with one full-velocity note per slice where the slice falls.
- **The same timing.** Unmuted at the same tempo, the new track plays the clip's audio back. A warped clip's slices keep their speed at another tempo and start on the clip's beats there.
- **Undo.** The whole command is one undo step, and the new track is selected.

## Evidence

`tests/SliceTests.cpp` slices decaying 1 kHz hits on every beat of a 120 BPM file:

- **At transients:**
  - a MIDI track follows the clip's;
  - its Drum Rack has eight pads, 36 to 43;
  - its notes start on beats 0 to 7;
  - rendered alone, it reproduces the original exactly (0.00% difference), and one undo removes it.
- **At divisions:** every quarter note makes the same eight slices; every eighth makes sixteen, half a beat apart, also reproducing the original.
- **Refusals:** slicing an unwarped clip at warp markers, and an unknown clip.
- **A warped clip at 60 BPM:** slicing at transients puts a note on each beat, and each hit plays at its own speed and level on the 60 BPM beats, followed by silence.

The UI suite slices a clip from the menu command, checks the new Drum Rack track is selected, and undoes it.

## Not yet covered

- The reference's slicing presets, Simpler in Slicing mode, and the Convert to MIDI commands (Drums, Harmony, Melody) are missing.
- Slicing a looped clip is refused.
