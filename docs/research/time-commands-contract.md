# Time commands contract

Halftrack's time commands follow the reference's "Using the …Time Commands" section of its Arrangement View chapter (consulted 2026-10-22). Unlike the selection commands, they "act upon all tracks by inserting and deleting time".

## Behaviour

Each uses the Arrangement time selection, or the loop range when nothing is selected. Each is one undo step, refused while recording or with an automated tempo.

- **Insert Silence** (Cmd/Ctrl+I) inserts empty time at its start. Later clips, locators and automation move later; audio clips crossing the point split, MIDI clips open a gap between their notes. A lane whose points lie on both sides holds its value at the point across the gap, so it plays as before up to the point and as before from the gap's end.
- **Delete Time** (Shift+Cmd/Ctrl+Delete) removes the time and closes the gap.
- **Cut Time** (Shift+Cmd/Ctrl+X) deletes the time as Delete Time does and keeps it on the range clipboard, from every track but the returns.
- **Paste Time** (Shift+Cmd/Ctrl+V) inserts the clipboard's length at the insert marker, as Insert Silence does, and pastes the clipboard there: from the first track when it holds every track, otherwise from the selected track, onto tracks of the same kinds.
- **Duplicate Time** (Shift+Cmd/Ctrl+D) inserts a copy of the time just after it. The clipboard is left as it was.
- **The loop brace** moves with the time, as the reference's transport moves it (static reading of its code, 2026-10-25):
  - Inserting moves its end when the end is at or after the point, then its start when that is.
  - Deleting brings each edge after the deleted time's start earlier by the time's length, but not before that start.
  - Its start stays at least a beat before its end. A loop from beat 8 to 24 becomes 4 to 5 when beats 4 to 30 go, and 8 to 40 when Duplicate Time copies beats 8 to 24.
  - Undo leaves it where it went, since the loop is never an undo step.
- **Afterwards**, Delete and Cut Time leave the selection collapsed to the time's start, the insert marker there, as the reference's do.
- **Locators** in deleted time go with it, and later ones move. The reference copies no locators with time.
- **Time signature markers** inside the time go with it, as the reference's manual says the time commands affect the markers in the selected region ([Arrangement View](https://www.ableton.com/en/live-manual/12/arrangement-view/), section 6.11, consulted 2026-10-25). Insert Silence and Delete Time move later markers, Delete and Cut Time remove those inside, the range clipboard keeps them, and Paste and Duplicate Time insert them into the new time. If the inserted markers would change the meter of what follows, a marker at the new time's end gives back the meter it had.

`Session::insertTime`, `deleteTime`, `cutTime`, `pasteTime` and `duplicateTime`. The three new commands are built from the others inside one undo transaction (`Impl::composite`); if a later step is refused, the earlier ones are undone with it and Cut Time's clipboard is put back.

## Evidence

`tests/TimeEditTests.cpp` (`time-edit-acceptance`), at 120 BPM with an audio clip crossing the edit, a later clip, a MIDI clip with notes on both sides, two locators and a volume ramp:

- Insert Silence inside the ramp holds its value across the inserted beat, and one undo restores the ramp;
- Paste Time with nothing copied is refused;
- Cut Time removes the time from every track, its locator and automation, as Delete Time does, with the clipboard holding the second from every track, and one undo restores it;
- Paste Time from the MIDI track onward is refused and leaves no inserted time behind;
- Paste Time at 3 s fills the new second with the copied audio and notes, moves the later clip past it, keeps the earlier locators, moves later automation, and one undo removes it;
- Duplicate Time repeats the second's audio and notes after it and moves later clips and a later locator a second. A render plays the copy sample for sample as the second it copies, after the volume's jump back at its start, which the engine ramps over 15 ms. The clipboard is unchanged, and one undo removes it all.
- With a 3/4 marker at beat 4 and 4/4 again at beat 10, Duplicate Time of beats 2 to 6 repeats the 3/4 marker at beat 8 and moves the 4/4 one to 14, undone in one step; Cut Time of the same beats removes the 3/4 marker, and Paste Time at beat 2 brings it back with a 4/4 marker after the pasted time, undone in one step.

The UI suite runs Cut Time from the Edit menu, then Paste Time and Duplicate Time by their shortcuts, and undoes each in one step. It finds Insert Silence moving the loop brace with the time, and Duplicate Time growing it over the copy.

The time edit suite also moves a loop from beat 8 to 24 as above: to 4 to 5 when beats 4 to 30 go, left there by undo, then to 8 to 40 when 16 beats go in at its end, 10 to 42 for two beats before it, and 10 to 38 when four beats inside it go.

## Not yet

These are recorded in the [open questions](../open-questions.md#time-edit-defaults):

- Locators inside copied time are not copied, and a return's automation is moved but not copied.
- Delete Time joins a lane's points across the deleted time rather than holding its values.
- Tempo changes: the commands need a constant tempo.
