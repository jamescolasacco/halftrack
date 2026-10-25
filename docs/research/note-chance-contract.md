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

## Not yet covered

A clip that does not loop plays its first variation each time until its graph is rebuilt, where the reference decides on every play. Chance groups, the note inspector's numeric fields, and comparison with the reference's random distribution are missing.
