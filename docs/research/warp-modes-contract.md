# Warp modes and transposition contract

Halftrack's warp modes follow the reference's: Beats, Tones, Texture, Re-Pitch, Complex and Complex Pro, offered in that order in the clip view. Halftrack keeps its cheaper Complex as a seventh choice. An audio clip warped in any mode except Re-Pitch can also be transposed while keeping its speed.

## Behaviour

- **Beats.**
  - Transients found in the audio start segments. Each segment plays from its transient at the clip's own speed, starting where the transient falls at the project tempo.
  - **Slowed down:** a segment that runs out before the next begins falls silent (Loop Off), loops its second half (Loop Forward), or loops it back and forth (Loop Back-and-Forth).
  - **Sped up:** a segment is cut short with a 2 ms fade.
  - **Transient Envelope** (0–100, 100 for none) fades each segment out over a share of its length.
  - Transients are found as the audio plays, half a second ahead, so a slowed segment plays in full while its end stays within that.
- **Tones** stretches with Signalsmith Stretch, its analysis block set by Grain Size, 1–100 at 4 ms a step (default 30, 120 ms).
- **Texture** stretches with Hann grains at half overlap, Grain Size 1–100 at 4 ms a step (default 65). **Flux** (0–100) moves each grain by up to half its length at random. The seed is fixed, so renders repeat exactly.
- **Complex** is Signalsmith Stretch's default preset, as before.
- **Complex Pro** is Complex that keeps **Formants** (0–100%, default 100) of the spectral envelope in place when the clip is transposed.
- **Settings.** Each mode's settings are part of `WarpSettings`. They are saved with the clip and refused out of range in calls and in project files. A mode ignores the settings of other modes.
- **Transpose and Detune** (−48 to 48 semitones, −50 to 50 cents) are part of `AudioClipSettings`.
  - They are allowed in the stretching modes only.
  - Switching to Re-Pitch or turning Warp off clears them in the same undo step.
- **The clip view.**
  - The Sample box shows the chosen mode's settings below the tempo.
  - Below them are Transpose, Detune and Gain knobs.
  - Transpose and Detune are disabled for Re-Pitch and unwarped clips.

## Engine changes

`cmake/PatchWarpModes.cmake` adds four Halftrack modes to the engine's time stretcher. Their stretchers come from a factory Halftrack registers (`src/WarpStretchers.*`). Mode settings travel in the stretch options as a version 2 string, written only when set, so existing projects save unchanged. See [dependencies](../dependencies.md#tracktion-warp-modes-patch).

## Evidence

`tests/WarpModeTests.cpp` renders fixtures recorded at 120 BPM:

- **Settings** read back for each mode; out-of-range values are refused, and other modes' settings ignored.
- **Beats**, with decaying 1 kHz hits on every beat:
  - at 60 BPM the hits start within 3 ms of each second (measured exactly on the second), each keeping its level and shape;
  - Loop Off leaves silence before the next hit, and Loop Forward fills it;
  - a Transient Envelope of 20 fades segments out early, identically on every render;
  - at 240 BPM the hits start every quarter second;
  - a live Beats clip plays through the synthetic device.
- **Tones and Texture** play a 1 kHz sine for 8 s at 60 BPM with no sidebands. Texture with Flux repeats exactly and keeps the pitch in 40 ms windows.
- **Transposition:**
  - Complex transposed up an octave sounds 2 kHz, still 8 s long, and +50 cents sounds 1029 Hz;
  - 49 semitones is refused;
  - Re-Pitch clears the transposition and refuses another.
- **Complex Pro**, with a harmonic sound whose formant is at 1 kHz, transposed an octave:
  - Complex moves the strongest harmonic to 2 kHz;
  - Complex Pro keeps it at 1.2 kHz;
  - Complex Pro with no formants kept moves it like Complex.
- **Saving:** modes, settings and transposition reopen, and a grain size of 500 in a saved file is refused while keeping the open project.

The UI suite chooses each mode, checks its settings appear and edit the clip, transposes with the knob, sees Re-Pitch clear and disable it, and undoes every step.

## Not yet covered

- Beats' Preserve grid divisions (1 Bar to 1/32) and transients from the whole file are missing.
- Complex Pro's Envelope setting is missing.
- Grain Size scaling, mode defaults and the reference's sound in each mode are unmeasured.
- Transposing unwarped clips is refused rather than following the reference.
- The default warp mode stays Complex.
