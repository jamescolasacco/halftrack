# MIDI note tool contract

The piano roll's Note tools menu has Transform and Generate submenus modelled on the reference's MIDI transformation and generative tools ([MIDI Tools](https://www.ableton.com/en/live-manual/12/midi-tools/), consulted 2026-10-20). Each tool opens a small settings window; Transform or Generate applies it, and the window stays open for another pass. Rhythm, Shape, Stacks and Time Warp follow the reference's own algorithms, read statically from its binary (see [below](#rhythm-shape-stacks-and-time-warp-2026-10-25)). The others are Halftrack's own algorithms; their parameters and results are simplest-default choices, not measurements of the reference tools.

## Interface

- `applyNoteTool(clipId, noteIds, NoteToolSettings, resultIds, gridBeats, timeRange)` applies a tool in one undo step and reports the new notes' IDs.
  - A transformation replaces the chosen notes with its result. The piano roll passes the selection; with none, the notes starting in its time selection, or every note without one.
  - A generator replaces any chosen notes with its own. Euclidean and Seed fill the clip; Rhythm, Shape and Stacks fill the time range.
  - Quantize and Time Warp move the chosen notes themselves.
- `NoteToolSettings` holds the tool and up to 16 values in the order of its field table in `src/NoteTools.cpp`, Stacks' chords, Shape's drawn levels and the seed of Rhythm's and Shape's random draws. Out-of-range, non-integer choice and unused values are rejected, as are transformations with no notes, results over 16384 notes, frozen tracks and edits while recording or exporting.
- `noteToolContext(clipId, timeRange)` gives where Rhythm, Shape, Stacks and Time Warp work: the time range, the clip's scale while on, and a Drum Rack's pads.
- `notetools::run` is the pure computation, tested directly. `notetools::range` and `notetools::limited` give the ranges that narrow with other values, as Rhythm's do.
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
| Rhythm, Shape, Stacks, Time Warp | The reference's | See [below](#rhythm-shape-stacks-and-time-warp-2026-10-25). |

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

Live preview while settings change (the reference's Auto Apply), the reference's Clip View panels for the tools, scale awareness outside Rhythm, Shape and Stacks, and any comparison with the running reference's output.

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

## Rhythm, Shape, Stacks and Time Warp (2026-10-25)

### Evidence

- The reference's manual names the tools' controls and says what they do: Rhythm's Pitch, Steps, Pattern, Density, Step Duration, Split, Shift, Velocity, Accent, Accent Frequency and Accent Offset; Shape's presets and drawn shape, Minimum and Maximum Pitch, Rate, Tie, Density and Jitter; Stacks' chords with Chord Root, Inversion, Duration and Offset in eighths of a chord's share, and its `.stacks` chord banks; Time Warp's one to three breakpoints with Time and Speed, Quantize, Preserve Time Range and Include Note End. Generators work in the time selection, or the loop without one ([MIDI Tools](https://www.ableton.com/en/live-manual/12/midi-tools/), consulted 2026-10-25).
- The algorithms, parameters, defaults and ranges come from a static reading of the reference's arm64 binary (Live 12.2.1, sha256 b37cb738…3fc) with Apple LLVM 17 `objdump` and Ghidra 12.1.4, checked instruction by instruction for rounding and fused multiply-adds. The reading was turned into an executable model, kept with the raw material in ignored local artifacts. Nothing was run or measured in the reference, and its UI labels and display formats live in encrypted resources that were not opened.
- What the reading found:
  - **Shared arithmetic.** The reference rounds beat times to the nearest 3843840th of a beat, halves away from zero. Its random draws are libc++'s `minstd_rand` with `uniform_int_distribution` and `std::shuffle`, seeded per tool from `/dev/urandom` each time the tool is applied.
  - **Time range.** Its time range is the editor's time selection, or else the clip's loop brace whether or not the loop is on.
  - **Merging.** A generator's notes replace the selected notes, or are added when none is selected. Rhythm also clears notes of its key in the range; whether that means starting in it or overlapping it was not decoded.

### Halftrack's behaviour

- **Rhythm** (generator): one key on a grid of steps filling the time range.
  - Steps 1–16 (8); Pattern (14, every other step of 8); Density 1–Steps (4); Step Duration Grid, Fit or 1 to 1/128 (1/8); Repetitions 1–8, with Fit only (1); Split 0–100% (0); Shift −8 to 8 (0); Pitch (C1); Velocity (100); Accent (127); Accent Frequency (4); Accent Offset (0).
  - The patterns are every arrangement of Density hits over Steps whose first step is a hit, in ascending order.
  - Split cuts a step in two by chance. Accents fall every Accent Frequency notes from Accent Offset, a split step counting two, and 0 turns them off.
  - Steps narrow to those that fit the range (16 with Fit), Density to Steps, Pattern to the patterns there are, Accent Frequency to the hits in the range and Accent Offset below it. Each value returns when the range widens again, and Pattern keeps its place among the patterns.
  - A velocity of 0 gives muted notes. Notes' release velocity is their velocity. Pitch snaps to the nearest key the scale or Drum Rack has.
  - It also clears the notes of its key that start in the range.
- **Shape** (generator): a random rhythm of Rate's steps with notes, ties and rests, the first note always first. Its keys follow the contour from the middle of the first note to the middle of the last.
  - Presets Flat, Up (the default), Down, Up & Down, Down & Up, Arc Up, Arc Down, Bounce Up and Bounce Down. Dragging on its display draws a shape of 21 levels, which becomes User.
  - Rate Grid, Fit (a step per key) or 1 to 1/128 (Grid). Tie and Jitter run 0–100% (0) and Density 0–100% (100%). Minimum (C3) and Maximum (C5) push each other.
  - The keys are those in the range the clip's scale has while on, or a Drum Rack's pads. Jitter pulls a note's key at most halfway toward a random one. Velocity and release velocity are 64.
- **Stacks** (generator): one to 16 chords, each in an equal share of the time range, sized by Duration and moved by Offset in eighths of it (1/8–15/8, and −7/8 to 7/8).
  - Each chord has a rule of the chord bank, a root degree and octave (C3 at first), and an inversion that moves its lowest notes up an octave, or below zero its highest down.
  - **+** adds a copy of the last chord and **−** deletes the selected one. The Chord list names each rule by the keys it plays from the chord's root.
  - With the clip's scale on, rules count scale degrees and the root is a degree of the scale. On a Drum Rack, a chord counts pads up from its root's pad, without inversions.
  - Velocity and release velocity are 64.
  - **The chord bank is Halftrack's own**, not the reference's: 16 rules each for scales of 5, 7 and 12 notes. The 12-note rules are major, minor, diminished, augmented, sus2, sus4, power, quartal and eight sevenths and sixths; the 7- and 5-note rules are degree shapes. Other scale sizes use the 7-note rules, as the reference falls back. Loading `.stacks` banks is not implemented.
- **Time Warp** (transformation): moves the chosen notes' starts (and ends, with Include Note End) through the integral of a speed curve, keeping the notes.
  - The curve comes from one to three breakpoints, each with a time across the range and a speed from 0.1× to 10×, interpolated linearly in density and held beyond the ends.
  - Preserve Time Range (on) scales the result so the latest-starting note ends where it did. Quantize (off) then puts starts on the editor's grid.
  - Defaults: the first and last breakpoints on at speed 1, and the middle one off at half the range. A breakpoint's time stays between the active breakpoints beside it, and at least one stays on.
  - Its display shows the speed line; a breakpoint is chosen by clicking it and moved by dragging, or with its Time and Speed sliders. Times read as a percentage of the range and speeds as multiples.
- **Common to the four.**
  - The time range is the editor's time selection, or else the clip's loop brace (the content it plays while unlooped), cut to the clip. An empty range is refused.
  - A new seed is drawn each time the window's Apply or Cmd+Return applies a tool.
  - Notes are cut to the clip, and notes left shorter than a 960th of a beat are dropped (Halftrack's clips hold notes only inside themselves). A tool that would pass the notes through unchanged, such as Shape with no key to play, changes nothing.
- **Exactness.** Halftrack computes these tools with the reference's arithmetic, step by step. `src/NoteTools.cpp` is built without floating-point contraction, so only its explicit `std::fma` calls fuse, as the reference's did. The tests compare the results bit for bit with the model's output; the model itself has not been compared with the running reference.

### Not followed

- The reference's chord bank, the Stacks pad of Tonnetz pictograms and `.stacks` loading.
- Choosing Pitch, Root or a pitch range with Option on the piano ruler.
- Auto Apply's live preview, the reference's panels and its display formats (it shows Time Warp times in bars and beats, for one).
- Merging by key beyond Rhythm's clearing.

See [open questions](../open-questions.md#rhythm-shape-stacks-and-time-warp) for the defaults taken.

### Evidence in Halftrack

`tests/MidiToolTests.cpp` checks, against the research model's output to the last bit:

- the random draws (minstd from seed 1, and five 1–100 draws from seed 12345) and the pattern lists;
- Shape's nine presets;
- Rhythm's first pattern, a 16-step pattern split by chance, shifted and accented, and its default;
- Shape's Up and a tied, sparse, jittered shape;
- Time Warp at speed 1, speeding up with and without Preserve Time Range, and with a slow middle breakpoint.

It also checks:

- muted notes at velocity 0, Fit, and the narrowing and widening ranges;
- Shape on a scale's keys, a drawn shape, and passing notes through;
- Stacks' first chord, a three-chord progression in C major with inversions, roots as degrees in C major and A minor, chords on Drum Rack pads, and the limit of 16 chords;
- Time Warp's Quantize, Include Note End, speed mapping and breakpoint order.

In a Session it checks:

- Rhythm clearing its key in the range as one step;
- Stacks replacing chosen notes across the loop brace, and in a selection on the clip's A minor;
- Time Warp moving the notes while keeping their identities, with the latest ending where it did;
- refusals of an empty range and of Time Warp without notes;
- a Drum Rack's pads as the keys Shape plays.

The UI suite opens each tool's window: Rhythm with Repetitions waiting for Fit, Stacks adding a chord, setting its root to G3 and deleting it, Shape drawn into User, and Time Warp with its middle breakpoint turned on and sped up. Each is applied and undone.
