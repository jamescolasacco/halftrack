# Multi-clip editing contract

Halftrack's multi-clip editing follows the reference's MIDI editor showing several clips at once. One clip is edited and the others are shown dimmed where they fall in the Arrangement.

## Behaviour

- **Showing clips.** In the Arrangement, Shift-clicking a MIDI clip makes it the edited clip and keeps the clips already shown. A plain click on a clip, or a click on empty lane space, shows one clip again.
- **The editor.**
  - The other shown clips' notes are drawn dimmed against the edited clip's start, over a light band marking each clip's length.
  - Inactive notes are fainter.
  - Fit covers the edited clip and the shown clips after it.
- **Changing the edited clip.** Clicking a dimmed note makes its clip the edited one, with that note selected, keeping the view where it was. Edits and Delete then apply to that clip only. The previous clip stays shown.
- **Scope.** Only Arrangement clips are shown together; a Session clip is edited alone. Shown clips can be on different tracks.

## Evidence

The UI suite:
- puts a note in each of two adjacent four-beat clips;
- clicks one clip and Shift-clicks the other through the Arrangement canvas;
- checks that the editor draws the other clip's note four beats on;
- clicks that note and checks its clip becomes the edited one, with the other still shown;
- presses Delete, which removes that note only, and undoes it;
- checks that a plain click returns to one clip.

## Not yet covered

- The view does not scroll before the edited clip's start, so a shown clip that starts earlier is out of view until it is made the edited clip from the Arrangement.
- Looped clips show their notes once rather than at each loop repeat.
- The reference's editing of notes across clips at once, its per-clip loop bars and its focus toggle are missing.
- Session multi-clip editing is missing.
