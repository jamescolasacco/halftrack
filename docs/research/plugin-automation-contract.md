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

Missing: override and Automation Arm recording when a parameter moves in the plug-in's own editor, host learn, parameter-count changes, and automation of plug-ins inside racks.
