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
