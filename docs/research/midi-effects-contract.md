# MIDI effects implementation contract

Halftrack implements seven original MIDI effects as native effect kinds 17–23: Arpeggiator, Chord, Scale, Pitch, Velocity, Random and Note Length. The reference manual describes these workflows ([MIDI effect reference](https://www.ableton.com/en/live-manual/12/live-midi-effect-reference/), consulted 2026-10-20); it does not specify their algorithms, timing rules or random sequences, so every rule below is a Halftrack default. No reference binary output is used.

## Placement, state and threading

MIDI effects insert only on MIDI tracks, immediately before the instrument and after earlier MIDI effects. `movePlugin` keeps MIDI effects ahead of the instrument and audio devices after it. Audio and return tracks reject them before any undo transaction. They reuse the native effect packet: named XML properties, strict validation (including integer-only controls), one-step undo gestures, generic editor, bypass, removal and project reopening. Bypassed devices pass MIDI unchanged.

`midifx::Processor` owns fixed arrays: per input note (16 channels × 128 notes) output assignments, output reference counts, 64 held arpeggiator notes and 512 pending releases. `process()` performs bounded work and never allocates; parameter packets arrive through the existing preallocated FIFO at block boundaries. The plugin copies note messages into a fixed 512-event input array, processes them into a fixed 2,048-event output array and rebuilds the engine-owned MIDI buffer in time order, interleaving generated notes with retained non-note events through a reserved scratch array that it swaps with the engine buffer. Engine-owned MIDI buffers can still grow when a block exceeds their prior capacity; this is the dependency's existing MIDI-buffer behaviour and is not certified allocation-free.

Every release uses the output pitches chosen at its note-on, so parameter changes while a key is held cannot strand a note. When two inputs produce the same output pitch, the second retriggers it (note-off then note-on) and it is released only after its last owner. All Notes Off from the engine releases every generated note and clears held/pending state.

## Devices

- **Pitch**: transposes by −48…48 semitones; input notes outside [Lowest, Highest] or results outside 0–127 are blocked together with their releases.
- **Chord**: up to six additional shifts (−36…36 semitones; 0 is off), each with a velocity scale (output floor 1). Duplicate pitches merge. Default adds a major third and fifth.
- **Scale**: twelve roots and thirteen scales (major, natural/harmonic/melodic minor, five other modes, two pentatonics, blues, chromatic). Each note moves to the nearest scale degree with equal distances resolving downward, then transposes by −24…24 semitones. Every root/scale/note combination is checked against a brute-force search.
- **Velocity**: Clip maps the input span [input lowest, highest] onto the output span with curve exponent 2^(−2·drive); Gate does the same but blocks notes outside the input span; Fixed outputs one value. Optional ±random spread uses the processor's seeded generator.
- **Random**: with probability Chance, offsets a note by Interval × k semitones, k in 1…Choices (uniform in Random mode, cycling in Alternate mode), upward, downward or either direction. A fixed xorshift seed makes renders reproducible after reset.
- **Note Length**: replaces incoming releases with a generated length in milliseconds or a tempo division, scaled by Gate; Note-off trigger starts the note at key release with its original velocity. A repeated pitch ends the previous generated note first.
- **Arpeggiator**: Up, Down, Up-down, Down-up, Converge, Diverge, As played and Random styles over one to four octaves; nine rates from 1/32 to one bar including triplets; gate 5–200% (longer gates overlap and retrigger repeated pitches); Hold latches a released gesture until the next gesture replaces it; velocity as played or fixed. While the transport plays or renders, steps lie on the project beat grid (constant project tempo) and a key pressed between grid points waits for the next step; when stopped, the first key anchors a free-running grid on the sample clock.

## Evidence

`tests/MidiEffectsTests.cpp` compares complete generated event lists (time, pitch, velocity) with hand-derived expectations for every device, block-size independence of the random arpeggiator, panic, and identity of channel/MPE source. Engine renders of Pitch, Chord, Scale, Velocity, Note Length, Arpeggiator and zero-chance Random through the built-in instrument match renders of the equivalent explicitly written notes within 2e-6 (24-bit). Device order, bypass, undo, removal, save/reopen and fractional-value rejection are checked through the production Session facade. The real-time fixture floods every kind with alternating notes at parameter extremes and both time bases with zero intercepted allocations and time-ordered output.

Not implemented: CC Control, Expression Control, Note Echo, MPE-aware per-note expression, arpeggiator retrigger/repeat/transpose-step/swing options, scale fold/custom maps, and velocity "relative" (release velocity) operation. No numerical or timing comparison with the reference devices has been measured.

## CC Control (2026-10-21)

CC Control follows the manual's description in the [MIDI effect reference](https://www.ableton.com/en/live-manual/12/live-midi-effect-reference/), consulted 2026-10-21. It has:

- Mod Wheel, Pitch Bend and Pressure;
- Custom A, a button sending minimum or maximum on a chosen CC (sustain, CC 64, by default);
- Custom B–M, twelve dials, each on a chosen CC;
- a Send button that transmits every value, and Learn.

Halftrack sends a control's message at the start of the block after it changes, before the block's notes:

- **Mod Wheel** is CC 1.
- **Pitch Bend** is a 14-bit pitch-wheel message centred at 8192.
- **Pressure** is channel pressure.
- **Custom A** is 127 or 0 on its CC.
- **Custom B–M** default to CCs 20–31.
- **On restarts.** Every control is sent again when the transport starts or jumps back, and when the channel changes.
- **Notes** pass through unchanged.

`tests/MidiEffectsTests.cpp` checks:

- the exact bytes for each control, including bend extremes and centre;
- reassigning a CC sends the value on the new one;
- a channel change sends all sixteen messages on the new channel;
- starting the transport sends all, while steady playback sends only changes;
- notes pass through.

The Send button, Learn, renaming, Push display and 1–8 / 9–16 pages are missing.

