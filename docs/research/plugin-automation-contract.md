# Plug-in automation contract

External plug-in parameters take Arrangement automation, as the reference's do.

## Behaviour

- **Listing.** `Session::pluginParameters` lists a loaded external plug-in's automatable parameters in the engine's order, leaving out the engine's own dry and wet levels. Each has its name, normalised 0–1 value and the plug-in's text for it. It is empty for native devices, unknown ids and missing plug-ins.
- **Writing.** `Session::setDeviceAutomation (pluginId, parameter, lane)` takes a parameter's index in that list and values from 0 to 1. Lanes are one undo step and refuse values outside 0–1, unordered beats and unknown parameters. The `device:<plugin>:<index>` lane key resolves to it, so captured automation, override bookkeeping and Re-Enable Automation see these lanes too.
- **Reading.** `PluginInfo::automation` for an external plug-in holds its lanes in 0–1 values.
- **Tempo.** Lanes keep their beats when the tempo changes; the plug-in is marked to remap on tempo change.
- **Switched off,** a lane leaves the parameter at whatever value it has; a value set by hand then applies.
- **Value set by hand.** A parameter keeps its value set by hand beside its automation, as the mixer's controls do. It is the value when its first points go in, or the last value set through `setPluginParameterValue`. When a parameter's automation goes, the parameter returns there at once: by undoing the lane or by clearing it (redo included). The plug-in's saved state is then written outside history, because renders start plug-ins from it.

Correction: undoing a plug-in's automation left the parameter where the automation last put it, which the engine then saved. Renders, which start plug-ins from the saved state, heard either value depending on when the engine's delayed save ran. On Linux CI that save sometimes came after the undo.
- **The automation editor** lists every plug-in parameter after the native device controls, as a percentage.

## Evidence

`tests/PluginAutomationTests.cpp` uses the VST3 gain fixture on a constant 0.5 signal, so each output sample is half the gain. It checks:

- the parameter list (Gain at 0.50, then the wrapper's Bypass);
- refusal of bad values, parameters and devices;
- that a Gain ramp from 0 to 1 over four beats is followed by the render and holds its last value;
- one-step undo and redo, with Gain back at its value set by hand straight after the undo;
- clearing the lane returning Gain there, and the render agreeing;
- reopening, with a render within 1e-6 of the original;
- that at 60 BPM the lane keeps its beats;
- that a switched-off lane holds one value, and that a value set by hand then applies.

- **The plug-in's own editor.** A move there overrides the parameter's lane, or with Automation Arm on while recording writes it, as Halftrack's own controls do. The engine reports editor changes on the message thread, so Halftrack listens to every hosted plug-in's parameters, keeping up as plug-ins come and go.
  - A change inside the editor's gesture (its begin and end) counts as a move. The gesture is an edit gesture: a recorded pass is a touch pass, ending on release, and an override joins its undo step.
  - The engine passes values on after a delay, so the gesture's end takes the hosted parameter's own value, which keeps the last move.
  - A change outside a gesture, as some editors send when they open or load a preset, only updates the value set by hand, and only while the parameter has no automation.
  - Each move writes the plug-in's saved state too, as renders start from it.
  - `hostedPluginParameter(pluginId, index)` gives the hosted parameter behind one of `pluginParameters`, which the suite moves as an editor does. The older index-based `setPluginParameter`, which moves it the same way, now overrides too.

  The plug-in automation suite checks that a change outside a gesture leaves the lane playing, that a gesture's moves override it while keeping its points, that the value at the gesture's end becomes the value set by hand and plays in the render, and that Re-Enable Automation brings the ramp back. Through the synthetic device with Automation Arm on, an editor gesture records its values from .3 to the .7 it ended on, and one undo removes them.

Missing: host learn, parameter-count changes, and automation of plug-ins inside racks.
