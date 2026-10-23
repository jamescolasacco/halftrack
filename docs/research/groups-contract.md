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
  - The Create menu enables both commands by the reference's rules (0x10242a09c, 0x1024714a8). Group Tracks is enabled while `canGroupTracks` allows the selection: no returns, and no other group emptied. Ungroup Tracks is enabled only with a group selected, so a selected track no longer ungroups its group.

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
- **Returns.** Return tracks move only among the returns, before one or after the last when no place is given, as the reference's drop target keeps returns and other tracks apart (0x1032c50ac). Other tracks stay before the first return.
- **Refusals.** These are refused:
  - a return moving among the other tracks, a track among the returns, or both at once;
  - a group moving into itself or into a group inside it;
  - unknown tracks or groups;
  - a place outside the chosen group.
- **Empty groups.** Grouping is refused when it would empty a group other than the one the new group goes into, as the reference's enable rule has it. Moves and deletions may empty a group, which stays. The reference has nothing that deletes or refuses an emptied group: neither its move nor the group's transaction handler (0x10219e5e4) does (medium). An empty group keeps its header lane and Session column, and tracks can be dropped into it.
- **Dragging headers.** In the Arrangement, dragging a track's header by its name or background moves the track, or every selected track if it is selected. A group's header moves the group. They land where the reference's drop target puts them:
  - over the upper half of a track or group, before it, in its group;
  - over the lower half of a group's header, folded or not, into the group, first;
  - over the lower half of a track, after it, in its group;
  - below every lane, after the last track.

  A track's automation and take lanes count as the track. Return lanes are no place for tracks, and a dragged group has no place inside itself; neither shows a line. A return's header drags among the return lanes only, before a return or after the last. Elsewhere an orange line across the headers shows where the tracks would go.

  Pressing the name of a track selected with others keeps them all selected, so the drag moves them all; releasing without a drag selects that track alone. Shift-clicking adds or removes a track, as before.
- **Placement.** Both views place a drop with one rule, the reference's drop target (0x1032c50ac) and the groups it offers at a place (0x1024314c8). "After" an item means the next place in the track order.
  - The groups that place can be in run from the deepest one the items on either side share to the innermost one the item before is in. An unfolded group's header counts as inside itself, so the next place after it is its first.
  - With no nesting level picked, the innermost is used.
  - A picked group gives the group one level outward of it, or the outermost if the picked group is not offered there.
  - A folded group's header puts the tracks inside it, first.
- **Nesting strips.** Arrangement headers start after a 5-pixel strip for each group around them, in the groups' colours, outermost first, with their automation and take lanes. Over the lower half of a header, the pointer over a strip picks that group's level, as the reference's arranger picks one level for every 5 pixels from the header's left edge (0x103834258; pixel layout medium). So below a group's last member, moving onto the group's strip takes the track out of the group. The line showing the drop starts after the strips of the groups the place is in.
- **Correction.** A group's header moved onto its last automation lane when its envelope opened in a lane below it. It stays on its own lane now.
- **Session columns.** As the reference's Session header row places them (0x103a51318, read in this pass; the hit test is 0x10178fe0c):
  - The title of a track's or group's column drags it, sideways. Over a column's left half the drop goes before it, over its right half after it. A group's title also selects the group.
  - The height in the title picks the nesting level (0x103a525a8): the groups around the column's track, outermost first, one for every 6 pixels from the title's top. Below those levels nothing is picked. So near the top of the last member's right half, a track leaves the group, and lower down it stays at the group's end.
  - Returns drag among the return columns. Over the Main column's left half they go after the last return; its right half and the other tracks' columns refuse them.
  - Past the Main column is after the last track (the reference's free space between the tracks and returns, which Halftrack's layout has no room for).
  - The drop target is the title row; below it nothing is offered. An orange line down the columns' edge shows where the drop would go.
- **Correction.** Pressing the first 18 pixels of a track's name toggled its fold, because the header took the name's coordinates for its own. Only the header's fold corner folds now.
- **Order.** `GroupInfo::position` says where each group's header is: before `Snapshot::tracks[position]`. Both views, the stem export and the export window's Rendered Track list use it to place groups, empty ones too, in display order. Before, a group holding only a group had no stem, and nested groups' stems were out of order.

Evidence (`tests/GroupTests.cpp`):
- a group's only track moved out leaves it empty, with its header's position after the track before it, and the track can move back in;
- a track moved into a group before another plays the same mix;
- a track moved out lands at the top level after the groups, and one undo puts it back;
- dropping a track where it is changes nothing and makes no undo step;
- a group cannot move into itself or into a group inside it;
- unknown ids, a place outside the group, and grouping or moving that would empty a group are refused;
- returns reordered among themselves, with one undo, and refused among the other tracks.

The UI suite drags headers:
- Pressing a track's name starts a drag without folding the track.
- Over the lower half of a nested group's header the drop goes into that group first, and the line is drawn there.
- Dropping moves the track; dropping below every lane moves it to the end, and two undo steps put it back.
- A group's header shows no drop over its own lower half, and dropping it after a track moves it with its contents.
- A return's header shows no drop over an ordinary track, and dropping it on the first return's upper half puts it first among the returns.
- Two tracks selected with Shift drag together below every lane and come back with one undo; pressing one of them without a drag then selects it alone.
- A doubly nested track's header starts after two strips, in the outer and inner groups' colours. Below the first track, right of its strips, a drop goes to the inner group's end, and over its outer strip after the inner group. Below the second track a drop stays at the outer group's end, the line starting after that group's strip, and over the strip it goes to the top level's end. Dropping there takes the track out, and one undo puts it back. A group's header stays on its own lane when an automation lane opens below it.
- In the Session view, the second track's title shows the drop before the first track (with the line), at the inner group's end, after the inner group when the outer group is picked, at the outer group's end, and at the top level's end from the top 6 pixels. Dropping there takes the track out of the groups.
- The inner group's title selects it, shows no drop over its own right half, and drops after the second track with its track.
- A return's title drops before the other return, but not over an ordinary track or the Main column's right half. Over the Main column's left half it would go after the last return.
- An emptied group keeps its header lane and Session column.

The stems suite exports a group holding only a group, before the group inside it.

Not reproduced:
- **The strips' look.** The reference keeps the deepest nesting for its arranger layout (LSong +0x800); how it draws the strips was not read. Halftrack's strips are its own.
- **After a move.** Afterwards the reference reveals the place, unfolding folded groups (0x10242a5fc, medium).
- **Audio To.** Moving a track into or out of a group switches its Audio To between Main and Group in the reference, leaving other destinations (0x1024bdbf8). Halftrack's grouped tracks always play into their group.
- **Other gestures.** Copying by drag, the reference's modifier rules for selecting several headers (not read; Halftrack's Shift adds or removes one track), and scrolling during a drag.

## Deleting (2026-10-23)

This follows a static reading of the reference's Delete (0x1032cdd64, 0x1024415e4) and its track deletion (0x102441cf0).

- **API.** `removeTracks(ids)` deletes tracks and groups in one undo step.
  - A group goes with everything inside it, nested groups included.
  - Sends to a deleted return go.
  - Plug-ins taking a sidechain from a deleted track keep the sidechain without a source.
  - Deleting a group's last track leaves the group, empty.
  - `removeTrack(index)` now deletes through it.
- **Interface.** After a click on an Arrangement header's name or background, or on a Session column's title, Delete deletes the selection: the selected group, or the selected tracks. Afterwards the track now where the first deleted one was is selected, as in the reference (medium). After a click in the lanes or on a Session slot, Delete acts there as before. The Create menu's Delete Selected Track does the same, a selected group included.
- **Ungroup.** Ungroup (see Nested groups) keeps the contents and discards only the group: its devices, mixer settings and automation, as in the reference.

Evidence (`tests/GroupTests.cpp`):
- deleting a group with its track, and one undo restoring both;
- deleting a three-level tree at once, with one undo restoring it and its mix;
- deleting two tracks together, leaving an inner group empty;
- unknown and empty deletions being refused.

The UI suite clicks a nested group's name and presses Delete. The group goes with its track, the next track is selected, and one undo brings both back. After a click in the lanes, the headers no longer have the focus. In the Session view, Delete after a click on a track's title deletes the track, one undo brings it back, and a click on a slot takes the focus from the titles.

## Group sends (2026-10-23)

The reference's group mixer is the ordinary track mixer, with a send per return (its Set schema; [group findings](#moving-tracks-2026-10-23)). Halftrack's groups now have sends too.

- **API.** `setGroupSendGain(groupId, returnId, dB)` sets a group's send from Off (−100 dB) to 0 dB along the send law, as `setTrackSendGain` does for a track. It is one undo step, and `GroupInfo::sends` lists one send per return.
- **Taps.** A group's send taps the group's sum before or after its fader, as the return's Pre/Post setting decides for every send. Changing that setting moves the groups' sends too.
- **Automation and mapping.** The send keys `send:<group>:<return>` work for groups. `setSendAutomation` takes a group's id, and the send is a MIDI and key mapping target that automation recording captures. The group's lane in Automation Mode shows and edits its sends' envelopes, as below.
- **Solo and mute.**
  - A soloed group now counts as a soloed source. Solo in Place keeps the returns open, fed only by the soloed group's sends and its tracks' sends, as for soloed tracks.
  - A track's send counts as soloed when a group around it is soloed. A group's send counts as soloed when the group, a group around it, or a track inside it is soloed.
  - A muted group's post-fader sends are silent.
- **Saving.** Group sends are saved with the group, and loading validates them as it validates track sends.
- **Deleting a return** removes the groups' sends to it as well.
- **Interface.** The Session mixer's group strips show a send knob per return, A, B, C, level with the tracks' knobs. The knobs are mapping targets and follow the Sends section switch.

Correction: soloing a group used to mute the returns, and left every track's sends open, because only soloed tracks counted.

Evidence (`tests/GroupTests.cpp`):
- a group's send at half level adding half its sum through a return, and one undo;
- refused unknown groups, non-return destinations and levels above 0 dB;
- a pre-fader send feeding the return with the group's fader down;
- a soloed group silencing an outside track while its return stays audible;
- a muted group's post-fader send being silent;
- the send being a mapping target, automatable, reopening and playing as before;
- the send going with a deleted return and coming back with one undo.

The UI suite turns a group strip's send knob, checks its mapping target and undoes it.

Not reproduced: selection linking for group controls. How group mute should act on its tracks' own sends is undetermined (see [open questions](../open-questions.md)).

## Group automation and mapping (2026-10-23)

The reference automates a group's mixer and devices as a track's: its group mixer is the ordinary track mixer ([group findings](#moving-tracks-2026-10-23)). Halftrack's groups now do too.

- **Session API.**
  - `setTrackAutomation` takes a group's id for its volume, pan and split pans.
  - `GroupInfo` reports `volumeAutomation`, `panAutomation`, `leftSplitAutomation` and `rightSplitAutomation`, as `TrackInfo` does.
  - The mixer keys `mixer:<group>:volume|pan|leftsplit|rightsplit` are MIDI and key mapping targets, and the group's faders and pans carry them.
- **Overrides.** Moving an automated group control overrides its lane, as on tracks, until Re-Enable Automation. Automation recording captures group moves through the same keys.
- **Interface.** In Automation Mode a group's header shows the device and control choosers in place of its fader and pan, and its lane draws the chosen envelope and edits it as a track's: the mixer's Track Volume and Track Panning, its sends named for their returns, and its devices' controls. The envelopes are drawn over the clip overview.
  - The choosers are now one `AutomationChoosers` shared by track and group headers.
  - The group header's + moves the envelope into a lane below the group's header, as a track's does. The lane draws and edits that envelope, and its header hides it.

Evidence (`tests/GroupTests.cpp`):
- a group's volume automated at −6 dB plays its track 6 dB down, and reopens playing the same;
- moving the group's fader overrides the lane, and the group plays at the fader's level until Re-Enable Automation;
- the group's pan lane;
- the group's volume, pan and split pans as mapping targets, and an unknown group's refused.

The automation-capture suite records a group's pan and send through the synthetic device. The UI suite checks a nested group's choosers in Automation Mode, a double-click adding a breakpoint to its volume, and undo. It also checks the + opening a lane under the group's header, a breakpoint added in it, and hiding it.
