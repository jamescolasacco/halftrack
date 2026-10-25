# Multi-clip editing contract

Halftrack's multi-clip editing follows the reference's MIDI editor showing several clips at once. One clip is edited and the others are shown dimmed where they fall in the Arrangement.

## Behaviour

- **Showing clips.** In the Arrangement, Shift-clicking a MIDI clip makes it the edited clip and keeps the clips already shown. A plain click on a clip, or a click on empty lane space, shows one clip again.
- **The editor.**
  - The other shown clips' notes are drawn dimmed where they play against the edited clip, over a light band marking each clip's length. A note plays from its clip's start marker, and a looped clip's notes show at each pass of its loop until the clip ends, as the Arrangement plays them (2026-10-25).
  - Inactive notes are fainter.
  - The view scrolls over the edited clip and every shown clip, those starting before it too, and Fit covers them all.
- **Changing the edited clip.** Clicking a dimmed note makes its clip the edited one, with that note selected, keeping the view where it was. Edits and Delete then apply to that clip only. The previous clip stays shown.
- **Scope.** Only Arrangement clips are shown together; a Session clip is edited alone. Shown clips can be on different tracks.

## Evidence

The UI suite:
- puts a note in each of two adjacent four-beat clips;
- clicks one clip and Shift-clicks the other through the Arrangement canvas;
- checks that the editor draws the other clip's note four beats on;
- clicks that note and checks its clip becomes the edited one, with the other still shown;
- presses Delete, which removes that note only, and undoes it;
- loops the shown clip's first beat and finds its note at each of the four passes, against the edited clip's beats;
- fits the view with the shown clip starting four beats before the edited one and finds the view starting there, the earlier clip's note in view;
- checks that a plain click returns to one clip.

## Not yet covered

- The reference's editing of notes across clips at once, its per-clip loop bars and its focus toggle are missing.
- Session multi-clip editing is missing.
