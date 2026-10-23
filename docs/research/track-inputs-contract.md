# Track inputs contract

A track's input section follows the reference's mixer In/Out section (manual, [Routing and I/O](https://www.ableton.com/en/live-manual/12/routing-and-i-o/), consulted 2026-10-23). The reference has two choosers for an input: Audio From or MIDI From picks the source, and a second picks its channel (or a source track's tap). Monitoring is three switches, In, Auto and Off ([mixer sections](mixer-sections-contract.md)).

## MIDI From channel

`Session::setTrackInputChannel (trackIndex, channel)` sets a MIDI track's MIDI From channel. 0 is All Channels, the default; 1 to 16 lets only that channel in. `TrackInfo::inputChannel` reports it.

- **Effect.** With a channel set, only that channel's messages reach the track: they are monitored and played through its instrument, recorded in the Arrangement or a Session slot, captured by Capture MIDI, and added by Session Record's overdubs. Other channels' messages are left out. Messages without a channel, such as clock, pass.
- **Changes.** A change is one undo step, and undo and redo apply too. It is refused on audio tracks and returns, for channels outside 0 to 16, and while recording. The new channel applies once the playback graph is rebuilt, which the change starts.
- **Saving.** The channel is saved as the track's `halftrackMidiInputChannel`; All Channels leaves it out. On load, a channel outside 1 to 16, or one on a track that is not a MIDI track, is refused and the open project is kept.

## Engine

`cmake/PatchMidiInputChannel.cmake` adds `MidiInputDevice::getInputChannelFor (edit, target)` (see [dependencies](../dependencies.md#tracktion-midi-input-channel-patch)). It reads the channel from the track a target names, or from a clip slot's track. A track's MIDI input node reads it when the graph is built and drops other channels' messages on the MIDI thread. Each MIDI recording context reads it when the recording is prepared and records only that channel. Capture MIDI applies the same filter in Halftrack's own code.

## Evidence

`tests/MidiInputTests.cpp` checks, through a generated controller and the synthetic device:

- that a monitored MIDI track takes All Channels at first and plays channel 3;
- that with channel 5 set, channel 3 is silent and channel 5 plays;
- one undo back to All Channels, and redo;
- refusals outside 1 to 16, on an audio track and while recording;
- that a recording keeps only channel 5's note;
- that the channel reopens, and that a project holding channel 17 is refused.

## Not yet covered

- The interface's MIDI From channel chooser.
- The reference's handling of MPE input with a channel set.
