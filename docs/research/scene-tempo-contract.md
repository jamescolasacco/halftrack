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
- **The UI.** A scene cell shows its tempo and meter right of its name, and its launch triangle is green while it has either. The scene menu's Scene Tempo… and Scene Time Signature… dialogs set them; an empty field clears the value and an invalid one is refused with the range.

## Evidence

`tests/SceneTempoTests.cpp` runs through the synthetic device and checks:

- the settings, their ranges, clearing and one undo step each;
- a stopped launch applying at once, undone in one step;
- a scene without settings leaving the tempo;
- a launch on beat 2 waiting for the bar, the change landing as the scene's clip starts (0.0124 beats in a block later, which is one block at 140 BPM), the transport playing on throughout, and the clip advancing at 140 BPM (1.493 of 1.493 expected beats over 120 blocks);
- Stop cancelling a waiting change;
- Duplicate Scene keeping the settings, saving and reopening, and refusing three malformed projects while keeping the open one.

The UI suite sets both settings through the dialogs, checks the cell text, refuses 7/9 with its reason, clears the tempo and undoes all three.

## Not yet covered

The reference's tempo and meter fields in the scene column, Scene View, keyboard editing, recording scene changes into the Arrangement, and overriding Arrangement tempo automation are missing.
