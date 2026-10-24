# Note editor clicks and Draw Mode contract

Status: partial. This follows the reference's manual. No reference output was measured.

## Evidence

- Live 12 manual, [Editing MIDI](https://www.ableton.com/en/live-manual/12/editing-midi/), consulted 2026-10-24: a click in the note editor places a flashing insert marker; a click and drag selects a timespan, and the notes it encloses; Shift adds notes to the selection or takes one out; a double-click adds a note, and outside Draw Mode a double-click on a note deletes it. Draw Mode is switched by the control bar's button or B. In it a click and drag adds notes, and a click on a note deletes it. Drawing is melodic by default; Alt (Option on a Mac) draws at one pitch; in melodic drawing a note drawn over is erased, and in pitch-locked drawing moving back toward the first note erases the notes drawn.
- Live 12 manual, [Automation and Editing Envelopes](https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/), consulted 2026-10-24: Draw Mode is also in the Options menu, and holding B while editing with the mouse switches it for the moment.
- The reference's Options menu builder, read statically for the [menu bar contract](main-menu-contract.md), puts Draw Mode (`endr`, shortcut B) after the grid commands, between separators.

## Halftrack's behaviour

Outside Draw Mode:
- A click on the grid places the insert marker on the nearest grid line and clears the selection. Dragging selects the notes the box touches.
- Shift-clicking a note adds it to the selection or takes it out; Shift-dragging adds the notes it touches.
- A double-click on the grid adds a note one grid step long, on the step clicked, at the row's pitch, at the editor's Velocity. It is selected, and the drag that follows moves it. A double-click on a note deletes it. Each is one undo step.

Draw Mode (Options > Draw Mode, B, or the pencil beside the control bar's KEYS switch):
- A drag on the grid draws a note one grid step long on each step from the first to the pointer's. Each takes the pitch the pointer had on its step; when the pointer jumps several steps, the pitch moves evenly between them.
- With Alt held at the start, every note takes the first note's pitch.
- Steps the pointer goes back over lose their notes. A drawn note replaces the notes it covers at its pitch.
- A click on a note deletes it, and the drag goes on deleting the notes it crosses.
- One drag is one undo step. The pointer is a pencil over the grid. Draw Mode is a setting of the window, not saved with the project.

In the lanes, Draw Mode draws too:
- In automation and clip envelopes, steps as wide as the grid, or freehand with Cmd held (Alt elsewhere) or snapping off ([automation contract](arrangement-automation-contract.md#draw-mode-2026-10-24)).
- In the velocity and Chance lanes, each grid step the pointer passes gives its value to the notes starting in it, only the selected notes when there are any. With Cmd held (Alt elsewhere) or snapping off, it sets just the notes under the pointer. With Option held (Alt elsewhere) the values follow a straight line from the press, level with Shift. Each drag is one undo step.

## Not yet

- Holding B to switch Draw Mode for the moment.
- Draw Mode in take lanes.
- The Display & Input setting "Draw Mode with Pitch Lock", which makes pitch-locked drawing the default.
- Changing velocity by dragging a note with Cmd (Alt elsewhere), and Option-dragging copies.
- The timespan a background drag selects: Halftrack selects notes only.

See [open questions](../open-questions.md) for the defaults taken where the manual is silent.

## Evidence in Halftrack

The UI suite checks:
- that a click on the grid places the insert marker and adds nothing, and that a double-click adds a note one grid step long at the clicked step and pitch;
- that a drag selects the note it encloses, and a double-click on a note deletes it, one undo bringing it back;
- that B turns Draw Mode on and off, lighting the pencil, and that Options > Draw Mode shows B and is ticked while on;
- drawing five notes across steps with a pitch change between them, moving back taking notes away, Alt keeping one pitch, and drawn notes replacing a covered note;
- a Draw Mode click deleting a note and the drag erasing more, with one undo for each drag;
- velocities drawn over three grid steps, and along a straight line with Option, each one undo step; and steps drawn in a clip envelope lane;
- that a double-click's note sounds while the button is held with Preview on.
