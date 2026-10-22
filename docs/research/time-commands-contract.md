# Time commands contract

Halftrack's time commands follow the reference's "Using the …Time Commands" section of its Arrangement View chapter (consulted 2026-10-22). Unlike the selection commands, they "act upon all tracks by inserting and deleting time".

## Behaviour

Each uses the Arrangement time selection, or the loop range when nothing is selected. Each is one undo step, refused while recording or with an automated tempo.

- **Insert Silence** (Cmd/Ctrl+I) inserts empty time at its start. Later clips, locators and automation move later; audio clips crossing the point split, MIDI clips open a gap between their notes. A lane whose points lie on both sides holds its value at the point across the gap, so it plays as before up to the point and as before from the gap's end.
- **Delete Time** (Shift+Cmd/Ctrl+Delete) removes the time and closes the gap.
- **Cut Time** (Shift+Cmd/Ctrl+X) deletes the time as Delete Time does and keeps it on the range clipboard, from every track but the returns.
- **Paste Time** (Shift+Cmd/Ctrl+V) inserts the clipboard's length at the insert marker, as Insert Silence does, and pastes the clipboard there: from the first track when it holds every track, otherwise from the selected track, onto tracks of the same kinds.
- **Duplicate Time** (Shift+Cmd/Ctrl+D) inserts a copy of the time just after it. The clipboard is left as it was.

`Session::insertTime`, `deleteTime`, `cutTime`, `pasteTime` and `duplicateTime`. The three new commands are built from the others inside one undo transaction (`Impl::composite`); if a later step is refused, the earlier ones are undone with it and Cut Time's clipboard is put back.

## Evidence

`tests/TimeEditTests.cpp` (`time-edit-acceptance`), at 120 BPM with an audio clip crossing the edit, a later clip, a MIDI clip with notes on both sides, two locators and a volume ramp:

- Insert Silence inside the ramp holds its value across the inserted beat, and one undo restores the ramp;
- Paste Time with nothing copied is refused;
- Cut Time removes the time from every track, its locator and automation, as Delete Time does, with the clipboard holding the second from every track, and one undo restores it;
- Paste Time from the MIDI track onward is refused and leaves no inserted time behind;
- Paste Time at 3 s fills the new second with the copied audio and notes, moves the later clip past it, keeps the earlier locators, moves later automation, and one undo removes it;
- Duplicate Time repeats the second's audio and notes after it and moves later clips and a later locator a second. A render plays the copy sample for sample as the second it copies, after the volume's jump back at its start, which the engine ramps over 15 ms. The clipboard is unchanged, and one undo removes it all.

The UI suite runs Cut Time from the Edit menu, then Paste Time and Duplicate Time by their shortcuts, and undoes each in one step.

## Not yet

These are recorded in the [open questions](../open-questions.md#time-edit-defaults):

- Locators inside copied time are not copied, and a return's automation is moved but not copied.
- Delete Time joins a lane's points across the deleted time rather than holding its values.
- Time signature changes (Halftrack has one meter per project).
