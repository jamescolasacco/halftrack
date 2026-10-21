# Parameter identity contract

A MIDI mapping, an automation lane or a configured control names the parameter it drives. That name must keep pointing at the same parameter through the edits that move devices and tracks around, and through reopening. The reference behaves this way: a mapping stays on its control when devices are reordered and is not copied to a duplicated track.

## Names

- **Mixer controls.** Named by their track's identity (`mixer:<track>:volume`), which never changes for a track.
- **Native device controls.** Named by the device's identity and the control's index (`device:<device>:<index>`). Halftrack defines those indices, and a device's controls only ever gain new indices at the end.
- **Hosted plug-in parameters.** Named by the plug-in's identity and the parameter's index in `pluginParameters`. Plug-ins can reorder their parameters between versions, so Halftrack also keeps the plug-in's own parameter ID wherever the name is saved:
  - A **MIDI mapping** of a hosted parameter saves `parameterID` beside its target.
  - A **mapping that has drifted** is reconciled after loading, and each second while a plug-in finishes loading. If its index no longer names that parameter, it follows the ID to the parameter's new index. If no parameter has the ID, the mapping is hidden and drives nothing, rather than driving whatever now sits at the old index.
  - **Configured parameters** (Configure, below) are saved by ID only.
  - **Automation lanes and clip envelopes** of hosted parameters are kept by the engine under the parameter ID already.
- **Unavailable plug-ins.** A plug-in that cannot load keeps its saved mappings and configuration unchanged. They are hidden until it loads again.

## Configure

As in the reference, Configure in a hosted plug-in's device panel chooses which of its parameters show in the panel as controls. They show four to a column, with the plug-in's own name and value text. Each can be automated, recorded with Automation Arm, and MIDI mapped in Map Mode like a device's own control. Choosing a parameter again hides it. Configuring is one undo step; at most 128 parameters. Moving a control is not an undo step, like the plug-in's own window. Configuring by touching a control in the plug-in's window is not implemented.

## Controller gestures and undo

A mapped controller's moves form one undo step until it rests for half a second. Any other edit made while it moves now ends that step first, so it is an undo step of its own. Before, it joined the controller's step, and one undo reverted both. A drag begun meanwhile is no longer ended by the controller coming to rest.

## Evidence

`tests/ParameterIdentityTests.cpp` maps a controller to an EQ Three control and another to the track volume, and automates a second EQ control. It then checks:

- **Reordering.** After the EQ moves behind a Saturator, and after its track moves below another, the mapping and automation still drive only their targets.
- **Duplicating.** A duplicate track's EQ has its own identity, a copy of the automation, and no mapping. The mapping still drives only the original.
- **Deleting.** Deleting the EQ hides its mapping, and the controller does nothing. Undo restores the device, mapping and automation, and only them: the tracks added and duplicated while the controller was moving remain.
- **Reopening.** After reopening, the mappings and automation are on the same controls.
- **A hosted VST3 fixture (Gain and the wrapper's Bypass):**
  - Unknown, repeated and native parameters cannot be configured. Configuring Gain shows it, with undo and redo.
  - Gain maps from 0 to 1, a controller drives it and only it, and its panel setter works.
  - The saved mapping and configuration carry Gain's own ID.
  - A project whose mapping index was moved onto Bypass opens with the mapping back on Gain, leaving Bypass alone.
  - A mapping whose parameter ID has gone is hidden and drives nothing.
  - With the plug-in unscanned, it opens as a placeholder with its mapping hidden. Saved that way and reopened once scanned, the configuration and mapping return.
  - A configuration naming Gain twice is refused on load, keeping the open project.

The UI suite inserts the fixture and checks Configure in its panel:

- choosing Gain shows a Gain control whose MIDI-map target can be mapped;
- moving it sets the plug-in's Gain;
- choosing it again hides it.

## Not yet covered

- Configuring by touching a parameter in the plug-in's window.
- Hosted parameter changes from the panel as undo steps.
- A plug-in that changes its parameter count at run time.
- Comparison with the reference on Windows.
