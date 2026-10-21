# Linked-track editing contract

Halftrack's linked-track editing follows the reference's Linked-Track Editing (Live 12 manual, Arrangement View, section 6.14). Linked tracks are edited as one, for example the microphones of a multitrack drum recording. The manual lists the edits that apply across a link but not how corresponding clips are matched. The matching rules below are Halftrack's defaults, recorded in [open questions](../open-questions.md#linked-track-matching).

## Linking

- **Link Tracks.** Selecting two or more audio or MIDI track headers and choosing Link Tracks from a header's context menu links them. Return tracks cannot be linked.
- **Separate links.** The selected tracks become one link of their own, leaving any link they were in. A link left with one track ends.
- **Joining a link.** Holding Cmd/Ctrl while choosing Link Tracks on a linked track's header adds the selection to that track's link.
- **Unlinking.** Unlink Track (or Unlink Tracks for several selected tracks) takes the selected tracks out of their links.
- **Undo and saving.** Linking and unlinking are undo steps. Links are saved with the project, as a `halftrackLink` track property.
- **Duplicates.** A duplicated track starts unlinked.
- **Load validation.** On load, a link must be a short identifier without spaces, on an audio or MIDI track. Otherwise the project is refused and the open one is kept.
- **Indicator.** A linked track's header shows a link indicator. Pointing at it outlines the tracks in its link, and clicking it selects them.

## Linked edits

An edit on one linked track applies to the other tracks in its link. The edit and everything it causes form one undo step. If any linked part cannot be done (for example, a linked track is frozen, or a linked clip would leave its source), the whole edit is refused and nothing changes.

| Edit | On the linked tracks |
| --- | --- |
| Move a clip in time | Clips that start and end with it (within 1 ms) move by the same amount. A move to another track moves only that clip. |
| Trim an audio clip | Linked audio clips that start and end with it have each edge and their source offset moved by the same amounts. |
| Split a clip | Each linked track is cut at that time, whatever clip crosses it (audio or MIDI). |
| Delete, activate or deactivate a clip | Clips that start and end with it do the same. |
| Fades | Linked audio clips that start and end with it take a fade-in change. They take a fade-out change if their fade-out starts where the edited clip's did. They keep their own gain and pitch. |
| Time-selection edits (Split, Delete, Duplicate, Consolidate) | The linked tracks are included. Selecting time on a linked track selects it on the others. |
| Arm and disarm | Linked tracks that have an input arm and disarm with it. |
| Take lanes | Inserting a lane inserts one on every linked track. Deleting or renaming a lane does the same to the lane at that position on each linked track. |
| Comping | Each linked track comps its lane at the same position, over the same range, where that lane holds material there. Cycling takes runs once per link. |

Selecting a clip on a linked track shows the linked clips as selected.

## Evidence

`tests/LinkedTrackTests.cpp` builds Kick and Snare (linked), Room (unlinked) and a MIDI track, and checks:

- **Linking.** One track alone and a return track are refused. Linking shows on both tracks, with undo and redo.
- **Clip edits:**
  - a split cuts Snare too and not Room, and one undo joins both;
  - a move carries the matching clip, from either track. An unmatched clip moves alone, and a move to another track leaves the linked clip;
  - a trim matches on Snare;
  - a fade-in on Kick reaches Snare without Kick's gain, and a fade-out set on Snare reaches Kick. One undo removes both;
  - deactivating, activating and deleting carry across, and one undo restores both clips;
  - with Snare frozen, deleting Kick's clip is refused whole.
- **Time selections.** Deleting time on Kick also removes Snare's clip in the range, while Room keeps its clip.
- **MIDI.** A linked MIDI track is split with the audio.
- **Duplicates and unlinking.** A duplicate starts unlinked. Unlinking one track leaves the rest linked, a link with one track left ends, and undo restores it.
- **Recording.** With the synthetic device split into two mono inputs:
  - arming Kick arms Snare, while Room, which has no input, stays unarmed;
  - two passes give each track two take lanes;
  - comping Kick's Take 1 comps Snare's own Take 1 over the same range, and one undo restores both;
  - inserting, renaming and deleting a lane carry across, and disarming Snare disarms Kick.
- **Saving.** The link reopens from a saved project, and a project whose link contains spaces is refused, keeping the open one.

The UI suite links two selected headers through Link Tracks. It checks the indicator appears, that pointing at it marks the link and clicking selects both tracks, that a time selection covers the linked track, and that Unlink Tracks ends the link.

## Not yet covered

- Auditioning a take lane on linked tracks together.
- Resizing MIDI clips with linked audio.
- Moves of linked clips to other tracks.
- Linking from a group track's header.
- Cut, Paste and Insert Silence of a time selection beyond what the time commands already do on all tracks.
- Comparison of the matching rules against the reference.
