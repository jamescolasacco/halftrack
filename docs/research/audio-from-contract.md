# Audio From another track contract

Halftrack's track-sourced input follows the reference's [routing and I/O chapter](https://www.ableton.com/en/live-manual/12/routing-and-i-o/), consulted 2026-10-21. There, any track with a compatible output can be chosen as an input, at Pre FX, Post FX or Post Mixer.

## Behaviour

- **Choosing.** `inputSources (trackId)` lists the tracks an audio track may take audio from. These are the audio and instrument tracks other than returns and itself, minus those it already feeds through track outputs or track inputs. `setTrackInputTrack (trackId, sourceId, tap)` assigns one as one undo step, replacing any hardware input; choosing the same source again changes only the tap, also as one undo step; `setTrackInput (index, {})` or a hardware input replaces it. `TrackInfo::inputTrackId` reports the source, and `inputName` is then empty. MIDI tracks and returns refuse track inputs. As with hardware inputs, monitoring is reset to Off when the input changes.
- **The taps.** `InputTap::preFX` takes the source's audio before its devices; on an instrument track that is after the instrument. `postFX` takes it after the devices and before the mixer. `postMixer`, the default, takes it after the mixer, including the source's volume, pan and muting. `TrackInfo::inputTap` reports the tap. The source's own output setting does not affect any tap, so a source played Sends Only still feeds the track.
- **Solo.** As in the reference, soloing the receiving track, which mutes the source, still lets it hear a Pre FX or Post FX tap, but not Post Mixer.
- **Hearing and recording.** The routed audio passes through the destination's devices and mixer when the destination monitors In, or Auto while armed. Recording the destination records it, so a track's output can be resampled into another track.
- **Loops.** A route that would feed back is refused, in either direction: a track cannot take audio from a track it feeds, and track outputs (Audio To) refuse a destination that takes audio from the source.
- **Exports.** A render includes a routed input, at its tap, while it is monitored as above. Without a playback context, it reads the routes from the edit's saved input state. See [dependencies](../dependencies.md).
- **Saving.** The engine saves the route as an input device of the source track; it reopens with monitoring Off, as all inputs do.
- **The UI.** Each track's input chooser lists hardware inputs, then a Tracks section with a submenu for each source holding its three taps, shown as "Audio 2 | Post FX".

## Evidence

`tests/AudioFromTests.cpp` plays a constant 0.5 signal on track A, set to Sends Only, through the synthetic device and checks:

- A is unheard with nothing routed;
- B taking A's audio is silent while unmonitored, plays 0.5 when monitored In, and follows B's −6 dB mixer and then A's;
- with a −6 dB Utility on A before its −6 dB mixer, Post Mixer hears both, Post FX only the Utility (in an export too) and Pre FX neither; a tap change is one undo step;
- soloing B still hears the Pre FX tap but silences Post Mixer;
- loops refused both ways, instrument tracks offered as sources but refusing audio input, and returns excluded;
- an armed recording on B holding A's 0.5 signal;
- an export holding the routed audio;
- the route reopening, and replacing it with No input then undoing.

The UI suite chooses the other track's Post FX tap from the input chooser and undoes it.

## Not yet covered

Resampling of the Master output, rack chain and device outputs as sources, MIDI from another track, several destinations per source, and reference timing comparison.
