# Hosted instruments and plug-in timing contract

## Hosted instruments

As in the reference, an instrument plug-in dropped on a MIDI track becomes its instrument. It replaces the built-in synth or a native instrument, as one undo step, and keeps the MIDI effects ahead of it and the audio devices after it.

- **Marking.** It is marked in its saved state, so a project reopens with it as the instrument even while the plug-in is missing.
- **Replacing it.** Choosing a native instrument replaces it in turn.
- **Audio tracks.** Instrument plug-ins are refused on audio tracks. From the browser, an instrument chosen while an audio track is selected gets a new MIDI track.
- **The device chain.** It shows the plug-in as a device, and hides the built-in synth's panel, which no longer plays.
- **Load validation.** On load, a track may hold one plug-in instrument, on a MIDI track; otherwise the project is refused.

## What a hosted plug-in is told

`tests/TestProbePlugin.cpp` builds the Halftrack Test Probe, a VST3 instrument that writes what its host tells it into its audio:

- **Left channel:** each note-on's velocity, at the exact sample the host delivered it.
- **Right channel:** the host's beat position as a sawtooth that restarts on each beat. It is negative while the host renders offline and absent while stopped.

Restarts and note samples can be found exactly whatever gain follows the plug-in.

`tests/PluginTimingTests.cpp` puts it on a MIDI track with notes at beats 0.5, 1.25, 3 and 7.75, and checks:

| Case | Result |
| --- | --- |
| Offline render at 120 BPM | Notes at samples 12000, 30000, 72000 and 186000, each exact; all 15 reported beats on their samples; offline reported |
| Offline render at 90 BPM | Notes at 16000, 40000, 96000 and 248000, exact; beats on their samples; offline reported |
| Live, 256-sample blocks | Notes 18000 and 42000 samples apart, the second 6000 after its beat; beats every 24000 samples; live reported |
| Live, 128-sample blocks | The same |

The hosting checks cover:

- the probe is refused on an audio track and becomes the MIDI track's instrument, with undo and redo;
- an effect follows it;
- the Poly Synth replaces it, and undo restores it.

## Not yet covered

- MPE and controller events to hosted plug-ins.
- Looping and tempo ramps as the plug-in sees them.
- Sample-rate changes.
- Plug-ins that report latency while receiving MIDI.
- AU instruments.
