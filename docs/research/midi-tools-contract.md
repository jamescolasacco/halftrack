# MIDI note tool contract

The piano roll's Note tools menu has Transform and Generate submenus modelled on the reference's MIDI transformation and generative tools ([MIDI Tools](https://www.ableton.com/en/live-manual/12/midi-tools/), consulted 2026-10-20). Each tool opens a small settings window; Transform or Generate applies it, and the window stays open for another pass. These are Halftrack's own algorithms. Their parameters and results are simplest-default choices, not measurements of the reference tools.

## Interface

- `applyNoteTool(clipId, noteIds, NoteToolSettings, resultIds)` applies a tool in one undo step and reports the new notes' IDs.
  - A transformation replaces the chosen notes with its result. The piano roll passes the selection, or every note when nothing is selected.
  - A generator (Euclidean, Seed) adds notes across the clip and replaces any chosen notes.
- `NoteToolSettings` holds the tool and up to eight values in the order of its field table in `src/NoteTools.h`. Out-of-range, non-integer choice and unused values are rejected, as are transformations with no notes, results over 16384 notes, frozen tracks and edits while recording or exporting.
- `notetools::run` is the pure computation, tested directly.
- Results are clamped inside the clip, at least a 960th of a beat long, with velocities of at least 1/127.

## Tools

| Tool | Settings | Result |
| --- | --- | --- |
| Strum | Low, High (beats), Tension | Within each chord (notes sharing a start), the lowest tone moves by Low and the highest by High, the others between them along a curve set by Tension. Each note keeps its end. |
| Chop | Parts (2–16), Gap | Each note becomes equal parts, each shortened by the gap fraction. |
| Span | Legato, Tenuto or Staccato; Amount | Legato reaches the next note's start, Tenuto fills Amount of that distance, and Staccato scales the length by Amount. The last note uses the selection's end. |
| Recombine | Pitch, Velocity or Length; Rotate, Mirror or Shuffle; Steps; Seed | Reassigns one property among the notes in time order: rotated by Steps, reversed, or shuffled reproducibly by Seed. |
| Velocity Shaper | Start, End (1–127), Curve | Velocities ramp from Start to End across the notes' start times. |
| Arpeggiate | Up, Down, Up & Down, Down & Up, Converge or Random; Rate (1/32–1/2); Gate; Steps; Distance; Seed | Each chord becomes a run over its own length. With more than one step, the chord tones repeat Distance semitones apart. Up & Down turns without repeating its ends, and Converge alternates the outer tones. |
| Ornament | Offset, Pitch, Velocity | Adds a grace note Offset beats before each note, transposed and scaled in velocity. A note too close to the clip start gets none. |
| Connect | Rate, Velocity, Gate | Fills gaps between consecutive notes with passing notes on the Rate grid. Their pitches interpolate between the two neighbours, and their velocity is the neighbours' average scaled by Velocity. |
| Euclidean | Steps, Pulses, Rotation, Pitch, Rate, Gate, Velocity | Spreads Pulses evenly over Steps (step *i* sounds when (*i* × pulses) mod steps < pulses). Rotation shifts the pattern earlier, and it repeats across the clip. |
| Fit to Scale | Root, Scale | Each note moves to the nearest tone of the scale, ties going down. The window starts from the clip's scale (see the [scale contract](scales-contract.md)). |
| Seed | Notes, pitch range, Shortest, Longest, Grid, Seed, Velocity | Places that many notes at random grid starts with random pitches and lengths, reproducibly for a seed. |
| Quantize | Grid (Current Grid, 1/4, 1/8, 1/8T, 1/8 + 1/8T, 1/16, 1/16T, 1/16 + 1/16T, 1/32), Start, End, Amount | See [Quantize](#quantize-2026-10-24). |

## Evidence

`tests/MidiToolTests.cpp` compares every tool's output note by note with hand-computed results. It checks:

- upward and downward strums;
- Chop with a gap;
- the three Span modes;
- rotated pitches, mirrored velocities and backward-rotated lengths;
- a seeded shuffle that is a repeatable permutation;
- a velocity ramp;
- Up, Up & Down, Converge, and two octave steps played downward on eighths;
- grace notes, including none at the clip start;
- passing notes;
- tresillo and rotated Euclidean patterns, and repetition across the clip;
- Seed's range, grid, length bounds and repeatability.

In a Session it also checks replacement with reported IDs, single-step undo, generators with and without chosen notes, and rejections. The UI suite opens Chop from its window, applies it to the selected note and undoes it.

## Not implemented

The reference's Time Warp, Rhythm, Shape and Stacks tools, live preview while settings change, scale awareness, and any comparison with reference output.

## Quantize (2026-10-24)

The reference's manual: the Quantize tool moves or stretches the selected notes to the current grid or a chosen value, triplets included; it quantizes note starts, ends or both, and Amount moves notes only part of the way ([MIDI Tools](https://www.ableton.com/en/live-manual/12/midi-tools/), section 11.2.7, consulted 2026-10-24). Edit > Quantize uses the tool's settings, and Quantize Settings opens it ([Editing MIDI](https://www.ableton.com/en/live-manual/12/editing-midi/), section 10.5.11). The reference's quantize algorithm keeps a straight and a triplet reference grid, note starts and ends, and an amount (static reading of its `AQuantizeAlgorithm` members), and its Record Quantization list offers the nine grids below.
- **Grids.** Current Grid (the editor's grid), 1/4, 1/8, 1/8T, 1/8 + 1/8T, 1/16, 1/16T, 1/16 + 1/16T and 1/32, the combined ones taking the nearer line of either grid. Lines run from the clip's start.
- **Start, End, Amount.** With Start, each note's start moves Amount of the way to the nearest line and the note keeps its length. With End, its end moves Amount of the way to the nearest line; an end that would reach the start goes to the next line after the start. With both, the note moves and stretches.
- **Notes.** It applies to the selected notes, or every note when none is selected, moving the notes themselves: they keep their identities, chance, velocity range and expression. One application is one undo step, named Quantize.
- **Keys and menus.** Edit > Quantize and Cmd+U apply the tool with its last settings, Current Grid, Start on, End off and Amount 100% at first. Edit > Quantize Settings... and Shift+Cmd+U open its window. Every note tool now keeps its last settings while Halftrack runs.

`tests/MidiToolTests.cpp` checks the defaults; starts to the current and 1/4 grids; Amount at 50%; ends alone, one end going past its start; starts and ends together; triplet and combined grids; Current Grid following the editor's; and a quantized note keeping its identity and chance. In a Session it checks that the notes move on the given grid as one Quantize step keeping their IDs and chance, that undo restores them and that a zero grid is refused. The UI suite opens the window from Shift+Cmd+U and from Edit > Quantize Settings, sets Amount to 50% and finds Cmd+U moving the notes halfway.

## Apply Current MIDI Tool Settings (2026-10-25)

The reference's key list gives Cmd+Return for Apply Current MIDI Tool Settings in the MIDI Note Editor ([Live keyboard shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), section 42.12, consulted 2026-10-25). In Halftrack the current tool is the one last opened or applied, Quantize included; Cmd+Return applies it again with its last settings to the selected notes (a transformation to every note without a selection), one undo step, as its window's Apply does. Before any tool is used it does nothing.

The UI suite applies Chop with two parts from its window, then selects every note and finds Cmd+Return chopping them again, each note into two, undone in one step.
