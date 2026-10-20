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
