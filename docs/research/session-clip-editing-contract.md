# Moving and copying Session clips

Status: partial, SES-001.

The reference's Session View moves clips around its grid by drag-and-drop and has the Edit menu's Cut, Copy, Paste and Duplicate for clips. Halftrack does the same for one clip at a time. The reference's manual is the source; where it is silent, the defaults below are recorded in [open questions](../open-questions.md#moving-and-duplicating-session-clips).

## Behaviour

**Moving and copying.** `Session::moveSlotClip (fromTrack, fromScene, toTrack, toScene, copy)` moves a slot's clip to another slot, or copies it.

- It goes to a track of its kind: MIDI clips onto MIDI tracks, audio clips onto audio tracks.
- It replaces any clip already in the destination slot.
- A copy is a clip of its own, with new identities for it and its notes.
- Moved within its track, the clip is the same clip: its slot changes places with the destination's. A playing clip plays on without interruption.
- Moved to another track, it is copied there and removed from the source, so it stops if it was playing.
- Frozen tracks, recording slots, a recording transport and exports refuse it.
- It is one undo step.

**Duplicate.** `duplicateSlotClip` copies the clip into the slot below when that slot is empty. Otherwise, or from the last scene, it inserts a new scene below for the copy, and the rows below move down. One undo step removes the copy and any scene made for it.

**Clipboard.** `copySlotClip` copies a slot's clip, and with `cut` also removes it (one undo step). `pasteSlotClip` puts a copy into a slot of the clip's kind, replacing what is there (one undo step).

- An audio clip's file is kept in a copy of its own, so it pastes into another project. The paste collects it into that project's assets, as an Arrangement audio paste does.
- There is one clipboard, as in the reference, so a copied clip pastes in either view:
  - A slot clip's copy also pastes into the Arrangement at the insert marker (`pasteMidiClip`, `pasteAudioClip`).
  - An Arrangement clip's copy also pastes into a slot, taking the preferences' launch settings.
  - A MIDI clip keeps its length in beats and its content start's beat either way, as the reference's manual describes for moving material between the views.
  - A time selection's copy clears the Session form.

**In the UI.**

- Dragging a clip by its name to another slot moves it, outlining the slot it will land on. Holding Option (Mac) or Ctrl (Windows, Linux) copies it, with the copying cursor.
- With the grid focused, Cmd/Ctrl+X, C, V and D cut, copy, paste and duplicate the selected slot's clip. The Edit menu's Cut, Copy, Paste and Duplicate do the same with the slots last clicked.
- A moved or duplicated clip's slot is selected.

## Several slots (2026-10-22)

The reference's manual:

- Shift-click selects adjacent clips and Ctrl/Cmd-click nonadjacent ones.
- One can click into an empty slot and rubber-band select from there.
- Several clips selected move together when dragged.

Halftrack:

- **Selecting.** Shift-click takes in the rectangle of ordinary tracks' slots from the selected slot; Cmd-click (Ctrl elsewhere) adds a slot, or on release without a drag leaves one out. A drag from an empty slot draws a rectangle. A plain click on a slot of the group keeps the group for a drag and, released without one, selects that slot alone.
- **Moving and copying.** Dragging one of the group's clips moves all its clips by the same tracks and scenes (`Session::moveSlotClips`), copying with the copy modifier. The moves run in an order that never lands a clip on a group member not yet moved, and the selection follows. A move that would leave the grid or put a clip on a track of the wrong kind is refused whole.
- **Delete** clears the group (`clearSlots`).
- **Cmd/Ctrl+C and X** copy or cut the group's clips keeping their places from the top-left one (`copySlotClips`). Cmd/Ctrl+V pastes them with the top-left clip at the selected slot, all or none.
- **Cmd/Ctrl+D** copies the group into the slots below its span when those are empty.

Each is one undo step.

## Evidence

`tests/SlotClipTests.cpp` (`slot-clips-acceptance`) checks:

- **Moving within a track.** A clip moves within its track keeping its identity, and replaces a clip it lands on; each move undoes in one step.
- **Copying across tracks.** Copies across and within tracks have their own clip and note identities, and the originals are kept.
- **Refusals.** MIDI clips do not go to audio tracks or audio clips to MIDI tracks; an empty slot has nothing to move.
- **A playing clip, within its track.** A MIDI clip holding a whole-clip note, moved down its track while playing, renders with 0.0 difference from the same performance without the move, at 48 kHz in 128-sample blocks.
- **A playing clip, to another track.** Moved to another track, it stops.
- **Duplicate.** It fills an empty slot below. With that slot taken it inserts a scene and moves the rows below down, and from the last scene it adds one. One undo removes the copy and its scene.
- **Clipboard.** Copy, cut and paste work, a paste replaces the clip it lands on, and MIDI is refused on an audio track. Undo reverses a paste and a cut.
- **One clipboard.** An Arrangement copy and a Session copy replace each other on the clipboard.
- **Another project.** An audio clip pasted into a newly saved project has its file collected there.
- **Several slots (`groupTests`).**
  - Three clips over two tracks move down a scene together, with none landing on another; copy three scenes down keeping their shape; and undo in one step.
  - Moves off the grid or onto the audio track are refused, changing nothing.
  - A group copy pastes with its shape, undone in one step; pastes that would leave the grid or land on the audio track are refused.
  - The group clears and comes back with one undo, and a two-clip cut pastes in order on another track.
- **Between the views.**
  - A three-beat Arrangement MIDI clip with a note on its second beat, pasted into a slot, keeps the note and its three beats, with a new identity.
  - Dragged across (`copySlotClipToArrangement`, `copyClipToSlot`), a four-beat slot clip lands in the Arrangement at beat 25 with its note, in one undo step, its slot keeping it, and the clipboard keeps what was copied before. A MIDI clip is refused on an audio track. The three-beat Arrangement clip dragged into an empty slot becomes a Session clip there, and two undo steps take both copies back.
  - A four-beat slot clip pasted into the Arrangement at beat 17 keeps its four beats and note.
  - A slot audio clip pasted into the Arrangement at one second has its audio collected.

For several slots, the UI suite checks:

- a Shift-click rectangle, and a Cmd/Ctrl-click leaving a slot out;
- a group dragged two scenes with its selection, and Delete clearing it, undone;
- Cmd/Ctrl+D copying the group into the empty slots below its span and selecting the copies;
- Cmd/Ctrl+C and V pasting the group at the selected slot;
- a rubber band drawn from an empty slot.

The UI suite drags a clip to another slot and copy-drags one with the platform's modifier. It also checks Cmd/Ctrl+C, V, D and X on the grid, pasting a clip into a slot, and Cmd/Ctrl+V in the Arrangement pasting the copied Session clip at the insert marker, with nine undo steps restoring the project.

## Dragging between the views (2026-10-24)

The reference's manual: clips move "from the Session View to the Arrangement and vice versa" by dragging them over the view selectors ([Session View](https://www.ableton.com/en/live-manual/12/session-view/), section 7.5, consulted 2026-10-24).

- **Over the selector.** A slot clip dragged over the Arrangement selector switches to the Arrangement, and the drag goes on there, outlining where the clip would land on the lane under the pointer, at the snapped time. Back over the Session selector, the drag returns to the grid.
- **From the Arrangement.** An Arrangement clip dragged over the Session selector switches to the Session: the move made so far is taken back, and the drag outlines the slot under the pointer. Back over the Arrangement selector, it moves along its track again.
- **The drop.** Released over a lane of the clip's kind, or over a slot, it leaves a copy there, as the clipboard's copy and paste do, in one undo step, without touching the clipboard. The clip it came from stays. Released anywhere else, nothing changes.
- A group of selected slots or clips moves only within its view.

The UI suite drags a slot clip over the Arrangement selector and onto a lane, and an Arrangement clip over the Session selector and onto a slot, checking the view switches, the aims, the copies and their notes, the originals kept, and the undo steps.

## Several scenes (2026-10-25)

The reference's manual ([Session View](https://www.ableton.com/en/live-manual/12/session-view/), section 7.2, consulted 2026-10-25):

- Shift-click selects adjacent scenes and Ctrl-click nonadjacent ones.
- Scenes reorder by drag-and-drop. A selection of nonadjacent scenes collapses together when dropped.
- Ctrl with the up or down arrow keys moves nonadjacent scenes without collapsing them.

The manual does not say what Duplicate or Delete do to several scenes.

Halftrack:

- **Selecting.** Shift-click adds the scenes from the selected scene to the clicked one. Cmd-click (Ctrl elsewhere) adds a scene, or leaves out one already selected. A plain click on one of them keeps them for a drag and, released without one, selects that scene alone. They stay selected while the scene they were chosen with stays selected; a click on a slot, the arrow keys or any other selection drops them.
- **Moving.** Dragging one of them drops them all together, in their order, where the drag ends. Cmd+Up and Cmd+Down (Ctrl elsewhere) move them a row, keeping the gaps between them; nothing moves when one is already at the top or bottom.
- **Commands.** Delete and Duplicate act on all of them, from the menu of one of them, from the Edit menu, or by Delete and Cmd/Ctrl+D after a click on a scene. Each copy goes below the scene it copies, and the copies are then selected. A colour chosen from the menu colours them all.

Each is one undo step. After a click on a scene, Delete and Cmd/Ctrl+D act on the scenes even when only one is selected; before, they acted on the selected slot.

The UI suite Shift-clicks and Cmd/Ctrl-clicks scenes, directly and through the launcher's own mouse handlers. It moves two nonadjacent scenes with Cmd+Down keeping their gap, drops them at the top by a drag, colours both, and deletes and duplicates both from the menu and the keys. It checks each undo step and that a slot click drops the other scenes.

## Not yet covered

- Pasting a time selection (several tracks' clips) into the Session, which the reference lays out top to bottom, and dragging several clips or slots between the views.
- Dragging a clip onto a group column or a scene's launch button.
- Shift with the arrow keys extending a scene selection, launching several selected scenes, and Cut, Copy and Paste of scenes (the reference's manual, section 7.4.3, lists them among the Edit commands that apply to scenes). After a click on a scene, Cmd/Ctrl+C, X and V still act on the selected slot.
