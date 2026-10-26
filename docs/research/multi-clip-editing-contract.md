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
- **Folding.** Fold to Notes keeps the rows with notes in any shown clip, in the Arrangement and the Session alike.
- **Loop bars** (2026-10-26), as the manual describes them (section 10.8):
  - While clips show together, a loop bar for each sits between the ruler and the notes, in its clip's colour. The edited clip's bar is outlined.
  - In the Session, each clip has a row, ordered by track then scene; its bar is its loop from the shared start. In the Arrangement, each track has a row, its clips side by side by time; each bar spans what its clip plays, with its loop along the top.
  - A click on a bar edits its clip. Dragging a loop's end changes that clip's loop length, on the note grid unless the grid is bypassed, in one undo step. The edited clip stays the same.
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
- selects a four-beat and a six-beat Session clip, each with a note at its start, and finds:
  - the six-beat clip's note at beats 0 and 6 over a twelve-beat span, and the edited clip's own note dimmed at beats 4 and 8;
  - a loop bar for each in its own row, pushing the notes down;
  - dragging the six-beat bar's end two beats on making that loop eight beats long, while the edited clip stays the same;
  - a click on that bar editing its clip;
  - the other clip becoming the edited one with both still shown;
  - Fold to Notes keeping the two clips' rows;
  - and one selected clip editing alone again, without loop bars.

## Not yet covered

- Some loop bar features are missing: selecting several bars (Cmd and Shift), duplicating them (Cmd+D and their menu), setting their height, and dragging a loop's start.
- The reference's limit of eight Arrangement tracks is missing.
- Neither the shading of each clip's area outside its loop in Session multi-clip editing nor its loop markers on hover is drawn.
