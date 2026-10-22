# Freeze and Flatten contract

Freeze and Flatten follow the reference workflow of rendering a track's devices to audio to save processing, locking the track while frozen, and optionally replacing the track with that audio ([Freezing tracks](https://www.ableton.com/en/live-manual/12/computer-audio-resources-and-strategies/#freeze-track), consulted 2026-10-20). The descriptions are public workflow documentation, not measured identity with the reference application.

## Interface

- `setTrackFrozen(trackId, frozen)` freezes or unfreezes an audio or MIDI track. It is one undo step each way; undo of a freeze removes it and redo renders again.
  - Returns and tracks without Arrangement clips are rejected.
  - Freezing renders the track's clips through its instrument and devices, up to the engine's freeze point before the mixer. The track then plays that render.
  - The fader, pan, mute, solo and sends stay live.
  - A Sends Only track freezes like any other: the render is its own audio, whatever its output. Frozen, it still plays Sends Only: its own output is not heard, but its sends and tracks taking its audio Pre FX or Post FX play the freeze.
  - Playback of a new freeze starts at once.
- While a track is frozen, edits are rejected with an "Unfreeze … first" message: devices (add, move, enable, remove, settings and automation), its instrument and samples, synth settings, clips (add, import, paste, move, remove, range, gain, reverse, warp) and notes.
- `TrackInfo::frozen` reports the state.
- `flattenTrack(trackId)` needs a frozen track. It writes the render to a 32-bit float WAV in the project's Assets and replaces the track's clips, instrument and devices with one audio clip from time zero. The mixer, meter and sends stay, and a MIDI track becomes an audio track without its MIDI input.
  - MIDI tracks with Session clips are rejected.
  - The engine cannot render a freeze while an undo is in progress, so Flatten unfreezes outside history. Undoing a Flatten restores the unfrozen track with its devices, instrument and clips, which can then be frozen again.
- In the interface, the track header menu offers Freeze Track/Unfreeze Track and Flatten. Frozen clips show an icy hatch, and the device chain is dimmed and disabled.

## Corrections

None of these needs an engine patch.

- **MIDI tracks.** The engine's freeze checks that a track's graph produces audio. Native instruments and the built-in synth did not report that they sound without audio input, so freezing a MIDI track rendered nothing. Both now override `producesAudioWhenNoAudioInput`.
- **Sends Only tracks.** The engine renders a freeze through its render graph, which leaves a Sends Only track's own output out of the mix (see the [routing contract](routing-contract.md#engine-fix)). The render was silent, so the engine unfroze the track and freezing failed with "The track could not be frozen." While a freeze renders (freezing, and redoing a freeze), Session now plays the track into Master outside history, then returns it to Sends Only.
- **Silent start.** The playback graph can ask the engine's file cache for the freeze file before the render writes it. The cache then records a failed open and retries only 4–7 seconds later, so a newly frozen track was silent for its first seconds of playback. In the synthetic-device test the first play after freezing stayed silent for 2.5–2.9 s of wall-clock time, at any block rate. After each freeze render Session asks the engine's file manager to validate the file, which reopens it at once.

## Persistence

The frozen flag is saved with the project, but the render is an engine temporary file. When the render is missing on reopening, the engine unfreezes the track, which then plays live from its devices. Flattened audio is ordinary collected project media.

## Evidence

`tests/FreezeTests.cpp` uses a Sampler playing a generated stereo source, rendered at the test engine's 44.1 kHz freeze rate so no comparison includes resampling. Exports render a frozen track from its clips and devices, not from its freeze, so the export checks cover the frozen track's mixer; the freeze itself is checked through Flatten, which copies it. It checks:

- the rejections;
- an export of the frozen track identical to the live render;
- locked devices, instrument, samples, clips and notes;
- a live fader scaling the frozen track's export;
- undo and redo of freezing;
- a flattened audio clip that renders like the live track, on the same track and without devices;
- undo of Flatten back to the MIDI track with its Sampler and note, and refreezing.

A second part plays a constant 0.5 signal on an audio track through the synthetic test device, which plays the freeze itself. The track plays Sends Only with a 0 dB send to a return. It checks:

- the track freezes and still reports Sends Only;
- the frozen track is heard through its send within 200 blocks of starting playback, and at 0.5 after. Each block dispatches messages for at least a millisecond, so this allows a fraction of a second against the seconds of silence before the correction. It is usually the first block, but under load the cache can miss it;
- with the send off, the frozen track is silent;
- undoing back to unfrozen keeps Sends Only, and redo freezes it again, heard at once by the same measure;
- Flatten keeps Sends Only, its audio is the track's own 0.5 signal, and it is heard through the send;
- frozen again with the send off, a track taking its audio Post FX hears it at 0.5.

A UI check freezes from the track menu handler (devices locked), flattens and undoes back.

## Not implemented

- Freezing Session clips.
- Portable freeze renders.
- A partial freeze point (freezing only some devices).
- Freeze progress while playing.
- Undoing an Unfreeze. The undo restores the engine's freeze point but leaves the track unfrozen, for any track. Freeze it again instead.
- The Post Mixer tap of a frozen track. A track taking a frozen track's audio Post Mixer (the default tap) hears nothing, whatever the frozen track's output, because the engine's frozen-track graph does not send to that tap. Pre FX and Post FX taps carry it. Fixing it needs an engine patch to the render graph.
- Any comparison with reference freeze behaviour.
