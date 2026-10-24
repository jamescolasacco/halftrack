# Arrangement time selection contract

The Arrangement has a time selection, after the reference's selection editing ([Arrangement View: Selection](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-20). Dragging over empty lane space selects a time range across the tracks between the drag's start and end. This is Halftrack's own implementation; command behaviour at the edges follows simple defaults, not measurements.

## Interface

- `splitMidiClip(clipId, seconds, rightId)` splits an unlooped Arrangement MIDI clip at least a 960th of a beat inside it. Both halves start at zero offset. Notes crossing the split become two notes, and each controller's value at the split becomes a point at the start of the right-hand clip.
- On a set of tracks, each in one undo step:
  - `splitRange(trackIds, start, end)` cuts every clip crossing either edge.
  - `deleteRange` removes the material inside the selection, leaving later clips in place.
  - `duplicateRange` copies the material inside to just after the selection, replacing whatever was there.
- Every clip crossing an edge must be splittable there (unwarped and outside its fades for audio, unlooped for MIDI); otherwise nothing changes. These are also rejected:
  - frozen tracks with affected clips;
  - unknown tracks;
  - ranges shorter than 1 ms, non-finite, or past the 48-hour timeline;
  - edits while recording or exporting.
- Return tracks in the set are ignored.
- Automation on the selected tracks (mixer and device lanes, including devices in racks) follows the edit:
  - Duplicate copies each lane's shape over the selection into the time after it. One-millisecond edge points let the value change at the copy's edges, and the original resumes after it.
  - Delete clears each lane inside the selection, keeping its values at both edges.
  - Split and Consolidate leave automation unchanged.
- `consolidateRange(trackIds, start, end, newClipIds)` replaces each track's material in the selection with one clip spanning it, in one undo step:
  - MIDI clips join into one MIDI clip that holds their notes and controller points at their original times.
  - Audio clips render into a new 32-bit WAV in the project's Assets at the audio device's rate, through the engine's renderer without the track's devices or mixer. Clip gain, fades, warping and gaps are baked in, and the new clip plays unwarped.
  - The render solos the track and unmutes it outside history, then restores both, so muted tracks consolidate too.
  - Tracks without clips in the selection are left alone, and a selection with no clips is rejected.

## Interface in the Arrangement

- Dragging over empty lane space selects time, snapped to the grid (Cmd on the Mac, Alt elsewhere, bypasses it). Shift-click extends the selection from its far edge.
- Clicking a clip or empty space clears the selection, and the click still moves the playhead.
- With a selection:
  - Cmd/Ctrl+E splits.
  - Cmd/Ctrl+D duplicates, and the selection moves onto the copy so repeating it continues the pattern.
  - Delete removes the material.
  - Cmd/Ctrl+L loops the selection.
  - Cmd/Ctrl+J consolidates it.
  - Insert Silence and Delete Time use it instead of the loop range.
- The Edit menu's Cut, Copy, Paste, Duplicate and Delete act on the selection, beside Loop Selection, Split and Consolidate (see the [main menu contract](main-menu-contract.md)).
- With a MIDI clip selected and no time selection, Cmd/Ctrl+E splits that clip at the playhead.

## Evidence

`tests/RangeEditTests.cpp` renders a four-second non-periodic source and compares it sample by sample. It checks:

- a MIDI split, with its note and controller layout, end rejection and single-step undo;
- a two-track split that renders exactly like the unsplit clip, with an unselected track untouched;
- deletion, which leaves exact silence and keeps the rest of the render unchanged;
- duplication, where 1.5–2.5 s plays the copy of 0.5–1.5 s and the MIDI copy lands note for note;
- undo of each edit;
- the exact point layout of a duplicated hold-and-rise volume lane and of a cleared one, with undo;
- consolidation at 44.1 kHz of a muted track whose clips have −6 dB gain, a fade-in and a gap, rendering identically to the original clips, from a 3.5 s file in Assets, with two MIDI clips joined note for note and controller point for point, and undo;
- rejection of unknown tracks, invalid ranges, warped clips crossing an edge, and frozen tracks.

The UI suite drags a selection, duplicates it with Cmd/Ctrl+D, deletes it, undoes both, and captures a snapshot.

## Cut, Copy and Paste of a time range (2026-10-22)

`Session::copyRange (trackIds, start, end, cut)` keeps what the selected tracks (and their linked tracks) hold in the range. Copying changes nothing in the project and adds no undo step.

- **What is kept.** The clipboard holds each track's clips that overlap the range whole, in the order the tracks are shown. Each audio file is copied once to a temporary file, so the clipboard outlives its project.
- **What can be copied.** Clips crossing an edge must split there, as for Split. Comped audio takes are refused.
- **Cutting** also deletes the range, as Delete Selection does.
- **Exclusive.** A range copy clears the single-clip clipboards, and a clip copy clears the range.
- **Automation.** Each automated control's shape over the range comes too, with its values at the edges. A control is known on another track by its device's type and name, its place among such devices (through racks), and its parameter. That is the track mixer's volume and pan on any track, and the same device in the same place.

`Session::pasteRange (firstTrackId, start)` places the copy from that track onward, one undo step.

- **Tracks.** Each copied track goes, in order, to the next track that is not a return. Kinds must match, audio onto audio and MIDI onto MIDI, and there must be enough tracks.
- **What is replaced.** The pasted span on each destination is cleared first, splitting clips at its edges. The copied clips are inserted at the shifted time and cut back to the span, so only the range is pasted.
- **Media.** The copied audio is collected into the project's assets.
- **Automation.** Each copied shape replaces the span on the same control there: held up to it, the copied points inside it, and the control's own value again after it. Controls without a match are left alone.

**Interface.** With a time selection, Cmd/Ctrl+C and Cmd/Ctrl+X copy and cut it. Cmd/Ctrl+V pastes a copied range at the insert marker from the selected track, and selects the pasted time on the tracks it landed on. The Edit menu offers Cut Selection, Copy Selection and Paste Selection.

**Evidence.** `tests/RangeClipboardTests.cpp` renders each track's stem as a pattern of loud, quiet and silent beats. The tracks are an audio track of alternating loud and quiet clicks over a MIDI track of the same pattern. It checks:

- copying two beats from both tracks changes nothing, and pasting them four beats on plays them there on each track;
- the pasted audio clip holds just the range, with its audio collected into the project;
- undo and redo of the paste;
- a paste over existing beats replaces just its range;
- refusals: too few tracks, audio onto MIDI, a missing track and an empty range;
- a cut, which removes just the range, and its paste back;
- a paste into a new project, from audio collected into it;
- clip and range copies clearing each other;
- a volume dip copied with the range and pasted five beats on, the original kept, the volume returning after it, and undone with the clips.

The UI suite copies a beat with Cmd/Ctrl+C, pastes it at the insert marker with Cmd/Ctrl+V (selecting it), and cuts it with Cmd/Ctrl+X.

## Selecting several clips (2026-10-22)

The reference's keyboard table names Shift-click for adjacent clips and Cmd-click (Ctrl on Windows) for nonadjacent ones; Option-drag (Ctrl on Windows) copies. Halftrack follows it in the Arrangement:

- **Shift-click** on a clip adds it to the selection, with every clip lying wholly in the span from the selection to it, across the tracks between.
- **Cmd-click** (Ctrl elsewhere) adds a clip, or on release without a drag leaves a selected one out.
- **A plain click** on a clip of a group keeps the group for a drag; released without one, the clip is selected alone. A click on empty lane space clears the group.
- **Dragging** one of a group moves every clip of it, all by the dragged clip's snapped move and change of track, as one undo step. A group with any clip that would land on a track of the wrong kind does not move.
- **A copying drag** (Option, or Ctrl) of one clip or a group leaves copies where they were. Copies use `Session::copyClipTo`, and a MIDI copy keeps its beats.
- **Delete** removes the group, as one undo step.
- **Cmd/Ctrl+D** repeats the group after itself, each copy keeping its place, and selects the copies.
- **Cmd/Ctrl+C and X** keep exactly the group's clips, in their places across the tracks they span, through `copyRange`'s `onlyClips`, without the tracks' automation. Cut removes just them. Cmd/Ctrl+V pastes them like a copied time selection.

Moving a MIDI clip now keeps its length in beats and its content start's beat at the tempo where it lands, as the reference keeps a MIDI clip's beats. Before, a clip moved past a slowing tempo kept its seconds and lost beats.

Evidence. The range clipboard suite covers:

- copying the first and third of three clips alone, the paste holding just those;
- cutting just them, undone in one step;
- `copyClipTo` making a clip of its own that plays, undone in one step, and its refusals;
- a copy and a move past a change to 60 BPM keeping their one beat.

The UI suite checks:

- a Shift-click taking in the clip between, and Cmd/Ctrl-click leaving one out;
- a group drag of four beats;
- Cmd/Ctrl+D selecting the copies;
- Delete;
- a copying drag;
- Cmd/Ctrl+C and V of two clips, pasting just them;
- nine undo steps restoring the project.

## Not implemented

- Shift-click that selects part of a clip as time, as the reference's does. (Dragging over empty space selects time, as in the reference; there is no rubber-band selection of clips in its Arrangement.)
- Selecting time on the ruler or across group lanes.
- Pasting automation onto controls the destination lacks: such shapes are skipped.
- Crop.
- Any comparison with reference output.
