# Metronome contract

The metronome follows the reference's Metronome Settings menu. The reference's manual (consulted 2026-10-22) and its built-in lesson image `CountInMenu.png` show the menu's sections:

- Count-In: None, 1 Bar, 2 Bars, 4 Bars;
- Sound: Classic, Click, Wood;
- Rhythm: Auto, 1 Bar, 1/2, 1/2T, and more;
- Enable Only While Recording.

## Count-In

- **The choices.** Recording waits for none, one, two or four bars of the project's meter, as the reference's choices are. A bar lasts the meter's bar in crotchets: three in 6/8, three and a half in 7/8.
- **The engine.** An engine patch (`cmake/PatchCountIn.cmake`, see [dependencies](../dependencies.md)) adds the four-bar mode and the bar length. Before it, a bar of 6/8 counted in six beats, and 7/8 could not be expressed.
- **Scope.** The count-in is an application setting, as the reference's is (static reading: its ASongPrefData, set without an undo step, and its Metronome device is not saved in Sets). It is kept with the preferences (`Preferences::countInBars`, None by default), not with a project. Setting it is not an undo step, and it cannot change while recording. Projects saved by earlier versions still validate their count-in, which is ignored.

## Sound

- **The sounds.** Classic (the default), Click and Wood. They are Halftrack's own, made to the character of the reference's three, which are the reference's own samples and are not used:
  - Classic: a pure 1.1 kHz blip, the accent 1.58 kHz, about 20 ms long;
  - Click: a bright 3 kHz click with a little noise, the accent 4 kHz;
  - Wood: a woodblock of three inharmonic modes at 1.07 kHz, the accent 1.55 kHz, ringing for a few hundred milliseconds.
- **How they play.** `metronome::tick` (`src/MetronomeSounds.cpp`) synthesises each tick offline, deterministically. The Session writes the chosen sound's two ticks beside the preferences and points the engine's click track at them, at startup and whenever the choice changes.
- **Storage.** The choice is kept with the preferences (`Preferences::metronomeSound`), as the reference keeps its metronome's preset outside the Set.

## Rhythm

This follows a static reading of the reference's code (raw material in the ignored `artifacts/reverse-engineering/metronome/`).

- **The choices.** Auto, 1 Bar, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16 and 1/16T, stored as the reference stores them: 0 for Auto, −1 for a bar, otherwise the division in crotchets, with triplets as single floats.
- **Storage.** The rhythm is kept with the project; choosing one is an undo step, as in the reference.
- **How it plays.**
  - Auto ticks on the meter's denominator: eighths in 6/8 and 7/8, halves in 3/2.
  - A bar ticks once a bar.
  - A division ticks at its length if the bar is a whole number of them. Otherwise it plays as Auto, and returns when the meter fits it again.
  - Ticks fall on a grid of their length from the start, and the first of each bar is accented.
- **The menu.** Divisions that do not fit the project's meter are disabled; Auto and 1 Bar always fit.
- **The engine.** A patch (`cmake/PatchMetronomeRhythm.cmake`) lets the click generator read the project's rhythm when it is prepared, resolve it as above, and place its ticks. Bars are numerator × 4 / denominator crotchets. The engine's own bar count treats a bar of 6/8 as six crotchets.

## Level and output

As the reference's metronome does (static reading), the click plays at the cue volume on Cue Out, in both modes, apart from the master's volume and meter ([cue contract](cue-contract.md)). Its own Click Level, Halftrack's, multiplies the cue volume. At the default cue volume of −6 dB, a click is half as loud as Click Level alone made it before.

## Punch-In

- **The click.** The reference's manual (consulted 2026-10-22) says, of Enable Only While Recording, that with Punch-In on "the metronome will only be audible after the punch-in point". With that option, recording with Punch-In mutes the click before the loop's start: `Session::record` gives the engine a click range from that point, and the next Play clears it. Otherwise the pre-roll clicks as playback does. The reference's code shows no punch term in its click (static reading; [open questions](../open-questions.md#metronome)).
- **The count-in.** As the reference's code has it (static reading), a count-in comes only when recording starts inside the punch window: from before Punch-In's point, or after Punch-Out's, the pre-roll plays at once.

## The menu

The metronome's menu has sections, as the reference's does:

- Count-In: None, 1 Bar, 2 Bars, 4 Bars;
- Sound: Classic, Click, Wood;
- Rhythm: Auto to 1/16T;
- Enable Only While Recording;
- Halftrack's own Accent First Beat and Click Level.

## Evidence

`tests/MetronomeTests.cpp` checks:

- clicks sample by sample against generated impulse sounds: beats, accents, gain, loops, count-ins of none, one, two and four bars, recording placement and export exclusion, as before;
- one-bar count-ins in 6/8 (1.5 s at 120 BPM) and 7/8 (1.75 s);
- that three bars is refused;
- that the count-in survives undo, a new project and the next launch, and is not written into the project;
- each sound's plain and accented tick: repeatable, at its designed peak, starting and ending in silence, ringing at its fundamental, with the accent higher;
- that the click track plays Classic at first;
- that choosing Wood points it at files holding exactly the Wood ticks, and that the choice is kept.

The same suite checks Rhythm by comparing playback clicks sample by sample:

- eighths, eighth triplets and one tick a bar in 4/4;
- Auto ticking eighths in 6/8, and crotchets in 6/8;
- crotchets in 7/8, which play as Auto;
- the menu's list, the divisions that fit 6/8, 7/8, 3/2 and 4/4, a refused value, and undo.

At the cue volume of −6 dB the click plays at half its level, sample by sample; the suite's other cases use a cue volume of 0 dB.

With Enable Only While Recording, Punch-In and the brace at one second, a recording from the start clicks, sample by sample, only from one second on; an ordinary Play afterwards clicks from the start again. With the click always on, the pre-roll clicks from the start. With a one-bar count-in, recording from before the brace starts at once, and from inside it counts in.

The UI suite chooses four bars, Wood and 1/8 from the menu.

## Not yet

- Its count-in starting position: the reference rounds a count-in from mid-bar to the nearest bar line from the time-signature section start. Unmeasured, and Halftrack counts in whole bars before the position.
