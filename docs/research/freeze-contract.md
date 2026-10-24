# Freeze, Flatten and Bounce Track in Place contract

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
- `bounceTracksInPlace(ids)` is the reference's Bounce Track in Place, after a static reading of it (0x10325bbc8 -> 0x1025a8d30, track list 0x10028b258). `canBounceTracksInPlace(ids)` says whether it would act.
  - It acts on the selected audio and MIDI tracks, top-most; a selected group and returns contribute nothing, as in the reference.
  - A track without a freeze is frozen first, outside history. Its render, or an existing freeze's, is written to the project's Assets.
  - The render replaces the track's Arrangement clips, cut at their edges and named after them ("… Bounce"). Where the sound rings on after a clip, a tail clip follows it ("… Bounce Tail"), up to the next clip or the end of the render.
  - Every device goes, with its automation, and so do the take lanes. A MIDI track becomes an audio track, keeping a name its instrument or a clip gave it. Mixer, sends, name and colour stay.
  - It is one undo step. Undo restores the clips and devices, unfrozen; redo needs no render.
  - Refused for now: tracks with Session clips (the reference renders each Session clip too; Halftrack does not freeze Session clips yet) and MIDI tracks without an instrument (the reference's sErrorNoAudioBounce).
  - The clip names and the tail threshold (−80 dB, with 10 ms after the last sound) are Halftrack's; the reference's suffixes are in its encrypted text, and its tail rule was not decoded.
- In the interface, the track header menu offers Freeze Track/Unfreeze Track and Bounce Track in Place (Tracks for more than one), as the reference's has them; its 12.2.1 has no Flatten, so the menu no longer offers it. Frozen clips show an icy hatch, and the device chain is dimmed and disabled.

## Corrections

- **Tails.** A freeze renders on past the last clip for as long as the devices say their tails may ring. The engine stopped at the first silent block, so a Delay's echoes were cut at the gap after the dry sound and frozen tracks lost them. `cmake/PatchFreezeTails.cmake` holds silent blocks back and stops only after three seconds of silence; a tail that sounds again within them is written whole. Other renders stop as before. The echoes then end where they fall below the engine's −70 dB silence.

None of the following needs an engine patch.

- **MIDI tracks.** The engine's freeze checks that a track's graph produces audio. Native instruments and the built-in synth did not report that they sound without audio input, so freezing a MIDI track rendered nothing. Both now override `producesAudioWhenNoAudioInput`.
- **Sends Only tracks.** The engine renders a freeze through its render graph, which leaves a Sends Only track's own output out of the mix (see the [routing contract](routing-contract.md#engine-fix)). The render was silent, so the engine unfroze the track and freezing failed with "The track could not be frozen." While a freeze renders (freezing, and redoing a freeze), Session now plays the track into Master outside history, then returns it to Sends Only.
- **Silent start.** The playback graph can ask the engine's file cache for the freeze file before the render writes it. The cache then records a failed open and retries only 4–7 seconds later, so a newly frozen track was silent for its first seconds of playback. In the synthetic-device test the first play after freezing stayed silent for 2.5–2.9 s of wall-clock time, at any block rate. After each freeze render Session asks the engine's file manager to validate the file, which reopens it at once.

## Persistence

The frozen flag is saved with the project, but the render is an engine temporary file. When the render is missing on reopening, the engine unfreezes the track, which then plays live from its devices. Flattened audio is ordinary collected project media.

## Evidence

The freeze suite bounces a Sampler MIDI track in place: it becomes an audio track whose clip renders like the live track, and one undo restores the MIDI track and its Sampler, unfrozen. It bounces an audio track whose Delay echoes a 0.2 s burst: the Delay goes, the render is cut at the clip's end with the echoes in a 2.3 s tail clip, the track plays as it did live but for echoes under −70 dB, undo restores it, and redo bounces it again without rendering. Returns are not bounced. The UI suite checks the menu item.


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

- Freezing and bouncing Session clips.
- Instrument releases after the last clip: instruments report no tail, so a freeze ends at the last clip and cuts a release there.
- Undoing a bounce of a track frozen before it, which comes back unfrozen, as after a Flatten.
- Portable freeze renders.
- A partial freeze point (freezing only some devices).
- Freeze progress while playing.
- Undoing an Unfreeze. The undo restores the engine's freeze point but leaves the track unfrozen, for any track. Freeze it again instead.
- The Post Mixer tap of a frozen track. A track taking a frozen track's audio Post Mixer (the default tap) hears nothing, whatever the frozen track's output, because the engine's frozen-track graph does not send to that tap. Pre FX and Post FX taps carry it. Fixing it needs an engine patch to the render graph.
- Any comparison with reference freeze behaviour.
