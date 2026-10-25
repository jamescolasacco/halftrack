# Note chance and velocity range contract

Halftrack's per-note chance and velocity range follow the reference's MIDI note editing, where each note has a chance of playing and a velocity range (also called velocity deviation) that varies its velocity each time it plays.

## Behaviour

- **Values.** `MidiNoteInfo::chance` is 0 to 1 (default 1) and `MidiNoteInfo::velocityRange` is −127 to 127 (default 0). `setMidiNotesChance (clipId, noteIds, chance)` and `setMidiNotesVelocityRange (clipId, noteIds, range)` each set the given notes as one undo step. A note at its default carries no property. Both are note properties, so they are saved, copied with notes through `addMidiNotes`, and validated on load, where a value out of range is refused before the open project is replaced.
- **Playing.** A clip with any note using either value plays 64 variations of its notes, one per pass of its loop, and then repeats them.
  - In each variation, a note plays when a deterministic number drawn from its identity and the pass is below its chance.
  - Its velocity then moves by that pass's fraction of its range, clamped to 1–127.
  - The same note identity always gives the same 64 passes, so a reopened project plays them identically.
  - Grooves apply on top of the variation.
- **The UI.**
  - The piano roll's lane chooser offers Chance after Velocity, with a bar per note that drags from 0% to 100%.
  - In the Velocity lane, Cmd-dragging a bar sets the note's velocity range, shown as a lighter bar beside it; selected notes follow together, as for velocity.

## Engine changes

`cmake/PatchMidiVariations.cmake` adds two engine behaviour hooks, for the number of variations and for creating one. The node builder creates a MIDI clip's sequences from them, as step clips already make probability sequences. It also fixes two faults in the looping generator that variations exposed. A pass played the sequence after the previously cached one, but the generator caches twice a block once a pass's events are done, so passes skipped variations. An empty pass, as a variation can be, stopped playback for good, since nothing advanced past it. See [dependencies](../dependencies.md).

## Release velocity (2026-10-24)

Each note keeps its note-off velocity, 0 to 127, as the engine plays it (`MidiNoteInfo::releaseVelocity`). The note editor's Notes tab offers a Release Velocity lane beside Velocity and Chance, as the reference's editor has one: dragging a note's bar, or drawing across bars in Draw Mode, sets it, one undo step (`Session::setMidiNotesReleaseVelocity`). It is saved with the note, copied with it, written in MIDI exports, and a saved value outside 0 to 127 is refused on load. New notes have 0, the engine's default.

## Evidence

`tests/NoteChanceTests.cpp` renders a looping one-beat Session clip offline, one window per pass, and checks:

- a default note sounding on every pass;
- refusals for chances outside 0–100%, ranges outside ±127 and unknown notes;
- a 50% chance sounding on 26 of 64 passes, the 64 repeating exactly over 128, each pass at the note's own level or silent, undoable in one step;
- a 0% chance never sounding;
- a +63 range playing every pass at 1.03 to 1.97 times the velocity-64 level;
- reopening playing the same passes at the same levels;
- a 200% chance in a saved file refused while keeping the open project;
- release velocities outside 0 to 127 refused, 90 set and undone, written in the exported note-off, and reopened.

The UI suite drags a chance to 50%, a release velocity to the middle and Cmd-drags a velocity range to full velocity, undoing each.

## Probability groups (2026-10-25)

The reference's manual ([Editing MIDI](https://www.ableton.com/en/live-manual/12/editing-midi/), section 10.5.13.1, consulted 2026-10-25): grouped notes share one probability. In a Play All group all the notes play with the probability set; in a Play One group only one note plays at a time, chosen at random, according to the probability. Groups are made from the Clip Content Toolbar's Play All and Play One, the context menu's Group Notes (Play All) and Group Notes (Play One), the Edit menu, or Cmd+G, which makes the type last made from the buttons or menu. Ungroup, Cmd+Shift+G or the menu takes notes out; a note added to another group leaves its first. A grouped note shows the corner triangle even at 100%, the Chance editor shows one marker per group, a diamond for Play All and a triangle for Play One, and hovering a grouped note highlights its group.

Halftrack:

- **The model.** `groupMidiNotes` puts two notes or more in a new group, Play All or Play One, taking them out of other groups, with the chance of the earliest of them (the lowest of those starting together). `ungroupMidiNotes` takes notes out. A group left with one note ends. `setMidiNotesChance` on a grouped note sets its whole group's chance. Each is one undo step. Notes keep their group (`halftrackChanceGroup`) and type (`halftrackPlayOne`) with the project, in copies and in chopped parts; a project with an empty or overlong group name, or a type other than 0 or 1, is refused before it replaces the open one.
- **Playing.** On each of the 64 passes a group decides once from its own identity: below its chance a Play All group keeps all its notes, and a Play One group one of them, chosen from the group's identity and the pass; otherwise none play. A grouped note does not roll alone.
- **The editor.** In the note editor Cmd+G groups the selected notes as the last group made from the menu was (Play All at first) and Cmd+Shift+G ungroups them; elsewhere Cmd+G still groups tracks. The note menu has Group Notes (Play All), Group Notes (Play One) and Ungroup Notes. A note with a chance below 100%, or in a group, shows a triangle in its corner. Pointing at a grouped note outlines every note of its group, and the Chance lane shows one marker for a group, at its earliest note, with a diamond handle for Play All and a triangle for Play One; dragging it sets the group's chance.
- **Not followed.** The toolbar's Play All, Play One and Ungroup buttons, changing a group's type from its marker's menu, and the Status Bar's group type are missing.

`tests/NoteChanceTests.cpp` renders a one-beat loop with a 50% note on its first quarter and another on its third. Ungrouped, they sound on different passes. A group of one note, or with an unknown note, is refused. Grouped Play All, they sound together or not at all on 33 of 64 passes. Setting one note to 100% sets both, and grouped Play One at 100% they never sound together and each sounds on some passes (36 firsts of 64). One undo returns the Play All group; the Play One group reopens; a broken type in a file is refused, keeping the open project; and taking one note of the two out ends the group. The UI suite groups two notes with Cmd+G in the note editor without grouping tracks, finds pointing at one highlighting the group and the Chance lane showing the two as one marker, and ungroups them with Cmd+Shift+G.

## Not yet covered

A clip that does not loop plays its first variation each time until its graph is rebuilt, where the reference decides on every play. The note inspector's numeric fields, the probability groups' toolbar buttons and marker menu, and comparison with the reference's random distribution are missing.
