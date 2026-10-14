# Channel utility implementation and evidence

The original Halftrack device follows the public workflow described in the [reference manual's Utility section](https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/#utility), consulted 2026-10-14. That description establishes independent input polarity switches, stereo channel selection, width and alternative mid/side control, mono and bass-mono processing, bass audition, gain, balance, mute and DC removal. Single-channel selection disables width/mid-side control. This is a behavioral specification, not recovered source, and it does not establish filter coefficients, interpolation, precise order or identical sound. No reference media, presets, implementation or visual assets are distributed.

## Original signal contract

All samples use stereo floating point with headroom. Gain ranges from a serialized -100 dB silence sentinel to +35 dB. Balance attenuates the opposite channel linearly while retaining unity on the selected side. Polarity precedes channel selection. Left/Right duplicate the chosen input; Swap exchanges inputs. Stereo width multiplies the difference signal from 0 to 4 while preserving the sum. The alternate signed mid/side setting retains both components at zero, reduces side toward zero at -1, and reduces mid toward zero at +1. Mono forces the side weight to zero. Disabled values remain serialized and undoable.

Bass processing uses a bilinear two-pole Butterworth low-pass at 50–500 Hz, default 120 Hz. The low side component is subtracted from the side signal; audition instead selects low mid in both channels while bass mono is enabled. These filters retain unweighted input history, followed by mid/side weights. Output gain/balance follows, then a 5 Hz first-order DC blocker normalized to unity at Nyquist, then mute. Filters continue maintaining state while disabled. Gain, balance, mid/side weights and mute use five-millisecond linear transitions after first processing; discrete switches and bass-frequency coefficient changes are not smoothed. These are explicit implementation defaults pending controlled reference measurements.

## Application integration

The device inserts after instrument/return input and existing insert effects, before send taps and mixer. Audio tracks, MIDI instrument tracks and returns share one processor. Add, edit, bypass, move and remove participate in engine undo. The native editor exposes every implemented parameter, groups slider drags into one undo step and follows the device's stable identity. Project XML retains all fourteen properties. Nonfinite/out-of-range values are rejected before replacing the current edit; absent properties select documented defaults.

Parameter packets cross a bounded single-producer/single-consumer queue. If full, the message-thread timer retries the latest full packet. Playback is drained before export resets that queue. Audio processing uses no owned heap allocations, locks or I/O. This does not certify the engine or arbitrary hosted plugins.

## Verification boundaries

`utility-acceptance` uses 320 independent constant-matrix cases, golden bass impulse samples, closed-form DC decay at four rates, full-scale headroom, exact steady mute, block partition invariance, filter reset, finite-input handling, a known five-millisecond gain ramp and mono/mute transitions with existing filter history.

Generated audio and MIDI projects exercise complete-file exports, insert ordering, pre-fader return processing, bypass, removal, undo/redo, stable identity, all-property persistence, and malformed-load preservation. Production editor handlers cover gesture undo and retained disabled values; generated native-window tests cover removal, undo/reopen and project replacement. Runtime C++ allocation guards and concurrent packet checks cover the owned DSP and its queue separately.

Automated synthetic tests do not establish physical hardware performance, mouse delivery, proprietary DSP identity, long-session stability or complete device parity. Native manual checks are listed in `docs/manual-test-phase1.md` and remain unrun unless separately recorded.
