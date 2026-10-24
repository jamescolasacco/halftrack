# Track header menu contract

Status: partial. This follows a static reading of the reference's track header menu. No reference output was measured.

## Evidence

A research agent read the reference statically on 2026-10-23:
- the builder `ATrackHeader::FillContextMenu` (0x1032c7ba8);
- `LContextMenu::AddContextMenuEntry` (0x1016c6f2c) and its separator rule (0x1016c7c64);
- the Arrangement and Session headers' command handlers, enable callbacks and predicates;
- Duplicate (0x10243278c), Delete (0x1032cdd64, CanDeleteTracks 0x10242c070) and the colour grid (0x1032c8f28).

Nothing was launched, no preferences or Sets were read, and nothing was decrypted. The English labels are in the reference's encrypted language file, so Halftrack's labels are its own plain names for the commands.

## The reference's rules (high)

- Most items are commands. A command that cannot act is left out of the menu rather than shown disabled. The exceptions are Delete All Take Lanes and Delete Unused Take Lanes, which appear disabled.
- Items built directly always appear, disabled when they cannot act: Select Content, Link and Unlink Tracks, Assign Track Color to Clips and Save as Default.
- A separator is added only when the menu has an item and its last item is not a separator.
- Commands act on the selected tracks. Rename, Edit Info Text, Save as Default and Copy LOM Path act on the clicked track.

## Order

The reference's Arrangement menu, with what Halftrack shows:

| Reference item | When | Halftrack |
|---|---|---|
| Cut, Copy, Paste | Copy for tracks and groups that are not Main nor in a frozen group; Cut when Copy and Delete can act; Paste with tracks copied | the same over the tracks Halftrack copies (`copyTracks`, `cutTracks`, `pasteTracks`; returns and frozen tracks not yet). Paste puts the copies right before the first selected track, in its group, as the reference's does, and selects them. After a header or title click, Cmd+C, Cmd+X and Cmd+V do the same |
| Duplicate | see the [groups contract](groups-contract.md#duplicating-2026-10-23) | Duplicate, when `canDuplicateTracks` |
| Delete | see [deleting](groups-contract.md#deleting-2026-10-23) | Delete, when `canRemoveTracks` |
| Clear All Envelopes | Automation Mode | not yet: left out |
| Rename | not for Main | Rename: the name edits in place in the Arrangement, in a dialog from a Session title |
| Edit Info Text | the header has editable info text | Edit Info Text, for the clicked track or group: a dialog edits its own text (`setInfoText`, saved, one undo step), which the Info view shows while pointing at its Arrangement header, Session title or Session mixer strip |
| Freeze / Unfreeze, plural for more than one | selected tracks that can freeze; Unfreeze when all are frozen | the same, over the selected tracks with Arrangement clips or frozen. Each track's freeze is its own undo step |
| Bounce Track in Place | | not yet. Halftrack keeps its Flatten for a frozen clicked track there until it has Bounce Track in Place; the reference's 12.2.1 has no Flatten |
| Insert Audio Track, Insert MIDI Track | always | the same |
| Insert Return Track | fewer than 12 returns | the same; `addReturnTrack` refuses a thirteenth too (`maximumReturnTracks`) |
| Group Tracks, Ungroup Tracks | the grouping rule; one selected group | the same |
| Select Content | always shown; enabled if a selected track has Arrangement content | the same, selecting the time the selected tracks' clips cover on those tracks (members of a selected group included) |
| take lanes: Insert Take Lane, Show Take Lanes, Delete All Take Lanes, Delete Unused Take Lanes | an audio or MIDI track's header, Automation Mode off | Insert Take Lane, Show Take Lanes (ticked when all selected tracks show theirs) and Delete All Take Lanes (one undo step, `removeTakeLanes`). Delete Unused Take Lanes is not yet there |
| Link Tracks, Unlink Tracks | audio, MIDI and group tracks | for audio and MIDI tracks; Halftrack's groups do not link yet |
| colour grid | always | Halftrack's own palette in the reference's layout, 5 rows of 14. A colour in use by a selected track is marked, and picking one colours every selected track and group in one undo step (`setTrackColours`) |
| Assign Track Color to Clips | | not yet: Halftrack's clips have no colours of their own |
| Save as Default Audio/MIDI Track | | not yet |
| Copy LOM Path | one track selected | left out: Halftrack has no object model for it |

In the Session view, a track's or group's title has the same menu without Clear All Envelopes, the take lane items and the link items. Select Content is enabled there when every selected track is audio or MIDI, and it selects their Session clips. The reference ends the Session menu with two items that show or hide a rack's chains in the mixer, shown only where they apply; Halftrack's mixer has no such strips.

Right-clicking a header selects its track or group unless it is selected, keeping the selection then. The reference's menu code only marks the clicked track; how it treats the rest of the selection was not determined (see open questions).

## Evidence in Halftrack

The UI suite checks:
- the exact items, order and separators of a nested track's Arrangement menu, its Session title menu, and a group's;
- Cmd+C and Cmd+V after a header click, pasting a copy right before the copied track in its group, and Paste appearing in the menu once tracks are copied;
- that Delete is left out where it would empty a group;
- that the colour grid colours two selected tracks in one undo step and marks the colour in use;
- that Rename edits the clicked track's name in its header, and Select Content selects its clip's time on its track;
- that a track's info text shows in the Info view at its header and Session title, and an undo restores the usual description (the groups suite saves and reopens info texts);
- right-click selection.

The groups suite checks the return limit.

## Not determined

- English labels.
- Whether right-clicking an unselected header changes the selection.
- Where Insert Audio/MIDI/Return Track insert in the reference; Halftrack inserts after the selected track, as its Create menu does.
- The reference keeps one clipboard for clips and tracks; Halftrack's track clipboard is its own. (Its Paste position was decoded later: right before the first selected track, in its group; with Main selected, returns after the last return and other tracks at the end of the other tracks.)
- Delete Unused Take Lanes' notion of unused: a take lane none of whose clips share an id with a main-lane clip (0x1024b6770, 0x102481d98), the id itself not decoded.
- Select Content for groups, and the meaning of the Session flag that drops Select Content and Assign colour.
