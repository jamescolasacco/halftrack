# Multi-clip editing contract

Halftrack's multi-clip editing follows the reference's MIDI editor showing several clips at once. One clip is edited and the others are shown dimmed where they fall in the Arrangement.

## Behaviour

- **Showing clips.** In the Arrangement, Shift-clicking a MIDI clip makes it the edited clip and keeps the clips already shown. A plain click on a clip, or a click on empty lane space, shows one clip again.
- **The editor.**
  - The other shown clips' notes are drawn dimmed where they play against the edited clip, over a light band marking each clip's length. A note plays from its clip's start marker, and a looped clip's notes show at each pass of its loop until the clip ends, as the Arrangement plays them (2026-10-25).
  - Inactive notes are fainter.
  - The view scrolls over the edited clip and every shown clip, those starting before it too, and Fit covers them all.
- **Changing the edited clip.** Clicking another clip's note makes its clip the edited one, keeping the note where it was under the pointer. The previous clip stays shown.
- **Focus** (2026-10-25), as the reference's manual describes it (section 10.8.1). The note editor's Focus button and N switch it, and N held a while switches it back when let go, as the momentary keys do. It is off at first.
  - **On:** only the edited clip's notes edit. The other shown clips' notes are grey, and clicking one selects it alone in its clip.
  - **Off:** every shown clip's notes show in their clip's colour, and select and edit together:
    - a click or Shift-click on another clip's note selects it as an edited note's would, keeping the selection in the other clips, and a drag across them selects them;
    - dragging a selected note moves the selected notes of every shown clip, as the arrow keys do, each staying inside its clip;
    - Delete removes them, and Copy and Cut take them where they show against the edited clip, in one undo step each.
  - Resizing, velocity drags, copy drags, the velocity and chance lanes and the note tools act on the edited clip alone, as the reference's velocity and chance editors do.
- **Session clips** (2026-10-26), as the manual describes them (section 10.8):
  - Selecting several Session slots holding looped MIDI clips shows up to eight together, ordered by track, then scene. The edited clip is the one clicked last. A clip that is not looped is left out, and an edited clip that is not looped shows alone.
  - The clips loop together from their loop starts. The editor shows the passes they take to come round together again: the least common multiple of their loop lengths, on a 960th of a beat, and at most 64 times the longest loop.
  - The other clips' notes show at each pass round their loops, and the edited clip's later passes show dimmed.
  - Clicking another shown clip's note edits that clip and keeps the selection. Focus works as in the Arrangement.
- **Scope.** Shown clips can be on different tracks.

## Evidence

The UI suite:
- puts a note in each of two adjacent four-beat clips;
- clicks one clip and Shift-clicks the other through the Arrangement canvas;
- checks that the editor draws the other clip's note four beats on;
- clicks that note and checks its clip becomes the edited one, with the other still shown;
- presses Delete, which removes that note only, and undoes it;
- loops the shown clip's first beat and finds its note at each of the four passes, against the edited clip's beats;
- fits the view with the shown clip starting four beats before the edited one and finds the view starting there, the earlier clip's note in view;
- with Focus off, Shift-clicks the other clip's note, both notes then selected, and raises both a semitone with Up, drags both a beat later and deletes both, each one undo step;
- turns Focus on with N, finds a click on the other clip's note selecting only that note, and turns it off again;
- checks that a plain click returns to one clip;
- selects a four-beat and a six-beat Session clip, each with a note at its start, and finds the six-beat clip's note at beats 0 and 6 over a twelve-beat span, the edited clip's own note dimmed at beats 4 and 8, the other clip becoming the edited one with both still shown, and one selected clip editing alone again.

## Not yet covered

- The reference's per-clip loop bars, its limit of eight Arrangement tracks, and Fold to Notes over every shown clip are missing.
- Neither the shading of each clip's area outside its loop in Session multi-clip editing nor its loop markers on hover is drawn.
