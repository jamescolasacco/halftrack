# Recording latency

Status: partial, AUD-009.

The reference places recordings by the audio device's reported latency. Its Driver Error Compensation corrects a driver that reports wrongly. Its audio preferences show the input, output and overall latency. Halftrack does the same; the behaviour comes from the reference's manual.

## Behaviour

**Placement.** A recording is moved earlier by:

- the device's reported input latency and output latency;
- the graph's plug-in latency;
- Driver Error Compensation.

So what was heard through an output is recorded where it played. JUCE's CoreAudio latencies include the device's buffer, the safety offset and the stream latency in each direction.

**Driver Error Compensation.** It is a preference, `Preferences::driverErrorCompensationMs`, from −200 to 200 ms; positive values move recordings earlier.

- **Storage.** It is kept in the preferences file. Older files without it read as 0.
- **When it applies.** It applies to every audio input when Arrangement or Session recording starts.

**Readout.** `Session::audioLatency()` reports the input and output latency in milliseconds, the compensation, and their overall sum. Audio & MIDI Setup shows them, with an editable compensation field. A value beyond ±200 ms is refused.

**Engine fix.** The engine placed every take one block early. The playback context's reference stream runs a block ahead of the device's stream time: before a callback it is resynced to [stream time, stream time + block], then advanced by a block. So output rendered in a callback plays from the edit time of stream time plus a block, while input in the same callback was stamped with the stream time itself. `cmake/PatchInputPlacement.cmake` stamps the input at stream time plus a block, the edit time of the output rendered with it. A take's first block, which arrives while the start is still queued, is placed at that start instead of being dropped. See [dependencies](../dependencies.md#tracktion-input-placement-patch).

## Evidence

`tests/RecordingLatencyTests.cpp` uses a loopback device whose input is its own output 700 samples later, as through a cable. It reports whatever latency the test sets. A click at one second plays from one track and is recorded on another, unmonitored. It checks:

- the reported latency reads back (14.58 ms overall at 48 kHz);
- **reported truthfully** (300 in, 400 out), the take's click lands where it played: 0.0 samples off (before the engine fix, a block of 256 samples early);
- **reported 200 samples short**, it lands 200.0 samples late;
- **compensation of 200 samples** (4.17 ms) puts it back to 0.0, and the overall latency includes it;
- compensation beyond 200 ms is refused.

The UI suite reads the readout on Preferences' Audio page, types a compensation and sees it in the overall latency, and checks that 500 ms is refused.

## Not yet covered

- A physical loopback measurement, which needs a cable from an output to an input (on the MOTU M4, Monitor Out to Line In 3).
- Monitoring delay, and per-input record adjustment.
- MIDI input placement against latency.
