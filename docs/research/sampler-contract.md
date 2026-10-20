# Sampler instrument contract

The Sampler follows the reference workflow of a single-sample instrument: it plays one audio file chromatically from a root key, with start/end region, looping, an amplitude envelope, a filter and a one-shot mode ([Simpler](https://www.ableton.com/en/live-manual/12/live-instrument-reference/#simpler), consulted 2026-10-20). It is Halftrack's own implementation. Its controls, defaults and sound are not measured against the reference instrument.

## Interface

`EffectKind::sampler` is native instrument kind 28 (`effectKindCount` is now 29), and `setTrackInstrument` chooses it like the other instruments. `setInstrumentSample(trackId, file)` loads an audio file. On a MIDI track that does not have a Sampler, the call creates one at the instrument position in the same single undo step. On an existing Sampler it changes only the sample and keeps the instance and its settings.

- Missing files, unreadable files, files longer than ten minutes and audio tracks are rejected without changing state.
- `PluginInfo::sample` reports the loaded file.
- In the interface, the instrument panel shows the sample's waveform with draggable Start/End markers (one gesture is one undo step), a loop marker, a Load button and a drop target. Choosing a sample in the browser while a MIDI track is selected loads it into that track's Sampler.

Controls (21):

- Mode: Classic / One-Shot.
- Pitch: root note, transpose (±48 st), detune (±50 ct).
- Region and loop: start, end, loop on/off, loop start.
- One-Shot trigger: Trigger / Gate.
- Filter: type, cutoff, resonance.
- Amp ADSR.
- Voices: Mono, 4, 8, 16 or 32.
- Velocity to amp, bend range, pan, volume.

## Playback

- The read rate is `2^((key + transpose + detune/100 + bend − root)/12) × sourceRate/outputRate`.
- Samples use four-point Hermite interpolation, so whole-frame positions return the source sample exactly.
- The played region is `[floor(min(start,end)·N), round(max(start,end)·N))`. Without a loop the voice ends at the region end.
- In Classic mode with Loop on, the voice wraps from the region end to the loop start (a fraction of the region) for as long as the envelope runs.
- One-Shot plays at full sustain. Trigger ignores note-off; Gate releases on note-off.
- The level is `envelope × ((1 − velocityAmp) + velocityAmp × velocity) × dB(volume)`. Unlike the synthesizers' quarter-scale headroom, 0 dB plays the source at unity.
- Pan is a balance law with unity at the centre: left gain is `min(1, 1 − pan)`, right is `min(1, 1 + pan)`.

## Real-time hand-off

The plugin decodes files on the message thread into an immutable `SampleData` (stereo float arrays and source rate). It publishes the data through a lock-free atomic pointer. At each block start the audio thread loads the pointer, hands it to the DSP and stores it as acknowledged. The message thread frees older samples only after the acknowledged pointer equals the published one. The callback never allocates, locks or decodes. A changed sample silences voices that played the old one.

## Projects

- Saved projects store the sample path relative to the project. Save copies the sample into the project's Assets folder, sharing copies with collected clip audio, and points the live instrument at the copy without decoding again or adding history.
- Loading resolves the path against the project directory. A missing or unreadable sample, or a sample reference on any non-Sampler device, rejects the load and keeps the open project.
- Duplicated tracks keep the sample.

## Evidence

`tests/SamplerTests.cpp` checks the following.

DSP:

- exact root-note and octave playback;
- transpose, root note and source-rate conversion;
- region and looping;
- Hermite accuracy against transposed and detuned closed-form sines (below 2e-5);
- Classic release and One-Shot Trigger/Gate;
- velocity, volume and pan;
- polyphony and mono restart;
- block-size independence;
- sample changes and empty samples.

Engine:

- an exact render through the engine;
- the single-step undo of the switch to Sampler with its sample;
- sample replacement and undo;
- 24 kHz playback;
- collection into Assets with relative storage;
- relocation of the copied project folder;
- identical reopened renders, duplication, and rejection of missing samples.

Allocation audits cover parameter extremes and sample switching inside the measured region. UI checks cover the panel, drop, marker drag, undo and a snapshot.

## Not implemented

- Slicing mode and warped (tempo-following) playback.
- Crossfaded loops and reverse.
- Filter and pitch envelopes, LFOs and glide.
- Multisampling and zones (the reference Sampler), and drum racks.
- Sample-accurate start offset modulation.
- Any comparison with reference output.

## Drum Sampler

`EffectKind::drumSampler` (kind 29) is a 16-pad kit modelled on the reference Drum Rack's pad workflow ([Drum Racks](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/#drum-racks), consulted 2026-10-20). Pads play notes 36 (C1) to 51 and appear in a 4x4 grid with pad 1 at the bottom left. Each pad holds one sample (`halftrackPad1` to `halftrackPad16`, collected and resolved like the Sampler's) and has five controls: tune (±48 st), length (fraction of the sample played), level, pan and choke group (none or 1–4); a kit volume follows. The parameter packet grew from 64 to 128 values to hold the 81 controls.

- A pad plays its sample from the start at `level × velocity × dB(volume)`, one-shot (note-off is ignored) and with the same balance pan and Hermite reads as the Sampler.
- A length below 1 fades out over 5 ms so that it ends at the cut.
- Striking a pad in a choke group releases the group's other pads over 5 ms.
- Notes outside the pads and empty pads are silent. Changing a pad's sample silences only that pad's voices, and the kit shares 32 voices.

`setDrumPadSample(trackId, pad, file)` loads a pad, switching a non-Drum Sampler instrument to a Drum Sampler in the same undo step; an empty file clears a pad. `PluginInfo::padSamples` lists the 16 pad files. The instrument panel replaces the generic controls with the pad grid: dropping a file on a pad loads it, clicking selects it, and the selected pad's Tune, Length, Level, Pan, Choke and Clear controls edit only that pad. Browser samples chosen for a Drum Sampler track load into the selected pad. The audio thread receives pad samples through the same per-slot acknowledged hand-off as the Sampler.

Evidence (`tests/SamplerTests.cpp`):

- DSP: exact pad playback and note mapping, note-off immunity, velocity, tune, level and pan, the length cut, choke groups against independent groups, block independence and per-pad sample changes.
- Engine: an exact kick-and-snare render on beats one and two, pad clearing and undo, single-step instrument creation, collection of every pad into Assets, relocation with identical renders, and rejection of a missing pad sample.

Allocation audits include pad sample switching, and UI checks cover the grid, drop, pad controls, Clear and undo.

Not implemented: chains with arbitrary devices per pad, pad sends and returns, more than 16 pads, pad auditioning by click, velocity curves, and reference comparison.
