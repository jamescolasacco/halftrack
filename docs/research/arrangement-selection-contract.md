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

## Interface in the Arrangement

- Dragging over empty lane space selects time, snapped to the grid (Alt bypasses it). Shift-click extends the selection from its far edge.
- Clicking a clip or empty space clears the selection, and the click still moves the playhead.
- With a selection:
  - Cmd/Ctrl+E splits.
  - Cmd/Ctrl+D duplicates, and the selection moves onto the copy so repeating it continues the pattern.
  - Delete removes the material.
  - Cmd/Ctrl+L loops the selection.
  - Insert Silence and Delete Time use it instead of the loop range.
- The Edit menu lists Split, Duplicate Selection, Delete Selection and Loop Selection.
- With a MIDI clip selected and no time selection, Cmd/Ctrl+E splits that clip at the playhead.

## Evidence

`tests/RangeEditTests.cpp` renders a four-second non-periodic source and compares it sample by sample. It checks:

- a MIDI split, with its note and controller layout, end rejection and single-step undo;
- a two-track split that renders exactly like the unsplit clip, with an unselected track untouched;
- deletion, which leaves exact silence and keeps the rest of the render unchanged;
- duplication, where 1.5–2.5 s plays the copy of 0.5–1.5 s and the MIDI copy lands note for note;
- undo of each edit;
- rejection of unknown tracks, invalid ranges, warped clips crossing an edge, and frozen tracks.

The UI suite drags a selection, duplicates it with Cmd/Ctrl+D, deletes it, undoes both, and captures a snapshot.

## Not implemented

- Automation inside the selection, which stays in place for Split, Delete and Duplicate.
- Selecting time on the ruler or across group lanes.
- Cut, Copy and Paste of a time range.
- Consolidate.
- Crop.
- Any comparison with reference output.
