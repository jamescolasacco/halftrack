# Mixer laws contract

Halftrack's faders and sends follow the reference's level scales. The scales come from a static reading of the reference's code (Live 12.2.1, arm64 slice sha256 `b37cb738…bd3fc`, 2026-10-23); nothing was launched or measured. The research notes stay in the ignored `artifacts/reverse-engineering/mixerlaw/`, with a model of the scales.

## The laws

Each fader or knob has a position from 0 to 1, which is what the reference saves and automates. A level scale turns the position into a gain. It interpolates in amplitude between 101 fixed levels, and the bottom of the travel is silence.

- **Volume.** Track, group, return, Main and cue volume use the reference's scale 1, from −70 to +6 dB, with 0 dB at 0.85 of the travel. The research model gives:

  | Position | 1.0 | 0.9 | 0.85 | 0.7 | 0.55 | 0.5 | 0.4 | 0.3 | 0.2 | 0.1 | 0.01 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | dB | +6 | +2 | 0 | −6 | −12 | −14 | −18 | −24.2 | −34.4 | −48.6 | −66.4 |

- **Sends.** Sends use scale 0, from −70 to 0 dB:

  | Position | 1.0 | 0.85 | 0.7 | 0.5 | 0.4 | 0.3 | 0.2 | 0.1 |
  |---|---|---|---|---|---|---|---|---|
  | dB | 0 | −6 | −12 | −20 | −24 | −30.2 | −40.4 | −54.6 |

- **Silence.** The mixer silences any gain at or below 0.00032. That is below 0.000232 of the volume travel and 0.00167 of the send travel. Session reports silence as −100 dB.
- **Rack chains.** Chain volume and chain sends inside racks use the same laws. This is inferred: the reference's chains have the same mixer device as tracks, but their converters were not traced.

`src/LevelScale.h` builds the tables the way the reference's builder does: in single precision, with its snap of near-zero levels and its correction of the last four entries. The reference's math library may round its powers of ten differently in the last place, so no bit-exact result is claimed.

## Engine and Session

- **Engine patch.** `cmake/PatchFaderLaws.cmake` lets the host install a volume law and a send law in the engine (see [dependencies](../dependencies.md#tracktion-fader-laws-patch)). Session installs both before any Edit exists. The engine's volume plug-in, aux sends and their automation then read positions through them, on the audio thread too.
- **Session API.** Session's API stays in decibels, from −100 (silence) to +6, or to 0 for sends. `Session::automationValueToProportion` and `automationProportionToValue` convert volume without a Session. `Session::levelRange (send, silence)` gives a slider's travel.
- **Automation and MIDI.** Automation moves the position in a straight line between points, as the reference's does. A MIDI controller moves the position from the mapping's minimum to its maximum, so with the full range CC 64 is −13.84 dB, CC 108 is +0.02 dB and CC 127 is +6 dB.
- **Interface.** The Arrangement and Session faders, group and Main faders, send knobs, the Sends panel, the cue and preview volume and the rack chain controls all move along these laws.

## Projects saved before

Projects saved before this change hold positions by the engine's own law, where dB = 20 ln(position) + 6. They have no `halftrackFaderLaw` marker on the Edit.

- **Conversion.** Loading such a project converts every position to the position of the same gain under the new laws. That covers mixers' levels, manual levels, the engine's stored parameter data and volume automation, and sends' levels, stored data and automation. It also covers Session clips' volume automation envelopes.
- **Modulation.** Volume modulation scales a position, so it has nothing to convert. It now scales along the new law.
- **Changes in sound.** Levels below −70 dB become silence. Segments between automation points follow the new law's shape between the same end levels.
- **Validation.** A legacy project is checked against the old law's ranges before conversion; for example, a volume position below e^−5.3 is refused.
- **Marker.** Every project is saved with `halftrackFaderLaw="2"`. A project with any other marker value is refused, and the open project is kept.

## Evidence

`tests/FaderLawTests.cpp` (suite `fader-laws`) checks:

- the model's levels both ways for volume and sends, and the silence thresholds;
- the cue knob's −70 dB bottom;
- that a track at −14 dB is saved at 0.5 of its travel, and a send at −20 dB at 0.5 of its own;
- that a rendered mix plays the track and a post-fader send at those levels;
- CC values 1, 64, 100, 108 and 127 against the model;
- that a generated legacy project keeps its fader, send, Main level (in the engine's parameter data), automation and clip envelope levels;
- that the legacy project renders as the project it was made from, within 1e-7 of full scale;
- that it is converted once when saved again, and that an unknown marker is refused.

`tests/AutomationTests.cpp` checks:

- the law's coordinates;
- a rendered volume ramp following it;
- legacy-floor refusals;
- that a current project's tiny level loads as silence.

`tests/RackTests.cpp` renders a macro moving a chain volume along the law. The UI suite checks a track fader's travel (0 dB at 0.85, −14 dB halfway, silence at the bottom) and a Session send knob's (−20 dB halfway, 0 dB at the top).

## Not yet covered

- The reference's pan law floor: it keeps the far side of a hard pan at 1e-5, not silence. Halftrack's mixer pan keeps its own normalized law.
- The reference's automation chunking (ramps of 1/128 of the range, at least 256 samples) and its 4 ms smoothing. Halftrack's engine smooths on its own schedule.
- The reference's dB text shows −inf from −69.7 dB (medium confidence); Halftrack shows −inf only for silence.
- The clip gain knob's travel along the reference's scale 3.
- Moving several selected faders with equal dB offsets.
