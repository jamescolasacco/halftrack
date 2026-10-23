# Group tracks contract

Group tracks follow the reference workflow of summing member tracks through a group bus with its own mixer ([mixing: group tracks](https://www.ableton.com/en/live-manual/12/mixing/), consulted 2026-10-20). They are engine submix folder tracks whose volume/pan plugin is replaced by Halftrack's constant-power `halftrack-mixer` (unity centre), plus a level meter.

`groupTracks(trackIds, groupId)` groups top-level audio or MIDI tracks (returns and already-grouped tracks are rejected; duplicates and empty requests fail) into a new group inserted at the first member, keeping members in their existing visual order. Snapshot track order and indices are unchanged: groups are reported separately in `Snapshot::groups` (`GroupInfo` with name, colour, gain, pan, mute, solo, fold state, meter and ordered member IDs) and each member's `TrackInfo::groupId`, so existing index-based track commands keep addressing the same tracks. `ungroupTracks` moves members back to the group's position and removes it. `renameGroup`, `setGroupGain`, `setGroupPan`, `setGroupMute` and `setGroupSolo` are undoable; `setGroupFolded` is saved view state, not a musical undo step.

Evidence (`tests/GroupTests.cpp`, 70 checks): a unity group renders the ungrouped member sum; group gain scales the sum by the dB factor; hard-left group pan gives √2 on the left and silence on the right; group mute silences members without changing their own mute; group solo leaves only members audible; grouped tracks still respond to index-based commands; name, fold and level reopen; ungroup restores order and the original render; undo/redo; returns and nested grouping are rejected; an instrument renders identically through its group.

Session view: a group column precedes its members, with the group colour and name and a disclosure triangle that folds the member columns away. A group slot shows a launch triangle when any member has a clip in that scene and lights while any of them plays. Clicking it calls `launchGroupSlot(groupId, scene)`, which launches each member's clip in that row on the global grid; members with an empty slot there stop, as in a scene launch. The group stop button calls `stopGroup`. Tracks outside the group are unaffected. The Session mixer has a group strip with a vertical volume fader, pan knob, group activator, solo and meter. `tests/LaunchModeTests.cpp` checks with generated levels that both members start together on the beat, that the second row switches one member and stops the other, and that stopping the group leaves an outside clip playing. UI checks cover column order, the strip, slot launch, stop and fold.

Correction: group gain and pan were not undoable. The parameter write alone is not recorded, and the manual-level properties that track faders keep were missing. Group commands now record them, and the post-undo refresh includes group mixers; `groups-acceptance` checks undo and redo of both.

## Device chains (2026-10-23)

As each of the reference's tracks has one, a group and the Main have device chains. Their devices play before the mixer: a group's on its members' sum, and the Main's on the whole mix before its fader.

- **Contents.** The chains take audio effects only: native audio effects, the Utility and effect plug-ins. Instruments and MIDI effects are refused, and the menus leave them out.
- **Session API.** Devices are addressed by chain: a track's id, a group's id or `Session::mainChain`. `addNativeEffect`, `addUtility`, `addPlugin`, `movePlugin`, `setPluginEnabled`, `removePlugin` and `showPluginEditor` all take a chain id, and on a track's chain they act as the track-index commands do. `GroupInfo::plugins` and `Snapshot::mainPlugins` list the devices.
- **Device commands by id.** Commands that address a device by its own id, such as settings, automation and MIDI mapping, find it in any chain.
- **Saving.** The devices are saved with the project (a group's in its plug-in list, the Main's in the engine's master plug-ins), with undo for each edit.
- **Interface.** Clicking a group's strip or header shows its chain in the device view, and clicking the Main's Session strip shows the Main's. Selecting a track shows its own chain again. + Device and the browser add to the chain shown.

Evidence (`tests/GroupTests.cpp`): a group's Utility at −6 dB plays its members 6 dB down. The Main's adds another 6 dB before its fader, and a switched-off device passes the sound. Instruments, MIDI effects and unknown chains are refused. A Channel EQ moves before the Utility, and one undo puts it back. The chains reopen and play as before, and removal works. The UI suite clicks a group strip and the Main's strip, adds a device to each, returns to a track's chain and undoes both.

## Nested groups (2026-10-23)

From a static reading of the reference's code (Group Tracks 0x10260d478, its grouping 0x102448fc4, Ungroup 0x102441b7c), groups nest.

- **Grouping.** `groupTracks` takes tracks and groups. The new group goes where the first of them is: inside its group, if it is in one. Grouping a grouped track therefore makes a group inside its group. Tracks and groups inside a chosen group move with it. Return tracks are still refused.
- **Ungrouping.** It moves a group's tracks and groups into the group around it, or to the top level.
- **Members.** Launching a group's slot or stopping the group acts on every track inside it, at any depth. The group's fader, activator and solo act on its whole contents.
- **Snapshot.** `GroupInfo::parentId` names the group around a group. A track's `groupId` names its own group.
- **Interface.**
  - The Arrangement shows a header lane for each group above its contents, outermost first. Folding a group hides everything inside it, nested groups' headers included.
  - The Session view shows each group's column before its contents.
  - A group lane summarises the clips of every track inside it.
  - With a group selected (by clicking its header or strip), Group Tracks groups it with the tracks selected besides it, and Ungroup Tracks ungroups it.

Evidence (`tests/GroupTests.cpp`):
- a grouped track grouped into an inner group, with the same mix and one undo;
- three levels built by grouping a group with a track, playing the same mix, with the top group's fader moving everything inside;
- the nesting reopening;
- ungrouping the middle group moving its contents into the top group, with the mix unchanged, and undo.

The UI suite checks the Session columns and Arrangement header lanes of a nested group, folding the outer group, and ungrouping the selected inner group.

Not implemented: group sends, presets and racks on group or Main chains, sidechains into group or Main plug-ins, per-clip launch modes through group slots, and any comparison with reference group summing.

## Moving tracks (2026-10-23)

This follows a static reading of the reference's code: its track move (0x10242f8d4), its drop target (0x1032c50ac) and its placement check (0x1024319a8). The grouping rule comes from Group Tracks' enable check (0x10242a09c).

- **Moving.** `moveTracks(ids, groupId, beforeId)` moves tracks and groups into a group, or to the top level when `groupId` is empty.
  - They go before `beforeId`, a track or group in that place, or at its end. At the top level the end is before the returns.
  - Groups move with their contents.
  - Dropping tracks where they already are changes nothing and makes no undo step. Otherwise the move is one undo step.
- **Refusals.** These are refused:
  - return tracks, which stay among the returns;
  - a group moving into itself or into a group inside it;
  - a move that would leave a group empty;
  - unknown tracks or groups;
  - a place outside the chosen group.
- **Empty groups.** Grouping is refused when it would empty a group other than the one the new group goes into, as the reference's enable rule has it. For moves, the reference has nothing that deletes or refuses an emptied group: neither its move nor the group's transaction handler (0x10219e5e4) does (medium), so it seems to keep the empty group. Halftrack refuses such moves until its views can show an empty group (see [open questions](../open-questions.md)).
- **Dragging headers.** In the Arrangement, dragging a track's header by its name or background moves the track, or every selected track if it is selected. A group's header moves the group. They land where the reference's drop target puts them:
  - over the upper half of a track or group, before it, in its group;
  - over the lower half of a group's header, folded or not, into the group, first;
  - over the lower half of a track, after it, in its group;
  - below every lane, after the last track.

  A track's automation and take lanes count as the track. Return lanes are no place for tracks, and a dragged group has no place inside itself; neither shows a line. Elsewhere an orange line across the headers shows where the tracks would go.
- **Correction.** Pressing the first 18 pixels of a track's name toggled its fold, because the header took the name's coordinates for its own. Only the header's fold corner folds now.

Evidence (`tests/GroupTests.cpp`):
- a track moved into a group before another plays the same mix;
- a track moved out lands at the top level after the groups, and one undo puts it back;
- dropping a track where it is changes nothing and makes no undo step;
- a group cannot move into itself or into a group inside it;
- unknown ids, a place outside the group, and grouping or moving that would empty a group are refused.

The UI suite drags headers:
- Pressing a track's name starts a drag without folding the track.
- Over the lower half of a nested group's header the drop goes into that group first, and the line is drawn there.
- Dropping moves the track; dropping below every lane moves it to the end, and two undo steps put it back.
- A group's header shows no drop over its own lower half, and dropping it after a track moves it with its contents.

Not reproduced:
- **Choosing the nesting level.** At a group's end the reference picks the level from the pointer's horizontal position, one level per 5 pixels from the header's left edge (0x103834258; pixel layout medium). Halftrack always takes the innermost group, which is the reference's choice when no level is picked.
- **After a move.** Afterwards the reference reveals the place, unfolding folded groups (0x10242a5fc, medium).
- **Audio To.** Moving a track into or out of a group switches its Audio To between Main and Group in the reference, leaving other destinations (0x1024bdbf8). Halftrack's grouped tracks always play into their group.
- **Other gestures.** Copying by drag, reordering returns by drag, dragging Session column headers and scrolling during a drag.
