# Take lanes contract

Halftrack's take lanes follow the reference's [comping chapter](https://www.ableton.com/en/live-manual/12/comping/), consulted 2026-10-21.

## Behaviour

- **Storage.** Each Arrangement track can keep take lanes below its main lane: a `HALFTRACKTAKELANES` child of the track with `TAKELANE` children (id, name), each holding clip states. Take lane clips do not play.
- **Recording.** Stopping an Arrangement recording, for each pass on an armed track:
  - keeps a copy, with fresh identities, in the first take lane holding nothing at or after the pass's start, or in a new lane named "Take N";
  - replaces the main lane's earlier material under the pass, splitting it at the pass's edges. With Options > Arrangement Overdub on, a MIDI pass first takes the notes and controller points of the MIDI clips beneath it, so they play on with the new notes, as the reference's Arrangement Overdub merges MIDI (`Session::arrangementOverdub`, kept with the project, off at first). The control bar's MIDI Arrangement Overdub switch, a plus between Record and Automation Arm as in the reference, shows and sets it too.

  Both are part of the take's undo step.
- **Comping.** `compTake (laneId, start, end)` splits the main lane at the range's edges, removes its material inside, and copies each lane clip's part of the range into it at its own timing (start and offset adjusted). The copies record their source lane. It is one undo step.
- **Cycling.** `cycleTake (trackId, start, end, next)` comps the range from the next or previous lane holding material there, starting from the lane the main lane's range came from. A recorded pass in the main lane counts as coming from the lane that keeps it, so cycling back from a fresh pass reaches the take before it rather than the one already playing.
- **Unused lanes.** `unusedTakeLanes (trackIds)` lists the lanes from which nothing in the main lane came, by comping or recording; `removeTakeLanes` deletes lanes in one undo step. The track header menu's Delete All Take Lanes and Delete Unused Take Lanes use them. The reference counts a lane used when one of its takes, by take id, is in the main lane (0x103827a2c, 0x1024b6770); Halftrack counts the lane each main-lane clip came from.
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
- cycling back from a fresh pass reaching Take 1, and one undo restoring Take 2;
- both takes in use, an inserted empty lane unused and deleted alone, and two undo steps restoring it and undoing the insert;
- inserting, renaming, deleting and undoing lanes;
- that saving puts every take's audio in the assets and the lanes reopen with it.

Auditioning, captured from the synthetic device's output with the track disarmed, is also checked:

- the main lane plays identically twice;
- auditioning Take 2 changes the output while the snapshot still shows the main lane;
- stopping restores the main lane's output exactly;
- an edit while auditioning ends the audition and applies to the main lane, and its undo leaves the output as before.

The main UI suite records two passes and checks Cmd+Option+U, the lane headers, selecting a range, Enter comping, Cmd+Down cycling and Shift+Option+T, the header's audition switch and T.

## Loop recording (2026-10-22)

As the reference's Comping chapter has it, recording round the Arrangement's loop adds a take lane for each pass, reusing lanes with nothing after the punch-in, and the last recorded clip is copied to the main lane. Halftrack does this for audio and MIDI.

How Stop produces the passes:

- **One take.** Stop switches the loop off as the engine's recording ends, so the engine keeps the take as one continuous recording.
- **The split.** `Impl::splitLoopPasses` cuts it into passes: the first runs from where recording began, before the loop if it began there, to the loop's end, then one per time round the loop, the last perhaps partial. With Punch-In or Punch-Out, each pass is then trimmed to the brace, as a take is, so a pre-roll is left out.
- **Audio passes.** Each is a clip of the same file at the loop, its source offset where the pass began. Latency offsets carry through, and no audio is lost between passes.
- **MIDI passes.** Each keeps only its own notes and controller events, moved to its start.
- **The main lane.** The passes are kept in lanes in order, and each replaces what is under it in the main lane. The main lane ends with the last pass and, after a partial last pass, the rest of the one before.
- **Short loops.** A Tracktion patch lowers the engine's two-second loop-recording minimum to 10 ms ([dependencies](../dependencies.md#tracktion-loop-recording-length-patch)).

The engine itself cut its loop recordings into per-pass files but, outside its own project system, referenced only the first. Passes after the first were left on disk unused, and the main lane played the first pass.

Evidence, from `tests/TakeLanesTests.cpp` with the synthetic input and the computer keyboard:

- **From before the loop.** Recording from a second before a one-second loop for 2.5 s makes a two-second first pass from that second, and a half-second second pass, through one file.
- **Audio.** 3.5 passes round a one-second loop fill four lanes, reusing the two free after the loop's start. The passes run one second each and then a half, at offsets 0, 1, 2 and 3 s through one file. The main lane plays the last pass and the third after it, and one undo removes every pass.
- **MIDI.** Three passes round a two-second loop, each with a note, make three lanes each holding its own note half a beat in. The main lane plays only the last pass's note.

## Source highlights and drawing comps (2026-10-25)

Evidence: Live 12 manual, [Comping](https://www.ableton.com/en/live-manual/12/comping/), consulted 2026-10-25.

- Section 21.7: source material for each comp is highlighted in the track's colour in the take lanes, and unused take material is shown "in a desaturated shade of the track's color". Highlights appear only when clips have matching positions and properties. Dragging a highlight's edge moves the split point between two adjacent parts of a comp.
- Section 21.6: in Draw Mode, a click, drag and release copies take material to the main lane in one gesture. With Create Fades on Clip Edges on, adjacent clips get four-millisecond crossfades.

Halftrack:

- **Highlights.** A take lane clip shows the material the main lane plays from it in the clip's colour, and the rest in a desaturated, darker shade of it. A main-lane clip counts where it was comped or recorded from that lane and still plays the same material at the same time: the same file at the same alignment for audio, the same content beat for MIDI. A part moved away loses its highlight. `ClipInfo::compSource` reports the lane a main-lane clip came from.
- **Moving a split.** Dragging a highlight's edge, within 4 pixels, where two audio comp parts meet moves the split between them, each part's edge going with it within its material and at least 10 ms long. It is one undo step.
- **Drawing.** In Draw Mode a drag across a take lane comps the range it covers on release, one undo step.
- **Fades.** Comp parts are new clips and split edges, so Create Fades on Clip Edges already gives them their default fades ([clip fades contract](clip-fades-contract.md)).

The UI suite checks Take 1's highlight over the comped range and Take 2's either side of it, a moved part losing its highlight, a highlight's edge dragged later moving the split in one undo step and undo returning it, and a Draw Mode drag comping a range in one step.

Missing: moving a split between MIDI comp parts, duplicating, reordering and resizing lanes, dragging samples onto lanes, and MIDI comping through the UI.
