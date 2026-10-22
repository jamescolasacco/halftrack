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
- With the grid focused, Cmd/Ctrl+X, C, V and D cut, copy, paste and duplicate the selected slot's clip. The Session menu offers the same as Cut Clip, Copy Clip, Paste Clip and Duplicate Clip.
- A moved or duplicated clip's slot is selected.

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
- **Between the views.**
  - A three-beat Arrangement MIDI clip with a note on its second beat, pasted into a slot, keeps the note and its three beats, with a new identity.
  - A four-beat slot clip pasted into the Arrangement at beat 17 keeps its four beats and note.
  - A slot audio clip pasted into the Arrangement at one second has its audio collected.

The UI suite drags a clip to another slot and copy-drags one with the platform's modifier. It also checks Cmd/Ctrl+C, V, D and X on the grid, Session > Paste Clip, and Cmd/Ctrl+V in the Arrangement pasting the copied Session clip at the insert marker, with nine undo steps restoring the project.

## Not yet covered

- Selecting and moving several clips at once (Shift/Cmd-click, rubber-band selection).
- Dragging clips between the views, and pasting a time selection (several tracks' clips) into the Session, which the reference lays out top to bottom.
- Dragging a clip onto a group column or a scene's launch button.
