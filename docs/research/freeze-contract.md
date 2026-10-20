# Freeze and Flatten contract

Freeze and Flatten follow the reference workflow of rendering a track's devices to audio to save processing, locking the track while frozen, and optionally replacing the track with that audio ([Freezing tracks](https://www.ableton.com/en/live-manual/12/computer-audio-resources-and-strategies/#freeze-track), consulted 2026-10-20). The descriptions are public workflow documentation, not measured identity with the reference application.

## Interface

- `setTrackFrozen(trackId, frozen)` freezes or unfreezes an audio or MIDI track. It is one undo step each way; undo of a freeze removes it and redo renders again.
  - Returns and tracks without Arrangement clips are rejected.
  - Freezing renders the track's clips through its instrument and devices, up to the engine's freeze point before the mixer. The track then plays that render.
  - The fader, pan, mute, solo and sends stay live.
- While a track is frozen, edits are rejected with an "Unfreeze … first" message: devices (add, move, enable, remove, settings and automation), its instrument and samples, synth settings, clips (add, import, paste, move, remove, range, gain, reverse, warp) and notes.
- `TrackInfo::frozen` reports the state.
- `flattenTrack(trackId)` needs a frozen track. It writes the render to a 32-bit float WAV in the project's Assets and replaces the track's clips, instrument and devices with one audio clip from time zero. The mixer, meter and sends stay, and a MIDI track becomes an audio track without its MIDI input.
  - MIDI tracks with Session clips are rejected.
  - The engine cannot render a freeze while an undo is in progress, so Flatten unfreezes outside history. Undoing a Flatten restores the unfrozen track with its devices, instrument and clips, which can then be frozen again.
- In the interface, the track header menu offers Freeze Track/Unfreeze Track and Flatten. Frozen clips show an icy hatch, and the device chain is dimmed and disabled.

## Correction

The engine's freeze checks that a track's graph produces audio. Native instruments and the built-in synth did not report that they sound without audio input, so freezing a MIDI track rendered nothing. Both now override `producesAudioWhenNoAudioInput`, so no engine patch is needed.

## Persistence

The frozen flag is saved with the project, but the render is an engine temporary file. When the render is missing on reopening, the engine unfreezes the track, which then plays live from its devices. Flattened audio is ordinary collected project media.

## Evidence

`tests/FreezeTests.cpp` uses a Sampler playing a generated stereo source, rendered at the test engine's 44.1 kHz freeze rate so no comparison includes resampling. It checks:

- the rejections;
- a frozen render identical to the live render;
- locked devices, instrument, samples, clips and notes;
- a live fader scaling the frozen render;
- undo and redo of freezing;
- a flattened audio clip that renders like the live track, on the same track and without devices;
- undo of Flatten back to the MIDI track with its Sampler and note, and refreezing.

A UI check freezes from the track menu handler (devices locked), flattens and undoes back.

## Not implemented

- Freezing Session clips.
- Portable freeze renders.
- A partial freeze point (freezing only some devices).
- Freeze progress while playing.
- Any comparison with reference freeze behaviour.
