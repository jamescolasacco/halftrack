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
- **Pan text.** The reference's pan formatter (0x1012d8dec) writes C within 0.01 of the centre. Otherwise it writes |pan| × 50, rounded half away from zero, with L or R, so the sides read 50L and 50R. `Session::panText` and `panFromText` do the same for every pan in Halftrack's mixer, rack chains, Drum Rack pads and automation editors.
- **Rack chains.** Chain volume and chain sends inside racks use the same laws. This is inferred: the reference's chains have the same mixer device as tracks, but their converters were not traced.

`src/LevelScale.h` builds the tables the way the reference's builder does: in single precision, with its snap of near-zero levels and its correction of the last four entries. The reference's math library may round its powers of ten differently in the last place, so no bit-exact result is claimed.

## Pan modes

The reference's panner, from a static reading of its code (kernel 0x10198be9c, mode handling 0x1024ccc38 and 0x1024cd228), plays in Halftrack's track and group mixer.

- **Stereo.** The pan is a balance law. The left channel is scaled by √2·cos and the right by √2·sin of (pan + 1)·π/4, and neither falls below 1e-5. The centre is unity on each side. A hard pan lifts its side 3 dB and keeps the other at −100 dB, not silence.
- **Split Stereo.** Each input channel pans on its own, with plain cos and sin of (its pan + 1)·π/4 and no lift or floor. Left Split Stereo and Right Split Stereo start hard left and hard right, which keeps the channels as they are. With both at the centre, each side carries the two channels 3 dB down. A mono input feeds both of the panner's inputs.
- **Switching.** Switching blends the two matrices linearly over 75 samples, the reference's default ramp. Parameter moves are smoothed over the engine mixer's 15 ms.
- **The mode.** The mode is a mixer setting, not a control: it cannot be automated or mapped. Each switch is one undo step, and it applies to every selected track when the switched track is among them. It is saved as `halftrackPanMode` (1 for Split Stereo), with `halftrackSplitLeft` and `halftrackSplitRight` beside it.
- **Split pans.** The split pans are controls like the pan. They are automatable (`AutomationTarget::leftSplitPan` and `rightSplitPan`, lane keys `mixer:<track>:leftsplit` and `:rightsplit`), MIDI- and key-mappable, and clip envelopes can drive them (mixer controls 2 and 3). Their text is the pan's.
- **Older mixers.** Mixers from projects older than Halftrack's own track mixer, and the Main's, have no split pans and refuse the switch.
- **Interface.** As in the reference, each pan control's context menu has one item: Split Stereo Pan Mode on the pan, Stereo Pan Mode on the split pans. In Split Stereo the Session strip shows a Left slider above a Right one in place of the knob, labelled L and R on strips at least 93 px wide. The Arrangement and group headers show the two side by side in the pan's place. The Mixer's automation and clip envelope choosers offer the pans of the current mode only.

## Selection linking

From a static reading of the reference's code, its linked groups are rebuilt from the selection (`AMixerLinkedTimeableGroups`, 0x102370080), and its value rule is at 0x101949210. Halftrack follows them.

- **Rule.** With two or more tracks selected, moving one selected track's mixer control moves the same control on the other selected tracks.
  - Volume keeps its decibel offset along the volume scale, and sends (per return) along the send scale.
  - The pans keep their offset in travel. They move only among tracks in the same pan mode, and Left and Right Split Stereo move separately.
  - The activator copies the new state.
- **Offsets.** Offsets count from where each control stood at its first linked move since the selection changed. A track held at an end of its travel gets its offset back, and two controls at the same place move as one.
- **Every route.** Linking acts in Session's setters, so mouse, MIDI, key and control-surface moves all link, and automation recording captures every linked track. Automation playback does not link.
- **Undo.** The moved control and the linked ones are one undo step (the reference's grouping is not read).
- **Resets.** Double-clicking a control resets it (`Session::resetTrackControl`), and every linked control takes exactly the same default, as the reference's reset to default does.
- **Selection.** Pressing a control of a selected track keeps the selection. The interface reports its selection with `Session::setSelectedTracks`.
- **Not linked.** Groups, the Main, rack chains and device editors are not linked in Halftrack yet. Solo, arm, monitoring and the pan mode are not linked by this rule in the reference either; the pan mode switches every selected track through its own menu.

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
- pan text at the centre, near it and at the sides, and reading it back;
- that a track at −14 dB is saved at 0.5 of its travel, and a send at −20 dB at 0.5 of its own;
- that a rendered mix plays the track and a post-fader send at those levels;
- CC values 1, 64, 100, 108 and 127 against the model;
- that a generated legacy project keeps its fader, send, Main level (in the engine's parameter data), automation and clip envelope levels;
- that the legacy project renders as the project it was made from, within 1e-7 of full scale;
- that it is converted once when saved again, and that an unknown marker is refused.

The suite's pan checks render a source with 0.5 on the left and 0.25 on the right. They check:

- the balance law at the centre, hard left (√2 and 1e-5) and halfway right;
- the split defaults keeping the channels, swapped split pans swapping them, and both at the centre mixing them 3 dB down;
- two tracks switched in one undo step, a split pan move as one undo step, and Stereo mode playing its pan again;
- a reopen playing as before, and refusals of a bad pan mode, a split pan out of range and a pan mode on the Main;
- a left split pan's lane sweeping it across, which the render follows, and a hand move overriding the lane.

The suite's linking checks select two of three tracks. They check:

- a −6 dB move giving the other selected track −18 from −12, with the unselected one left alone, and one undo for both;
- a track held at the bottom and getting its offset back;
- no linking with one track selected;
- pans moving by the same amount, a Split Stereo track keeping its pans, and split pans moving together;
- an Off send following to −6 dB and sends keeping −14 dB apart;
- activators copying;
- resets giving every linked fader 0 dB and every linked send Off;
- a mapped controller's move linking too.

The UI suite checks that pressing a selected track's fader keeps both selected, that dragging it moves the other, and that a double-click resets both.

The UI suite checks the pan menus' items, Left over Right in the Session strip and side by side in the Arrangement header, the split pans' text, the automation choosers, and undo.

`tests/AutomationTests.cpp` checks:

- the law's coordinates;
- a rendered volume ramp following it;
- legacy-floor refusals;
- that a current project's tiny level loads as silence.

`tests/RackTests.cpp` renders a macro moving a chain volume along the law. The UI suite checks a track fader's travel (0 dB at 0.85, −14 dB halfway, silence at the bottom) and a Session send knob's (−20 dB halfway, 0 dB at the top).

## Not yet covered

- The reference's automation chunking (ramps of 1/128 of the range, at least 256 samples) and its 4 ms smoothing. Halftrack's engine smooths on its own schedule.
- The reference's dB text shows −inf from −69.7 dB (medium confidence); Halftrack shows −inf only for silence.
- The clip gain knob's travel along the reference's scale 3.
- Moving several selected faders with equal dB offsets.
