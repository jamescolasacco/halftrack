# Take lanes contract

Halftrack's take lanes follow the reference's [comping chapter](https://www.ableton.com/en/live-manual/12/comping/), consulted 2026-10-21.

## Behaviour

- **Storage.** Each Arrangement track can keep take lanes below its main lane: a `HALFTRACKTAKELANES` child of the track with `TAKELANE` children (id, name), each holding clip states. Take lane clips do not play.
- **Recording.** Stopping an Arrangement recording, for each pass on an armed track:
  - keeps a copy, with fresh identities, in the first take lane holding nothing at or after the pass's start, or in a new lane named "Take N";
  - replaces the main lane's earlier material under the pass, splitting it at the pass's edges.

  Both are part of the take's undo step.
- **Comping.** `compTake (laneId, start, end)` splits the main lane at the range's edges, removes its material inside, and copies each lane clip's part of the range into it at its own timing (start and offset adjusted). The copies record their source lane. It is one undo step.
- **Cycling.** `cycleTake (trackId, start, end, next)` comps the range from the next or previous lane holding material there, starting from the lane the main lane's range came from.
- **Managing lanes.** `insertTakeLane`, `renameTakeLane` and `removeTakeLane` are each one undo step.
- **Saving and media.** Saving collects take lane audio into the project's assets. Loading anchors it to absolute paths. Missing Media lists and relinks it.
- **Auditioning.** `auditionTakeLane (laneId)` plays a lane in place of its track's main lane, through the track's devices; `stopAuditioning` ends it. The main lane's clip states are set aside at their places in the track while copies of the lane's clips play, with no undo history. The same states return, so earlier undo steps stay valid. The snapshot keeps showing the main lane and names the auditioned lane in `TrackInfo::auditionLane`. Any edit (through the Session's transaction start), undo, redo, recording, saving or exporting ends the audition first; replacing the project drops it.
- **The UI:**
  - Cmd+Option+U (View > Show Take Lanes) shows the selected track's lanes below it, each with a header that renames on double-click and deletes with x;
  - dragging in a lane selects a range, and Enter comps it;
  - Cmd+Up/Down cycles a time selection's range on its tracks;
  - Shift+Option+T (Create > Insert Take Lane) adds a lane;
  - each lane header's speaker switch, or T for the selected lane, toggles its audition.

## Evidence

`tests/TakeLanesTests.cpp` records through the synthetic device and checks:

- that a pass plays in the main lane and is kept in Take 1, with one undo removing both;
- that a second pass over the same time gets Take 2, and a later pass reuses Take 1;
- that comping Take 1 into 0.25–0.5 s plays it there at its own offset, with Take 2 around it, and that one undo restores it;
- cycling forward and wrapping, and refusal where no lane covers the range;
- inserting, renaming, deleting and undoing lanes;
- that saving puts every take's audio in the assets and the lanes reopen with it.

Auditioning, captured from the synthetic device's output with the track disarmed, is also checked:

- the main lane plays identically twice;
- auditioning Take 2 changes the output while the snapshot still shows the main lane;
- stopping restores the main lane's output exactly;
- an edit while auditioning ends the audition and applies to the main lane, and its undo leaves the output as before.

The main UI suite records two passes and checks Cmd+Option+U, the lane headers, selecting a range, Enter comping, Cmd+Down cycling and Shift+Option+T, the header's audition switch and T.

Missing: source highlights, comp crossfades, splitting loop-recorded takes into lanes, duplicating, reordering and resizing lanes, dragging samples onto lanes, and MIDI comping through the UI.
