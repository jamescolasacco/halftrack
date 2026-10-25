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

## Scene View (2026-10-25)

The reference's manual (section 7.2.2, consulted 2026-10-25): the Scene View is where scene properties are set. Selecting scenes, clicking a scene's tempo or time signature control, or clicking the Main track's title bar opens it. Its upper section's Tempo and Signature edit the selected scenes', and its lower section their Follow Actions. With one scene selected its title bar shows the scene's name, number and colour, with several the number selected; its context menu renames the scenes and colours them from the palette, for several scenes too.

Halftrack:

- **Showing it.** Selecting scenes in the Session (clicking one, Shift- or Cmd-clicking more, which a click on a scene's tempo or signature field does too) or clicking the Main's title above the scenes shows the Scene View in the detail view, in the device view's place. Selecting a track, opening a clip, choosing the clip or device view, or going to the Arrangement shows those instead.
- **The title bar** shows the scene's number and name on its colour, or how many scenes are selected. Its right-click menu has Rename, which names every selected scene the same as one undo step, and the colour grid, which colours them all.
- **Tempo and Signature** show the scene the selection follows: its own values, or the song's dimmed when it has none. Dragging their parts, typing, or Delete (which clears them) sets every selected scene's, as one undo step ("Scene tempo", "Scene time signature").
- **Not here.** The reference's lower section, the scenes' Follow Actions: Halftrack's scenes have none.


`tests/SceneTempoTests.cpp` runs through the synthetic device and checks:

- the settings, their ranges, clearing and one undo step each;
- a stopped launch applying at once, undone in one step;
- a scene without settings leaving the tempo;
- a launch on beat 2 waiting for the bar, the change landing as the scene's clip starts (0.0124 beats in a block later, which is one block at 140 BPM), the transport playing on throughout, and the clip advancing at 140 BPM (1.493 of 1.493 expected beats over 120 blocks);
- Stop cancelling a waiting change;
- Duplicate Scene keeping the settings, saving and reopening, and refusing three malformed projects while keeping the open one.

The UI suite shows the Scene View for a selected scene with its number and name, gives the scene a tempo five BPM above the song's by dragging its Tempo, counts two selected scenes in the title bar, types 3/4 into Signature for both, clears their tempo with Delete and renames both, each one undo step, which four undos take back; it then sees a track's selection show the devices again and a click on the Main's title open the Scene View. It also finds the fields hidden at first, shows them from the scene menu, sets both settings by clicking the fields, checks the fields' text, drags the tempo's whole beats and hundredths and the signature's denominator, each one undo step, refuses 7/9 with its reason, clears the tempo and undoes all three. The track order suite keeps the switch with the project.

## Not yet covered

Scene follow actions (the Scene View's lower section), keyboard editing of the launcher's scene fields, recording scene changes into the Arrangement, and overriding Arrangement tempo automation are missing.
