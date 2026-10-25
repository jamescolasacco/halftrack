# Track colours contract

Status: partial. New tracks, groups, returns and clips take their colours as the reference's do, by its algorithm read statically, from Halftrack's own palette. No reference output was measured.

## Evidence

- A research agent read the reference's code statically (Live 12.2.1, arm64 slice sha256 b37cb738…; Apple LLVM 17 objdump and the existing Ghidra 12.1.4 decompilation; raw notes in the ignored `artifacts/reverse-engineering/trackcolors/`, 2026-10-25). Nothing was launched, and no user preference, Set or library was read.
  - Its palette has 70 entries. Each Set keeps a colour sequence index (`ColorSequenceIndex`), which chooses one of eight sequences of 17 palette indices (the index modulo 8). Two pickers, saved with the Set as `NextColorIndex`, walk the sequence: one for audio, MIDI and group tracks stepping forward, one for returns and the Main stepping back, starting one slot before the first so the two walk apart.
  - Its random numbers come from one minstd generator (multiplier 48271, modulus 2^31 − 1), seeded once from the system. A missing sequence index becomes a raw draw, and the pickers' start is a second draw modulo the sequence's length.
  - Colouring a track: with Auto-Assign Track Colors off, it takes Default Track Color (a palette index, 0 at first). Otherwise a new group takes its first track's colour, a track inside a group takes the group's colour without the picker stepping, and any other track takes its picker's next colour. Duplicates keep their colours.
  - A new Set not made from the user's template draws a new sequence index and picker start, then recolours every track in order (groups before what they hold), then the Main, then the returns. The built-in default Set stores sequence index 1153618690, tracks coloured 19, 18, 20 and 21, the Main 4, returns 1 and 14, and pickers at 7 and 16, all of which the algorithm reproduces.
  - The colourblind-friendly option (`mpRestrictAutomaticColorsToColorblindSubset`) swaps in one sequence of 11: 0, 1, 3, 5, 9, 10, 14, 20, 21, 23, 26. Changing it restarts the pickers at a random slot, keeping the sequence index and every colour.
  - Random clip colours (`mpAssignRandomClipColor`, off) give a new clip without a colour one drawn from the sequence, other than the last two drawn; clips recorded together share one. Off, a clip takes its track's colour.
  - The defaults are from its song preference data: Auto-Assign Track Colors on, Default Track Color 0, both other options off.
- Live 12 manual, [First Steps](https://www.ableton.com/en/live-manual/12/first-steps/), section 2.3.2 (Theme & Colors), consulted 2026-10-25: its Track and Clip Colors section switches auto-assigning track colours on or off, chooses a default colour for all tracks, decreases the colours used with Reduced Automatic Colors, and has a Clip Color toggle between random colours and the track's colour for new clips.

## Halftrack's behaviour

- **The palette.** Halftrack's own 70 colours (`Session::paletteColour`), laid out as the reference's grid of 5 rows of 14, so the reference's sequences pick Halftrack's colours at the same places. The reference's theme colours are not used.
- **The project** keeps its colour sequence index (`halftrackColourSequence`), its pickers' next slots (`halftrackNextTrackColour`, `halftrackNextReturnColour`) and the Main's colour (`halftrackMainColour`) as Edit properties, reported by `Snapshot::colourSequence`, `nextTrackColour`, `nextReturnColour` and `mainColour`. Values out of range refuse the project. A project without them draws them when it first needs a colour.
- **New tracks.** Insert Audio Track and Insert MIDI Track (default tracks included, whose own colour is replaced), Slice to New MIDI Track and the other paths that insert tracks take the track picker's next colour, or their group's inside one; Insert Return Track takes the return picker's. Group Tracks gives the group its first track's colour. With Auto-Assign Track Colors off each takes Default Track Color, and the pickers stay. Each step is part of the command's undo step, so undo puts the picker back.
- **New projects** (New, and the built-in default) draw a new sequence index and start, then colour the tracks in order, the Main, and the returns. A default set keeps its saved colours and pickers, as the reference's template does; opening a project keeps everything.
- **Preferences** (Theme & Colors, Track and Clip Colors): Auto-Assign Track Colors (on), Default Track Color (a swatch opening the palette, dimmed while Auto-Assign is on), Reduced Automatic Colors (off), and Clip Color (Track Color, or Random). Turning Reduced Automatic Colors on or off restarts the open project's pickers, outside the undo history.
- **Random clip colours.** With Clip Color at Random, an inserted MIDI clip, an imported audio clip (Arrangement or slot), a new slot MIDI clip and recorded takes get their own colour from the sequence, never one of the last two given. Takes recorded together, by one Arrangement pass or by Session recordings that overlap, share one colour. The last two colours are forgotten with the project, as the reference does not save them.

## Not followed

- The Main's colour is kept and drawn from its picker, but the interface still draws the Main in grey.
- The reference's drop paths that give a new track the dropped clip's colour, its rack chains' colours, and its internal Recolor Live Set command.
- Whether the reference's undo restores its pickers was not determined (probably it does); Halftrack's does.

See [open questions](../open-questions.md#automatic-colours) for the defaults taken.

## Evidence in Halftrack

The track-colours suite seeds the generator so it draws sequence index 1153618690 and a start of 3, and checks that a new project like the reference's default Set colours its tracks at palette places 19, 18, 20 and 21, the Main 4 and returns 1 and 14, with the pickers at 7 and 16; that new tracks and a return step on (places 6, 17 and 26) and undo puts the picker back; that a group takes its first track's colour and a track inserted into it the group's, neither stepping the picker; that with Auto-Assign off new tracks, returns and groups take Default Track Color; that Reduced Automatic Colors restarts the pickers among its 11 colours without recolouring; that a saved project reopens with its colours and pickers and one with a picker out of range is refused; that a plain new project steps from a random start; and that with Clip Color at Random eight new clips each take a sequence colour other than the two before, two takes recorded together share one, and the next pass's share another. The UI suite checks the Theme & Colors controls and the preferences they set.
