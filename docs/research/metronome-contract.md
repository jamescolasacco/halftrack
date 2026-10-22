# Metronome contract

The metronome follows the reference's Metronome Settings menu. The reference's manual (consulted 2026-10-22) and its built-in lesson image `CountInMenu.png` show the menu's sections:

- Count-In: None, 1 Bar, 2 Bars, 4 Bars;
- Sound: Classic, Click, Wood;
- Rhythm: Auto, 1 Bar, 1/2, 1/2T, and more;
- Enable Only While Recording.

## Count-In

- **The choices.** Recording waits for none, one, two or four bars of the project's meter, as the reference's choices are. A bar lasts the meter's bar in crotchets: three in 6/8, three and a half in 7/8.
- **The engine.** An engine patch (`cmake/PatchCountIn.cmake`, see [dependencies](../dependencies.md)) adds the four-bar mode and the bar length. Before it, a bar of 6/8 counted in six beats, and 7/8 could not be expressed.
- **Scope.** The count-in is kept with the project and is one undo step, as before.

## Sound

- **The sounds.** Classic (the default), Click and Wood. They are Halftrack's own, made to the character of the reference's three, which are the reference's own samples and are not used:
  - Classic: a pure 1.1 kHz blip, the accent 1.58 kHz, about 20 ms long;
  - Click: a bright 3 kHz click with a little noise, the accent 4 kHz;
  - Wood: a woodblock of three inharmonic modes at 1.07 kHz, the accent 1.55 kHz, ringing for a few hundred milliseconds.
- **How they play.** `metronome::tick` (`src/MetronomeSounds.cpp`) synthesises each tick offline, deterministically. The Session writes the chosen sound's two ticks beside the preferences and points the engine's click track at them, at startup and whenever the choice changes.
- **Storage.** The choice is kept with the preferences (`Preferences::metronomeSound`), as the reference keeps its metronome's preset outside the Set.

## The menu

The metronome's menu has sections, as the reference's does:

- Count-In: None, 1 Bar, 2 Bars, 4 Bars;
- Sound: Classic, Click, Wood;
- Enable Only While Recording;
- Halftrack's own Accent First Beat and Click Level.

## Evidence

`tests/MetronomeTests.cpp` checks:

- clicks sample by sample against generated impulse sounds: beats, accents, gain, loops, count-ins of none, one, two and four bars, recording placement and export exclusion, as before;
- one-bar count-ins in 6/8 (1.5 s at 120 BPM) and 7/8 (1.75 s);
- that three bars is refused;
- each sound's plain and accented tick: repeatable, at its designed peak, starting and ending in silence, ringing at its fundamental, with the accent higher;
- that the click track plays Classic at first;
- that choosing Wood points it at files holding exactly the Wood ticks, and that the choice is kept.

The UI suite chooses four bars and Wood from the menu.

## Not yet

- Rhythm (the tick division), and Auto following the meter's denominator. Halftrack ticks every crotchet.
- The reference's levels and routing: its metronome plays at the Preview volume on the cue output.
- Whether the count-in belongs to the project or the application, which is being researched (see [open questions](../open-questions.md#metronome)).
