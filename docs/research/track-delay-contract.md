# Track Delay contract

Track Delay follows the reference mixer's per-track delay ([Mixing: Track Delays](https://www.ableton.com/en/live-manual/12/mixing/), consulted 2026-10-20). A track's output can play up to a second later or earlier than its clips. This is Halftrack's own implementation; its range and placement are simplest defaults, not measurements of the reference.

## Interface

- `setTrackDelay(trackId, milliseconds)` accepts −1000 to +1000 ms on any audio, MIDI or return track. It is one undo step.
- Frozen tracks, non-finite values, out-of-range values and unknown tracks are rejected, as are changes while recording or exporting.
- `TrackInfo::delayMs` reports the delay.
- Each Session mixer strip shows the delay in milliseconds above its crossfader buttons. Dragging or typing edits it, and double-click resets it to 0 ms.

## Processing

- The first non-zero delay inserts a Halftrack Track Delay device after the track's devices and before its sends and mixer, so sends carry the delayed signal. It then stays on the track at any value.
- A positive delay holds audio in a ring buffer, preallocated for one second when the device is prepared, and the audio callback only reads and writes it.
- A negative delay reports that many milliseconds of latency and passes audio unchanged. The engine's delay compensation then holds every other path back by the same amount, so the track plays early while the rest of the project stays on the timeline.
- Changing between negative values rebuilds the playback graph, and positive changes apply from the next block.

## Projects

The delay is stored on the device (`halftrackDelayMs`). Loading rejects more than one delay per track, a delay outside a track, or a value outside ±1000 ms, and keeps the open project.

## Evidence

`tests/TrackDelayTests.cpp` renders two tracks, each with a single impulse, and finds every impulse to the sample. It checks:

- ±10 ms and ±1000 ms shifts of the delayed track while the other track stays in place;
- each impulse plays exactly once;
- undo and redo;
- zero delay;
- a pre-fader send carrying ±25 ms with the fader down;
- reopening;
- rejection of an out-of-range file;
- the frozen-track lock.

The UI suite edits a strip's delay field.

## Not implemented

- Delays in samples or beats.
- Delays on the master or group tracks.
- Clearing the buffer on seek, so for up to the delay time after a jump a positively delayed track can play audio from before the jump.
- Any comparison with reference output.
