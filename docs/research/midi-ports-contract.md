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

## Not yet covered

- MIDI Clock input and tempo following.
- Sync delay.
- MTC.
- Ableton Link.
- MIDI To another track's instrument, and per-port Remote switches.
- The External Instrument device (a stock device, deferred).
- Physical hardware timing measurements.
