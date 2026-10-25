# Scene tempo and time signature contract

Halftrack's scene tempo and time signature follow the reference's [Session View chapter](https://www.ableton.com/en/live-manual/12/session-view/), consulted 2026-10-21. The chapter gives the ranges, clearing and the lit launch button. It does not say when a quantized launch applies the change, so the defaults below are recorded in [open questions](../open-questions.md).

## Behaviour

- **Settings.** `SceneInfo::tempo` and `SceneInfo::timeSignature` are optional. `setSceneTempo (sceneId, bpm)` accepts 20 to 999 BPM and `setSceneTimeSignature (sceneId, meter)` accepts 1 to 99 over 1, 2, 4, 8 or 16. An empty value clears the setting. Each change is one undo step. The settings are scene properties, so they are saved, reopened and copied by Duplicate Scene.
- **Loading.** A tempo out of range, a meter out of range, or a meter with only one of its parts is rejected before the open project is replaced.
- **Launching.** Launching a scene that has either setting makes it the project's, as one undo step named "Launch scene":
  - with the transport stopped, or with launch quantization None, at once;
  - otherwise at the scene's launch boundary.

  The message thread compares the engine's sync point with the boundary and applies the change once the engine has processed up to it, so the scene's clips play their first block at the new tempo when the boundary ends a block. The clips keep their launch boundary and beat phase. A scene with neither setting changes nothing, and leaves an earlier launch's change waiting.
- **Cancelling.** Stop, locating, Return to Arrangement and replacing the project cancel a change still waiting.
- **Exceptions.** An automated tempo (more than one tempo point) and a meter with several changes are left as they are. Nothing is changed while the Arrangement records or an export runs.
- **The UI.** As the reference's View and scene menus switch them (2026-10-24), Scene Tempo and Time Signature shows each scene's tempo and time signature fields at the right of its cell; the reference's default Set hides them (`ShouldSceneTempoAndTimeSignatureBeVisible` false), and the choice is kept with the project as view state (`Session::setSceneSettingsShown`). A field shows the scene's own value, or the song's dimmed. A click on a field, without a drag, opens its dialog; an empty value clears it and an invalid one is refused with the range. Dragging up or down on a part of a field steps it as the Control Bar's number fields drag ([value controls](value-controls-contract.md)): the tempo's whole beats or hundredths, from 20 to 999 BPM, or the signature's numerator or denominator, from the scene's own value or else the song's, a tenth as fast with Shift, each drag one undo step (2026-10-25). The launch triangle is green while the scene has either. The fields do not take the value keys.

## Evidence

`tests/SceneTempoTests.cpp` runs through the synthetic device and checks:

- the settings, their ranges, clearing and one undo step each;
- a stopped launch applying at once, undone in one step;
- a scene without settings leaving the tempo;
- a launch on beat 2 waiting for the bar, the change landing as the scene's clip starts (0.0124 beats in a block later, which is one block at 140 BPM), the transport playing on throughout, and the clip advancing at 140 BPM (1.493 of 1.493 expected beats over 120 blocks);
- Stop cancelling a waiting change;
- Duplicate Scene keeping the settings, saving and reopening, and refusing three malformed projects while keeping the open one.

The UI suite finds the fields hidden at first, shows them from the scene menu, sets both settings by clicking the fields, checks the fields' text, drags the tempo's whole beats and hundredths and the signature's denominator, each one undo step, refuses 7/9 with its reason, clears the tempo and undoes all three. The track order suite keeps the switch with the project.

## Not yet covered

Scene View, keyboard editing of the scene fields, recording scene changes into the Arrangement, and overriding Arrangement tempo automation are missing.
