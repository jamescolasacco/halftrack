# MIDI output ports, MIDI To and MIDI Clock

Status: partial (REC-012 and MID-013).

The reference behaves as follows:

- A MIDI track without an instrument plays its MIDI to an external port. Its MIDI To choosers pick the port and a channel from 1 to 16.
- Its MIDI Ports preferences list each output with Track and Sync switches. Track lets tracks play to the port; Sync sends MIDI Clock and Song Position to it.
- Both switches start off.

Halftrack follows that model with its own UI.

## Behaviour

**Ports.** `midiOutputPorts()` lists the engine's MIDI outputs with their two switches. `setMidiOutputPort (name, track, sync)` sets both. A port opens while either switch is on.

The switches are kept with the engine's device settings, not the project:

- Track in Halftrack's `midiout_halftrackTrack <name>` item;
- Sync as the engine's own `sendMidiClock`.

A newly found output starts closed, with both switches off.

**Empty MIDI tracks.** `removeTrackInstrument (trackId)` removes a MIDI track's instrument, whether built-in, native or hosted, as one undo step. `TrackInfo::hasInstrument` reports whether a MIDI track has one. Giving the track an instrument again, from the chooser or as a hosted instrument, returns its output to Master in the same undo step, as dropping an instrument on a MIDI To track does in the reference.

**MIDI To.** `setTrackOutput (id, { OutputKind::midiPort, {}, port, channel })` plays a MIDI track to a port, as one undo step. It is refused:

- for a track with an instrument;
- for a port whose Track switch is off;
- for an unknown port;
- for a channel outside 1 to 16.

`TrackInfo::output` reports the port by name and the channel. The track's MIDI leaves on that channel, notes and other channel messages alike. Muting the track, MIDI effects before the port and notes held across a stop are not yet measured.

The project saves the port by name, which the engine resolves on any machine. It saves the channel as `halftrackMidiOutChannel`. A project with a channel outside 1 to 16, or on a track that is not a MIDI track, is refused without replacing the open project.

**Sync.** While Sync is on and the transport plays, the port receives:

- 24 MIDI Clocks to the beat at the current tempo;
- Song Position (in sixteenths) and Start when playback begins at the start;
- Song Position and Continue elsewhere;
- Stop when the transport stops.

Each rebuild of the playback graph, after a settings change for example, restarts the clock, which names its position again at the next beat. The generator is the engine's own.

**The UI.**

- The instrument chooser ends with No Instrument. The device view then says the track has none and points to MIDI To.
- A strip's output chooser on such a track becomes MIDI To: No Output, then a submenu of channels for each port whose Track switch is on (and for its current port). It shows, for example, "IAC Bus 1 | Ch. 5".
- Audio & MIDI Setup has a MIDI Ports menu with Track and Sync for each output. It no longer shows the device selector's MIDI output chooser, which set an output the engine never used.

## Engine

`cmake/PatchMidiPorts.cmake` changes four things in the engine (see [dependencies](../dependencies.md)):

- An engine-behaviour hook can leave physical MIDI ports out, and the device scan honours it. A Session built with a port prefix sees only the ports whose names start with it, so tests never touch the machine's own devices.
- Newly found outputs open only as the host chooses.
- A track routed to a MIDI device with `halftrackMidiOutChannel` set gets a node at its end. The node rewrites each channel message's channel without allocating.
- The engine already adds device nodes for MIDI outputs that send clock, so a Sync port gets clock with no track routed to it.

## Evidence

`tests/MidiPortTests.cpp` makes a virtual MIDI port and builds a Session that sees only it. It plays through the synthetic audio device in real time and checks:

- the port listed with both switches off, and an unknown port refused;
- MIDI To refused for a track with an instrument, for a port with Track off, and for channels 0 and 17;
- instrument removal with undo, and MIDI To with undo and redo;
- three notes arriving on channel 3, in order, each released, and the first at velocity 102;
- no clock with Sync off, and a Session clip launching with the port open;
- with Sync: Song Position 0 and Start before the first clock, exactly 96 clocks in four beats at 120 BPM, and Stop;
- Song Position 32 and Continue from beat 8;
- 72 clocks in three beats at 90 BPM;
- no clock after Sync is switched off;
- an instrument returning the output to Master, and undo;
- saving and reopening, with the notes played again on channel 3;
- a tampered channel refused.

The UI suite makes its own port, chooses No Instrument and MIDI To channel 5 from the strip, undoes it, switches Track on and off through MIDI Ports, and restores the synth.

On systems that cannot make virtual MIDI ports (Windows, or Linux without the ALSA sequencer), the suite and the UI port checks report SKIP.

## External Sync (2026-10-22)

**Following a clock.** An input port's Sync switch (Audio & MIDI Setup > MIDI Ports, kept with the engine's settings) lets it send MIDI Clock. Halftrack opens such ports itself, beside the engine's own input, so their notes still play into tracks. Their clock and transport messages reach the message thread through a fixed ring. Once an input's Sync is on, the control bar shows EXT before TAP. With External Sync on:

- **Tempo.** It comes from a least-squares line through the last beat's 24 clock ticks, so one late tick barely moves it.
- **Phase.** While playing, the tempo is raised while the transport is behind the clock's position and lowered while it is ahead, by the distance in beats beyond 0.02 (the fixed latency of reading either), 10% at most.
- **Transport messages.** Start plays from the beginning; Continue plays on from where the transport, or a Song Position, put it; Stop stops; Song Position moves the play head while stopped and keeps it on that beat as the tempo follows.
- **Undo and editing.** The tempo it sets is not an undo step. The tempo field and TAP step aside meanwhile, and setting the tempo otherwise is ignored.
- **One tempo.** External Sync needs a single tempo, not a tempo map.

**Evidence.** `tests/ExternalSyncTests.cpp` makes a virtual MIDI port and sends it clock in real time, pumping the synthetic device in step. The test's own ticks jitter by a few milliseconds, so tempos are checked to half a BPM. It checks:

- the port is offered as an input;
- with External Sync off, the clock's 120 BPM is read but not followed;
- with it on, the tempo follows (about 120.1);
- Start then two seconds gives four beats (within 0.15);
- three seconds at 90 BPM bring the tempo to about 89.7 and the transport to the clock's 8.5 beats (within 0.2);
- Stop, Song Position to beat 8 (exact), and Continue on to beat 9.5;
- none of it is an undo step;
- off again, a 140 BPM clock leaves the tempo, and the tempo can be set;
- with the port's Sync off its clock is no longer read, and a missing port is refused.

Systems that cannot make virtual ports skip it. The UI suite switches an input's Sync on in MIDI Ports, sees EXT appear before TAP, and checks that EXT turns External Sync on with the tempo field and TAP disabled. Turning both off hides EXT.

## Not yet covered

- Sync delay, and clock loss while playing (the transport plays on at the last tempo).
- MTC.
- Ableton Link.
- MIDI To another track's instrument, and per-port Remote switches.
- The External Instrument device (a stock device, deferred).
- Physical hardware timing measurements.
