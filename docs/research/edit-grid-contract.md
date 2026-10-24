# The Arrangement's editing grid

Status: partial, ARR-004.

## Sources

The reference's manual describes:

- a zoom-adaptive or fixed grid, whose width is set from the context menu;
- Narrow, Widen, Triplet, Snap and Fixed/Adaptive on Cmd/Ctrl+1 to 5;
- the readout of the step above the time ruler, lower right;
- bypassing snapping with Alt (Windows) or Cmd (Mac), which snaps when snapping is off.

Its keyboard table also gives:

- Option-drag (Mac) or Ctrl-drag to copy;
- Shift+Option or Shift+Alt to slide a waveform.

The reference's own Set files give the defaults, the same in 5314 of the 5320 grids across its 25 installed Sets:

- adaptive;
- a spacing of 20 pixels;
- a fixed value of 1/16;
- no tuplets;
- snapping on.

The rest comes from static reading of the reference's code (Live 12.2.1, arm64). The constants are noted locally in ignored research notes. None of it is measured in the running app.

## Behaviour

`Session::EditGrid` holds the grid: fixed or adaptive, a fixed value, the adaptive spacing in pixels, the tuplet and snapping. `Session::gridValue` gives the step at a zoom.

- **The ladder.** Steps run 512 bars, 256 … 1 bar, then 1/2, 1/4 … 1/16384. A bar follows the meter; a note value is 4/d quarter notes. A tuplet step is 2/n of its note value: a 1/8 triplet is a third of a beat. Bars take no tuplet.
- **Adaptive.** The step is the one whose length is nearest the spacing at the zoom on a log scale. Its plain lines must be more than 8 pixels apart, and it must be shorter than half the view, up to 512 bars.
- **Narrow and Widen** (Cmd/Ctrl+1 and 2, View > Narrow/Widen Grid).
  - Adaptive, they halve or double the spacing. They do nothing where the next step's lines would be 8 pixels apart or closer, or where it would be half the view long.
  - Fixed, they walk the ladder.
- **Triplet Grid** (Cmd/Ctrl+3) toggles triplets, except on a step in bars.
- **Snap to Grid** (Cmd/Ctrl+4) toggles snapping.
- **Fixed Grid** (Cmd/Ctrl+5).
  - Turning it on fixes the grid at its current step.
  - Turning it off makes the grid adaptive at that step's width in pixels: at least 8, at most half the view.
- **The context menu.** Arrangement lanes and clips offer Adaptive Grid, Fixed Grid, Triplet Grid and Snap to Grid.
  - The adaptive widths are computed from half the view's width (Widest) down to 8 pixels (Narrowest). Medium is the geometric mean of the two, Wide of Widest and Medium, and Narrow of Medium and 8. The width nearest the spacing on a log scale is checked.
  - The fixed values are 8 Bars to 1/32.
  - Choosing a width or a value turns snapping on.
- **The readout** at the foot of the Arrangement shows the step: "1/16", "1/8T", "5 : 1/8" for a 1/16 quintuplet, and bars as "1/1" or "2/1". It shows "Off" while snapping is off; a click opens the grid's menu.
- **Snapping.** Edits go to the nearest line. Holding Cmd (Mac) or Alt (Windows, Linux) bypasses snapping, or snaps while it is off. The clip editors use the same bypass modifier.
- **Saving.** The grid is saved with the project as a `HALFTRACKGRID` child of the Edit. A value off the ladder reads as the default. Changing it is neither an undo step nor an unsaved change.
- **Each clip's grid.** As in the reference, every clip has a grid of its own for its editor: `Session::clipGrid`, `setClipGrid` and `ClipInfo::grid`. It is a `HALFTRACKGRID` child of the clip, carried by its copies and saved with it. Its default is the same adaptive grid; 3835 of 3925 clips in the reference's Sets are adaptive.
  - The note editor's Grid button names the grid and opens its menu; this replaces the old 1/4–1/32 snap menu.
  - Cmd/Ctrl+1 to 5 act on the focused editor's grid, otherwise the Arrangement's. The Options menu does the same, as the reference's.
  - The note editor and the audio clip view snap to the clip's grid at their own zoom and width, and drop snapping when it is off. The audio clip view's old rule was the coarsest step at least 12 pixels wide.

## Evidence

`tests/EditGridTests.cpp` (`edit-grid-acceptance`) checks:

- the defaults and the ladder;
- fixed bars in 4/4, 3/4 and 6/8;
- tuplet lengths;
- the readout names;
- the adaptive choice either side of a geometric mean, the 8-pixel floor and half-view ceiling, zooming out to bars, and 3/4 and triplet cases;
- validation;
- that a change is not an undo step and not an unsaved change;
- saving and reopening, and that a damaged value reads as the default.

The UI suite checks, at 80 pixels a beat:

- the readout;
- Cmd/Ctrl+1 halving the spacing and refusing a fourth narrowing past 8 pixels;
- Cmd/Ctrl+2;
- Cmd/Ctrl+5 both ways;
- fixed Narrow and Widen;
- Cmd/Ctrl+3;
- Cmd/Ctrl+4 with the readout reading Off;
- the context menu's Narrow and 1/16 items, and the widths' order and ends;
- a lane click snapping, landing freely with snapping off, and snapping again with the bypass modifier;
- View > Snap to Grid.

The existing trim and locator checks pin a fixed 1/16 grid and drag with the platform's bypass modifier.

For clip grids, the grid suite checks:

- the default;
- a fixed triplet grid shown in the clip's details, leaving the Arrangement's alone and outside undo;
- refusals;
- a duplicate carrying it;
- saving and reopening.

The UI suite checks:

- the note editor's Grid button naming the clip's adaptive grid;
- Fixed and Narrow Grid on the editor's grid changing the clip's and not the Arrangement's, with the button following;
- the button's 1/16 item, which the editor snaps to;
- Snap to Grid, with the button reading Off.

The keys themselves need the editor focused, which only an active app gives, so that check prints a skip in a background run. The note-editing checks fix each clip's grid at the 1/16 they were written for: at their zooms the adaptive grid gives 1/8.

## Differences and gaps

- **Per-meter state.** The reference keeps an adaptive step per time-signature section and moves it with hysteresis. Halftrack chooses afresh at each zoom with no state, which agrees on the power-of-two ladder. They can differ around 1 Bar and 1/2 in meters whose bar is not a power of two.
- **Tuplet commands.** Quintuplets and septuplets are kept and named, but no command chooses them. Bigger/Smaller Tuplet and the "no grid" step past 1/16384 are not offered.
- **Which gestures round down.** The reference rounds some gestures down and others to the nearest line. Which ones round down is unknown, so Halftrack rounds all of them to the nearest line.
- **Expression lanes.** The reference also keeps a separate expression grid per clip, which Halftrack does not have.
