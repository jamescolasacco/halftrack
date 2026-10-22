# Hosted instruments and plug-in timing contract

## Hosted instruments

As in the reference, an instrument plug-in dropped on a MIDI track becomes its instrument. It replaces the built-in synth or a native instrument, as one undo step, and keeps the MIDI effects ahead of it and the audio devices after it.

- **Marking.** It is marked in its saved state, so a project reopens with it as the instrument even while the plug-in is missing.
- **Replacing it.** Choosing a native instrument replaces it in turn.
- **Audio tracks.** Instrument plug-ins are refused on audio tracks. From the browser, an instrument chosen while an audio track is selected gets a new MIDI track.
- **The device chain.** It shows the plug-in as a device, and hides the built-in synth's panel, which no longer plays.
- **Load validation.** On load, a track may hold one plug-in instrument, on a MIDI track; otherwise the project is refused.

## Where an effect plug-in goes

As in the reference when no device is selected, a new device joins the end of the track's chain. An effect plug-in follows the same rule as a native effect or the Utility:

- **Audio effect:** after the devices already there, before the track delay, sends and mixer. On a MIDI track that is after the instrument and its audio effects; on a return, after the return input and its effects.
- **MIDI effect:** after the earlier MIDI effects, immediately ahead of the instrument.

`tests/SessionTests.cpp` adds the VST3 gain fixture after an EQ Three on an audio track, after an Arpeggiator and an EQ Three on a MIDI track, and after an EQ Three on a return, and checks that it comes last each time. `tests/TrackDuplicateTests.cpp` checks that it follows a Utility. Inserting after a selected device is not implemented yet; see [open questions](../open-questions.md#device-insertion).

## Classification

`Session::pluginRole` classifies a scanned plug-in from its description, not its file:

- **Instrument:** the description says so.
- **MIDI effect:** its category mentions MIDI, as Audio Units' MIDI processors do. A VST3 scan reports no channels and no MIDI role, so a VST3 MIDI effect lists as an audio effect.
- **Audio effect:** anything else.

`pluginSummary` adds the format, and the main channel counts when the scan knew them.

The browser lists plug-ins under their makers ("Unknown Maker" when none is given), by name. Each row shows its role, format and channels. A MIDI effect plug-in goes on a MIDI track ahead of its instrument and is refused on audio tracks; an audio effect plug-in joins the end of the chain. The UI suite checks the grouping, the summaries for an instrument, a MIDI effect, an audio effect with channels and one without, and the audio-track refusal, using stand-in descriptions.

## Architectures

Halftrack runs natively, as arm64 on Apple silicon and x86_64 on Intel Macs, Windows and Linux, and bridges no plug-ins between architectures.

`src/PluginBinary.*` reads which architectures a plug-in's binary holds, from its header. It handles thin and universal Mach-O, PE and ELF, and the binary inside a `.vst3` or `.component` bundle, including the per-architecture folders of Windows and Linux VST3 bundles. When a plug-in lacks this process's architecture, inserting it fails with an explanation before its binary is loaded. Loading one could stop on a system error dialog on Windows, which hung the first Windows run of the test. A placeholder for it in an opened project gives the same reason. The explanation names the plug-in, what it is built for, what Halftrack runs as, and says to install a matching version. For example: "“Foreign Probe” is built for Intel (x86_64) only, and Halftrack runs as Arm (arm64)…".

`tests/PluginBinaryTests.cpp` writes headers for each format and checks the architectures found, and that other files and missing ones name none. It then builds a bundle for the other architecture and checks its explanation, that a native bundle gets none, and that inserting the foreign one fails with the explanation.

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
