# Track output routing contract

Halftrack's track output ("Audio To") follows the reference's [routing and I/O chapter](https://www.ableton.com/en/live-manual/12/routing-and-i-o/), consulted 2026-10-21.

## Behaviour

- **Choices.** `Session::TrackOutput` is Master (the default), Sends Only, or another track by its stable identity. `setTrackOutput (trackId, output)` sets it as one undo step. `TrackInfo::output` reports it, and it is saved with the project.
- **Destinations.** `outputDestinations (trackId)` lists the tracks a track may play into: other audio tracks that are not returns, not MIDI tracks, and do not already play back into it. Choosing any other track is refused, so routes cannot form a loop. MIDI tracks route their instrument's audio like audio tracks.
- **Through the destination.** A routed track plays through the destination track's devices and mixer, whatever that track's monitoring. Solo follows the route: a soloed source is still heard through its destination.
- **Sends Only.** The track is processed as before, and its sends still feed their returns, but its own output is not heard. Live playback and renders agree.
- **Fixed outputs.** Return tracks play into Master, and tracks in a group play into the group. Both refuse other outputs.
- **Loading.** A source track may both send to returns and play into another track. A return routed to a track is rejected before the current project is replaced. The engine drops an output that would feed back.
- **The UI.** Each Session mixer strip has an Audio To chooser below Monitor: Master, Sends Only and the tracks it may play into. Returns show Master and grouped tracks show Group, both disabled. Every strip leaves room for the row, so the faders line up.

## Engine fix

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

## Not yet covered

The lower routing chooser (a destination's input against its devices' sidechain inputs), Ext. Out, return and group tracks as destinations, and Audio From taps of other tracks (Pre FX, Post FX, Post Mixer) are not implemented. See [open questions](../open-questions.md).
