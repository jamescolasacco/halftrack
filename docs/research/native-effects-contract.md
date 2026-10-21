# Native effects implementation contract

Halftrack now implements an eight-band equalizer, compressor, stereo delay, room reverb, bitcrusher, noise gate and peak limiter as original native devices. They insert on audio tracks, after MIDI instruments, or after return inputs; device order, bypass, removal, parameter gestures and saved state use the engine undo system. All parameters have named XML properties. Invalid state is rejected before replacing the active project.

The reference manual identifies independently enabled filter bands, dynamics controls, feedback delay with stereo routing, and algorithmic reverberation as relevant workflows. These implementations cover subsets of those workflows; the manual does not establish their numerical algorithms. [Official audio effects reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/), consulted 2026-10-14. Recovered binary output is not compiled into these effects.

## Equalizer

Eight serial stereo bands each offer Off, Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch and Band Pass. Each has a frequency (20–20,000 Hz), gain (-24–24 dB) and Q (0.1–18). Output gain and dry/wet follow the bands. Frequency is limited internally to 45% of the actual processing rate. All bands default to flat bells. The display evaluates the actual settled transfer function, including output gain and mix, at the selected device rate (48 kHz when no device exists).

The biquad transfer equations are standard normalized second-order sections following the mathematical forms in the [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/). Implementation and state management are original. Shelf Q controls transition resonance; this is not a promise of matching a proprietary shelf implementation. No dynamic spectrum, channel-specific/mid-side processing, oversampling or 48 dB cut modes are implemented. Band mode changes clear that band's history; coefficient changes are immediate and may produce transients during large edits.

## Compressor

The linked stereo detector takes the larger channel magnitude. Peak mode smooths magnitude; RMS mode smooths its square and takes the root. Attack/release coefficients use a one-pole time constant. Threshold, ratio and a quadratic soft knee determine static gain reduction. Makeup and parallel wet mix follow. There is no lookahead latency.

Ranges: threshold -60–0 dB, ratio 1–20, attack 0.01–1,000 ms, release 1–3,000 ms, knee 0–24 dB and makeup -24–24 dB. Defaults are -18 dB, 4:1, 10 ms, 100 ms, 6 dB knee, zero makeup and fully wet peak detection. External sidechain, sidechain filtering, auto release/makeup, lookahead, expanded metering and reference envelope/curve identity remain missing or unverified.

## Stereo delay

Independent left/right times span 1–2,000 ms with linear interpolation. Linking retains the inactive right-time value. Stereo feedback stays in its channel; ping pong injects the summed mono input into the left delay and feeds each delayed channel into the other. Feedback is bounded to 95%, with a one-pole high cut. Freeze rejects input and recirculates the existing delayed signal at unity, bypassing feedback damping. Dry/wet is linear. Defaults are 250/375 ms, 35% feedback, 30% wet and 12 kHz damping.

Project-tempo mode adds eleven divisions from a sixteenth note through a whole note, including dotted and triplet timings. Left/right divisions remain independent unless linked. Musical divisions are quarter-note beat ratios, not time-signature-dependent bar lengths. Millisecond values remain stored when sync is enabled. The message-thread timer observes project tempo changes at 30 Hz and publishes a complete FIFO packet; export refreshes immediately after draining playback. This does not implement sample-accurate tempo automation.

Time changes interpolate over 5 ms and intentionally change pitch. Modulation, alternate transition modes, input filtering and measured reference feedback/freeze behavior are not implemented. A prepared 12-second stereo buffer covers the longest supported division at 20 BPM; no tempo edit allocates audio-thread memory. The engine tail estimate is capped at 1,800 seconds, including Freeze; explicit export duration remains user-selected. Freeze is a buffer loop, not an infinite-decay guarantee under fractional interpolation or changing times.

## Room reverb

The original room uses eight delay lines with an orthonormal Householder feedback matrix. Per-line feedback is calculated from its duration and the requested -60 dB decay time, with one-pole high-frequency damping. Independent input/output weights create stereo reflections. Pre-delay, decay, size, damping, width and dry/wet are editable. Allocations occur only during preparation; fixed maximum delay capacity accommodates all parameters at the prepared rate.

Defaults: 15 ms pre-delay, 2 s decay, size 1, 6 kHz damping, width 1 and 25% wet. This is an original algorithm; no proprietary room topology or presets are claimed. Convolution, modulation, early-reflection editing, freeze, input EQ and reference room identity are absent. Room-size edits move delay endpoints immediately and can produce transients. Decay is the feedback design target, not a measured broadband T60 guarantee with damping engaged.

## Bitcrusher

A hard quantizer and sample-and-hold stage offer 1–24 bits, 1–64 samples per capture, 0–36 dB input drive, -24–24 dB wet output and linear dry/wet. Defaults are 8 bits and a four-sample interval, without gain changes. Integer depths use nearest signed PCM-style levels with saturation and ties away from zero; fractional depths continuously vary the step size. At one bit, signed endpoints are -1 and 0. Silence is exactly zero. Fractional hold intervals alternate adjacent sample counts while retaining phase across blocks; channels share capture timing but retain independent samples.

This intentionally aliases during rate reduction. There is no pre/post anti-alias filtering, jitter, DC-shift control, dither or legacy-mode emulation. The implementation is original and has not been measured against the reference processor. Its exact equations, update behavior and bounded tail are in the architecture contract. Hand-calculated signal tables verify the quantizer independently of its implementation.

## Noise gate

A linked peak detector controls a linear attack/hold/release envelope. Separate opening and closing thresholds provide hysteresis; reopening during release resumes from current gain. Floor attenuation, Gate/Duck and parallel mix are adjustable. Zero attack/hold/release times are supported, and the minimum floor setting mutes exactly. Parameters, frame-count rules and default values are documented in the architecture contract.

Hand-calculated sample sequences verify both stereo gains, cold-state and open-state hysteresis, exact hold frames, reopening, floor/mix arithmetic and duck inversion. Closed-form ramps are compared at 8/44.1/48/96 kHz. This original device does not yet include lookahead, external/filtered sidechains or reduction metering, and no reference envelope identity is claimed.

## Peak limiter

A linked stereo sample-peak detector uses a fixed one-millisecond lookahead rounded up to whole frames. A preallocated monotonic deque calculates the full delayed-frame/future-window maximum; gain immediately meets the ceiling and returns toward unity with a one-pole release. Input gain, peak ceiling and release are editable. This has intentional latency, reported to the native graph. Plugin bypass provides the unprocessed path; there is no parallel dry knob that would invalidate the ceiling.

An independent brute-force window oracle verifies complete samples at 8/44.1/48/96/384 kHz. Impulses verify exact delay and stereo independence, while graph tests cover compensated edges, clips shorter than lookahead, serial instances, parallel tracks, a delayed return and bypass. This is an original sample-peak algorithm, without oversampling/true-peak protection, automatic release, selectable lookahead, reduction metering or measured reference identity.

## Control and verification boundaries

Full 34-value packets travel through a fixed 64-entry SPSC queue (63 usable). The message thread retries its latest complete packet when saturated. Audio callbacks do not read model/UI state. Wet/output gain and delay time transitions take 5 ms; EQ coefficients and compressor coefficients update directly. DSP defensively replaces nonfinite samples and bounds internal magnitudes to 1e12. Headroom below this protection limit is preserved.

Signal tests independently check all eight bell center gains, each filter's DC/Nyquist/center limits, closed-form peak/RMS attacks and soft-knee transfer, exact delay impulse times at four sample rates, first cross-feedback response, first room reflection and recirculation, pre-delay shifts, width and decay behavior. Block splitting, reset and dry bypass are deterministic. Complete-file engine exports, parameter round trips, malformed-load preservation, nonlinear order changes and production editor handlers are also exercised. Runtime allocation/FIFO guards and the source callback audit have a separate scope.

These tests establish the stated implementation. They do not establish sample-identical reference processing, physical input delivery, arbitrary plugin safety, moving-parameter sound equivalence or whole-engine real-time safety.

## Extended device set (2026-10-20)

Ten further original devices share the same packet, validation, history, editor and persistence path. The reference manual describes the corresponding workflows (saturation curves, band-limited overdrive, envelope/LFO filter, tremolo/panning, modulated delays, three-band isolation, bus compression and frequency shifting); it does not specify their algorithms, so every formula below is a Halftrack default. Oracles live in `tests/ExtendedEffectsTests.cpp`; every kind also passes the shared block-partition, reset, non-finite input, zero-wet, project round-trip, malformed-load, engine-export equality, bypass, removal-undo and allocation-free extremes checks.

| Device | Behaviour | Independent evidence |
| --- | --- | --- |
| Saturator | Drive −36–36 dB into one of six memoryless curves (tanh analog clip, soft sine, algebraic medium curve, cubic hard curve, sine fold, digital clip), optional 10 Hz DC high-pass, output −36–36 dB, optional half-scale-knee soft clip, dry/wet. | 48 curve/drive/output combinations match closed forms within 2e-7; soft clip exact below its knee and bounded at full scale; DC removal; exact dry/wet average. |
| Overdrive | Constant-peak band-pass (50 Hz–20 kHz, 0.5–9 octaves), 0–36 dB drive into a biased tanh, envelope-ratio Dynamics restoration, one-pole Tone (400 Hz–20 kHz), DC blocker. | More drive raises harmonic distortion more than threefold; even harmonics present; centre passes at unity and a distant tone is rejected by >20 dB; Tone and Dynamics change measured content in the stated direction. |
| Auto Filter | Trapezoidal state-variable low/high/band/notch, 12 or 24 dB, resonance Q 0.5–20, LFO ±4 octaves (six shapes, Hz or nine tempo divisions, stereo phase), linked envelope follower ±4 octaves. | 32 mode/slope/cutoff/resonance cases match analytic bilinear magnitudes at five frequencies within 2e-4; tempo-division rates are exact; LFO, stereo phase and envelope direction measured. |
| Auto Pan | Per-channel gain `1 − amount·(1 − lfo)/2`, six shapes, Hz or tempo division, stereo phase and offset. | 30 shape/amount/phase cases match the closed form within 1e-6; held random gain per cycle; quarter-note sync at 60 BPM. |
| Chorus | One to three voices of a linearly interpolated 1–30 ms delay with up to 10 ms sinusoidal depth, stereo spread and ±95% feedback. | Zero-depth exact stereo delay, geometric feedback train, modulation bounded by depth, unmodulated voice averaging. |
| Phaser | 2–12 first-order all-pass stages around a 50 Hz–10 kHz centre, ±3 octave LFO, ±95% feedback and stereo spread. | Two stages cancel the centre at 50% mix for three centres; all-pass magnitude preserved for every stage count; LFO sweep measured. |
| Flanger | Single-voice 0.1–20 ms modulated delay with depth, rate, spread and feedback. | Same exact-delay, echo-train and bounded-modulation checks as Chorus. |
| Three-band EQ | Linkwitz–Riley 24 or 48 dB crossovers (50 Hz–5 kHz, 200 Hz–18 kHz) with an all-pass-aligned low band; band gains −70 (off) to +6 dB and band on/off. | Unity bands recombine to an all-pass within 2e-6 for both slopes; low/mid/high gains and band kill measured; inverted crossovers collapse safely. |
| Bus Compressor | Linked peak detector, 2/4/10:1, seven attack and six release choices plus automatic dual-detector release, makeup, maximum-reduction Range, soft clip, dry/wet. | 9 ratio/attack combinations match the closed-form attack recurrence within 1e-7; Range bound; soft clip bound; automatic release lies between its detectors. |
| Frequency Shifter | Single-sideband shift ±5 kHz via a two-path IIR quadrature network, optional opposite right-channel shift, or ring modulation. | Three shifts place the wanted sideband within 0.5% with the image and carrier below −40 dB; opposite stereo and ring sidebands measured. |

The quadrature network uses the published coefficients from Olli Niemitalo's IIR Hilbert transformer design; its image rejection is about 49 dB at 1 kHz. Known gaps: no oversampling or anti-aliasing on any waveshaper, no sidechain input for the compressors, no tempo-synced phaser/flanger rate, no Chorus-Ensemble "Ensemble"/"Vibrato" modes, no Auto Filter morph/vowel/DJ circuits, no Shifter pitch mode, and no measured numerical comparison with the reference devices.

## Channel EQ (2026-10-20)

Channel EQ follows the manual's description in the [audio effect reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/), consulted 2026-10-20. The manual gives:

- an HP 80 Hz switch;
- Low, a 100 Hz shelf of ±15 dB;
- Mid, a ±12 dB peak swept from 120 Hz to 7.5 kHz;
- High, a shelf of up to +15 dB. When cutting, a low-pass is added whose cutoff falls from 20 kHz to 8 kHz as the gain goes from 0 to −15 dB;
- Output.

It is the first device kind numbered after the racks.

Halftrack's own choices, where the manual is silent:

- the high-pass is a second-order Butterworth;
- the shelves use RBJ sections with Q 1/√2, and the High shelf's corner is 8 kHz;
- the Mid peak has Q 0.7;
- the low-pass is a second-order Butterworth whose cutoff moves evenly in octaves;
- Output runs from −12 to +12 dB.

Sections at 0 dB are exact identities, so the defaults pass audio unchanged.

`tests/ExtendedEffectsTests.cpp` checks closed-form properties rather than re-running the device's formulas:

- the 80 Hz high-pass is 3.01 dB down at its corner and flat at 1 kHz;
- each shelf is at exactly half its gain at its corner;
- the peak reaches exactly ±12 dB at 120 Hz, 1 kHz, 2.5 kHz and 7.5 kHz;
- the low-pass cutoff is exact at 0, −7.5 and −15 dB;
- at −15 dB the shelf's half cut and the low-pass's 3 dB add at 8 kHz;
- Output scales exactly;
- both channels are filtered alike;
- out-of-range values are rejected.

Channel EQ also passes the shared device checks. Its spectrum display and measured comparison with the reference are missing.

## Tuner (2026-10-20)

The Tuner follows the manual's description in the [audio effect reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/), consulted 2026-10-20. It detects a monophonic pitch and its distance from the nearest semitone, shown in cents or Hz. The reference pitch runs from 410 to 480 Hz, defaulting to 440 Hz, and green means in tune.

The device passes audio unchanged. Its audio callback writes the mono mix of what it processes into a preallocated ring of 16,384 atomic samples, without allocating or locking. `Session::tunerReading(pluginId)` copies the latest 4,096 frames on the message thread and runs `tuner::detect`. The detector, in `src/Tuner.*`, is YIN (de Cheveigné and Kawahara, 2002) with a 0.1 threshold:

- the dip is refined with a parabola;
- the period is then refined again at a multiple near 400 samples on the raw difference function;
- silence (below −80 dBFS RMS), aperiodic input and pitches outside 30 Hz–4.2 kHz read nothing.

The editor shows the note name, cents or Hz, and the Classic view's target mode. A circle on a ±50 cent scale is green within 3 cents and red beyond, sharp to the right.

`tests/TunerTests.cpp` (`tuner-acceptance`) checks:

- **Accuracy.** Sines from E1 to C8, and offsets from −49 to +49 cents, read within half a cent; measured error is under 0.03 cents.
- **Reference.** 432 Hz references read correctly.
- **Sample rates.** 44.1 and 96 kHz read alike.
- **Harmonics.** A saw reads as its fundamental.
- **Rejection.** Silence, white noise, a −100 dBFS tone, 20 Hz and 6 kHz read nothing.
- **In a project.** The device renders bit-identically to its absence, reads A4 after a render, and follows its reference. Its inline and window editors show the reading in cents and in Hz.
- **Live input.** The synthetic device's monitored stereo input, 440 and 660 Hz, mixes to a signal periodic at 220 Hz and reads as A3 while the transport is stopped. It reads nothing once monitoring stops.

Strobe mode, the Histogram view, note-spelling choices and measured comparison with the reference are missing.

## Spectrum (2026-10-21)

The Spectrum follows the manual's description in the [audio effect reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/), consulted 2026-10-20. It is an analyser that does not change the signal. Its controls follow the manual:

- **Block:** 1,024 to 16,384 samples per analysis.
- **Channel:** L, R or L+R.
- **Refresh:** 10–500 ms.
- **Avg:** 1–100 blocks averaged per display update.
- **Graph:** a line or bins.
- **Max:** an accumulated maximum, reset by clicking the display and when playback restarts.
- **Scale X:** Lin, Log or ST, where ST is logarithmic with note names.
- **Range/Auto:** Min and Max levels, or a 96 dB axis that follows the loudest bin.

The manual gives no numbers for these ranges, so Halftrack's are recorded as defaults.

The device shares the Tuner's analysis tap. The ring now keeps both channels and holds 32,768 frames, so a 16,384 block fits. `Session::spectrum(pluginId)` copies the newest block of the chosen channel and runs `spectrum::analyse` in `src/Spectrum.*`. The analysis is a periodic Hann window and JUCE's FFT, scaled so that a full-scale sine centred on a bin reads 0 dBFS. The editor averages the readings in power and draws the display; it also names the loudest bin, which the reference shows on hover.

`tests/SpectrumTests.cpp` (`spectrum-acceptance`) checks:

- **Exact levels.** Bin-centred tones read exactly: 0 dB at full scale, −20 dB at a tenth, and two resolved tones at −6.02 and −12.04 dB, at every block size.
- **Rejection.** Other block sizes read nothing, and silence reads the floor.
- **Pass-through.** The device renders bit-identically to its absence.
- **Channels.** A stereo file reads per channel at 6.02 dB above the L+R mean, with the other tone over 40 dB down.
- **Block sizes.** The tone falls on bin 30 at 1,024 and bin 480 at 16,384.
- **Editor.** The inline editor names the 586 Hz peak.

Moving the display between the device chain and the main window, the hover readout and measured comparison with the reference are missing.

## Pedal (2026-10-21)

Pedal follows the manual's description in the [audio effect reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/), consulted 2026-10-21. The manual gives:

- **Types and level:** Overdrive, Distortion and Fuzz, with Gain (0% still distorts) and Output.
- **EQ after the distortion,** whose resonance increases with boost:
  - Bass, a 100 Hz peak;
  - Mid, switched between 500 Hz, 1 kHz and 2 kHz and narrower at lower settings;
  - Treble, a shelf near 3.3 kHz.
- **Sub:** a shelf boosting below 250 Hz.
- **Dry/Wet.**

Halftrack's own choices:

- **Drive:** Gain drives the stage from +6 to +42 dB.
- **Curves:**
  - Overdrive is `tanh(x + 0.2) − tanh(0.2)`, a warm asymmetric soft clip;
  - Distortion is `2/π·atan(3x)`, tight and symmetric;
  - Fuzz is `tanh(4(x + 0.35x²))`, hard and strongly asymmetric.
- **Filtering:** a 10 Hz one-pole DC blocker follows the curves.
- **EQ sections:** RBJ sections whose Q is multiplied by 1 + 0.04 per dB of boost.
  - Bass has Q 0.7.
  - Mid has Q 1.4, 1.0 or 0.7 for its three frequencies.
  - Treble is a Q 1/√2 shelf.
- **Sub:** a fixed +6 dB shelf.
- **Ranges:** ±12 dB bands, and Output from −36 to +12 dB with a default of −12.
- **Keys:** Type and Gain use the keys `pedalType` and `pedalGain`, because the plugin state's own `type` property would collide.

`tests/ExtendedEffectsTests.cpp` checks:

- **EQ, through the near-linear small-signal path:** Bass and each Mid reach exactly ±12 dB at their centres, Treble is at half its boost at 3.3 kHz, and Sub adds 3 dB at 250 Hz and about 6 dB at 25 Hz. A bigger boost is more resonant, and the 500 Hz Mid is narrower than the 2 kHz one.
- **Harmonics, from a half-scale 1 kHz tone:** Distortion has no even harmonics, Overdrive adds them and Fuzz more, and every type distorts more with more Gain.
- **Mix and Output:** fully dry passes audio unchanged, and Output scales exactly.

The Hi-Quality (oversampling) mode and measured comparison with the reference are missing.

## Beat Repeat (2026-10-21)

Beat Repeat follows the manual's description in the [audio effect reference](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/), consulted 2026-10-21. The manual describes these controls:

- **Interval and Offset** (1/32 to 4 bars, in sixteenths) set when a slice is captured.
- **Chance** gives the probability of a repeat.
- **Gate** sets the total length of the repeats in sixteenths.
- **Repeat** holds a repeat for as long as it is on.
- **Grid** sets the slice size, with **No Triplets** and **Variation** (Trigger, 1/4, 1/8, 1/16 or Auto).
- **Pitch and Pitch Decay** lower the repeats by resampling.
- **A band filter** acts on the repeats.
- **Mix, Insert and Gate** decide how the repeats combine with the input.
- **Volume and Decay** set the repeats' level.

Halftrack's behaviour, where the manual gives no detail:

- **Timing.** Triggers follow the transport position given at each block (`setTimelinePosition`), so renders and playback place repeats on the same samples whatever the block size. A start, seek or loop jump schedules the next trigger afresh and ends a timed repeat. Beats are counted at the project's first tempo, with 4/4 bars.
- **Slices.** The first slice of a repeat is the input itself, captured as it passes. Later slices replay it, each `(1 − Decay)` quieter than the last and scaled by Volume.
- **Pitch.** Pitch reads the slice at `2^(semitones/12)` speed with linear interpolation, and Pitch Decay lowers each further repeat by up to 12 semitones.
- **Mix modes.** Mix adds the replays to the input; Insert plays the repeat in place of the input; Gate plays only the repeat. Stopped, Beat Repeat passes the input in Mix and Insert and silence in Gate.
- **Filter.** The filter is a 12 dB/oct high-pass and low-pass around its frequency, Width octaves apart, on the replays.
- **Variation.** Variation moves the grid up to Variation/2.5 steps. No Triplets rounds triplet sizes to binary ones.
- **Randomness.** The random choices (Chance and Variation) come from a generator seeded at reset, so each render repeats them while live passes vary.
- **Capture length.** The capture buffer holds three seconds, a quarter note at 20 BPM.

`tests/ExtendedEffectsTests.cpp` checks, on a unique pseudo-random input at 120 BPM:

- **Gate, Insert and Mix.** Each reproduces the exact expected sample layout: the third beat of each bar repeats its first sixteenth four times.
- **Decay, Volume and Chance.** Decay and Volume scale the repeats exactly, and Chance 0 never repeats.
- **Block size.** Any block size gives the same samples.
- **Repeatable randomness.** Chance 50% repeats some beats and not others, identically in each render.
- **Repeat switch.** It holds the first slice.
- **Pitch.** −12 semitones reads the slice at half speed.
- **Stopped transport.** Mix and Gate behave as described above.

The shared device checks compare an engine render with the processor run from position 0, sample for sample. Tempo maps, time signatures other than 4/4 and measured comparison with the reference are missing.

