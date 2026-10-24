# Track output routing contract

Halftrack's track output ("Audio To") follows the reference's [routing and I/O chapter](https://www.ableton.com/en/live-manual/12/routing-and-i-o/), consulted 2026-10-21.

## Behaviour

- **Choices.** `Session::TrackOutput` is Master (the default), Sends Only, or another track by its stable identity. `setTrackOutput (trackId, output)` sets it as one undo step. `TrackInfo::output` reports it, and it is saved with the project.
- **Destinations.** `outputDestinations (trackId)` lists the tracks a track may play into: other audio tracks that are not returns, not MIDI tracks, and do not already play back into it. Choosing any other track is refused, so routes cannot form a loop. MIDI tracks route their instrument's audio like audio tracks.
- **Through the destination.** A routed track plays through the destination track's devices and mixer, whatever that track's monitoring. Solo follows the route: a soloed source is still heard through its destination.
- **Sends Only.** The track is processed as before, and its sends still feed their returns, but its own output is not heard. Live playback and renders agree. A Sends Only track freezes and stays Sends Only while frozen; see the [freeze contract](freeze-contract.md).
- **Groups, returns and grouped tracks** (2026-10-24, after a static reading of the reference's routing choosers; findings in the ignored `artifacts/reverse-engineering/routing/`).
  - A track or group inside a group plays into it (`OutputKind::group`), as the reference's Audio To names the group. It may leave the group for Main, Sends Only, another track or an external output. Grouped MIDI tracks without an instrument may play to a MIDI port.
  - A group has an Audio To of its own (`GroupInfo::output`, set with `setTrackOutput` and the group's id).
  - A return may play into Main, Sends Only, an audio track or an external output; the reference never lists returns as destinations.
  - As the reference's grouping does (0x1024bdbf8), a track moved into a group from Main plays into it, and one leaving every group goes back to Main from its group. Other outputs are kept: a track routed elsewhere keeps its route when it is grouped. Ungrouping gives a track playing to Main inside the group around it that group, as the reference's Ungroup gives every track its new parent. New tracks inserted in a group play into it.
  - A track or group leaving its group is marked in the project (`halftrackOutsideGroup`); the engine patch below then leaves it out of the group's sum.
- **Loops.** A route that would lead back to the track is refused, followed through outputs, groups, sends, track inputs and sidechains, so a return cannot play into a track that sends to it. The reference's lists do not filter loops, as its engine has feedback units; Halftrack's engine would drop such a route, so `outputDestinations` leaves those tracks out. Loading refuses a project whose routes loop, before the current project is replaced.
- **The UI.** Each Session mixer strip has an Audio To chooser below Monitor, in the reference's order: the Ext. Out outputs and Configure..., which opens Audio & MIDI Setup; Main and the group the track is in, by its name; the tracks it may play into; and Sends Only. A group's strip has one in the same place. Every strip leaves room for the row, so the faders line up.

## Engine fixes

`cmake/PatchGroupOutputs.cmake` lets a track or group leave its group: one marked as leaving it is in no submix, so its folder leaves it out of its sum and it plays where its output goes. A parent group's mute and solo still reach it (see [open questions](../open-questions.md)). See [dependencies](../dependencies.md).

The pinned engine processed a track whose output goes nowhere as a silent bus in live playback, but its render graph summed every track without a destination track into the mix, so Sends Only tracks were heard in exports. `cmake/PatchRenderSendsOnly.cmake` makes renders process such a track as playback does. See [dependencies](../dependencies.md).

## Evidence

`tests/RoutingTests.cpp` renders a constant 0.5 signal offline and checks:

- A plays into Master at its own level, then through B's −6 dB mixer once routed into B;
- B cannot then play back into A, and the route is one undo and redo step;
- soloed, A is still heard through B;
- Sends Only with no sends is silent, and with a 0 dB send A is heard through the return;
- returns and MIDI tracks are refused as described above;
- the route reopens, and A is then heard through B and through its send together.

The UI suite chooses Sends Only and then a track from a strip, and undoes back to Master.

For groups and returns, `tests/RoutingTests.cpp` checks:
- a return playing into a track that does not send to it, heard through that track's mixer, its route reopening, and Sends Only;
- a return refused as a destination for a track that sends to it;
- a grouped track at −6 dB group gain playing into the group, then to Main past the group's fader, Sends Only, into another track and back into the group, each one undo step;
- a group playing into a track, heard through both mixers, with that track refused a route back into the group's member, and the group Sends Only;
- a grouped track playing to Main reopening that way;
- grouping and ungrouping keeping Sends Only, and switching Main and the group.

The UI suite checks the chooser's order, a grouped track's chooser showing its group by name, choosing Main and the group, and a group strip's chooser. `sends-acceptance` refuses a project whose return plays back into its source.

## Not yet covered

The lower routing chooser (a destination's input against its devices' sidechain inputs), group tracks as destinations through their devices' inputs, and the reference's two-chooser layout are not implemented. Ext. Out and Audio From taps are in the [output routing](output-routing-contract.md) and [Audio From](audio-from-contract.md) contracts. See [open questions](../open-questions.md).
