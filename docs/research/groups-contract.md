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

Not implemented: group sends, moving tracks between groups, presets and racks on group or Main chains, sidechains into group or Main plug-ins, per-clip launch modes through group slots, and any comparison with reference group summing.
