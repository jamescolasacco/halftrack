# Audio Effect Rack contract

Audio Effect Racks follow the reference rack workflow ([Instrument, Drum and Effect Racks](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/), consulted 2026-10-20). Devices are grouped into a rack. The rack runs any number of parallel chains on its input and sums them at its output. Each chain has its own volume, pan, activator (mute) and solo, and eight macro knobs can drive device controls inside the rack. This is Halftrack's own implementation on the engine's rack graph. Its sound and limits are not measured against the reference.

## Interface

- `groupDevices(trackIndex, pluginIds, rackId)` wraps audio devices of one track into a new rack. The devices keep their track order, and the rack takes the first one's position.
  - The rack gets one chain, named after the first device, holding those devices.
  - With an empty list it adds an empty rack, with no chains, where new audio devices go.
  - MIDI effects, instruments, racks and unknown or repeated IDs are rejected. Grouping is one undo step.
- `ungroupRack(rackId)` puts a one-chain rack's devices back on the track in the rack's place and removes the rack. Their macro mappings are removed with it. Racks with two or more chains are rejected.
- `addRackChain(rackId, chainId)` appends an empty chain named "Chain N"; `removeRackChain(chainId)` deletes a chain and its devices.
- `setRackChain(chainId, ChainSettings)` sets:
  - the chain's name (1–128 characters);
  - volume (silence to +6 dB, on the track fader law);
  - pan (−1 to +1, on the track pan law);
  - mute and solo.
- `addNativeEffectToChain(chainId, kind)` and `addUtilityToChain(chainId)` append a device to a chain. MIDI effects and instruments are rejected. A chain holds at most 64 devices, and a rack at most 64 chains.
- Devices are addressed by ID on tracks and in chains alike:
  - `setDeviceEnabled`, `removeDevice` and `showDeviceEditor`.
  - `moveDevice`, which moves a device only within its own track or chain.
  - The existing `setNativeEffectSettings`, `setUtilitySettings` and `setDeviceAutomation` also find chain devices.
- Macros:
  - `setRackMacro(rackId, macro, value)` takes a value from 0 to 1.
  - `renameRackMacro` takes a name of 1–64 characters.
  - `mapRackMacro(rackId, macro, pluginId, parameter, minimum, maximum)` maps a native device control in the same rack over a range in the control's own units. The minimum may exceed the maximum to invert the range.
  - `unmapRackMacro(pluginId, parameter)` removes a mapping.
- `PluginInfo::isRack`, `chains` (ID, settings, devices) and `macros` (name, value, mappings) describe a rack in snapshots.

## Device view

- Clicking a device's title selects it. Shift-click extends the selection within the same track or chain, and Cmd/Ctrl-click toggles a device.
- With devices focused, Cmd/Ctrl+G groups the selection, Cmd/Ctrl+Shift+G ungroups a selected one-chain rack, and Delete removes the selected devices.
- A device's menu offers Move left/right, Group, Ungroup and Add chain (racks), Activate/Deactivate and Delete. + Device offers an empty Audio Effect Rack.
- A rack shows a title strip (power, Macros and Chains toggles, delete), then its sections:
  - Eight macro knobs (0–127), showing their mappings in a tooltip. Right-clicking a knob renames it or removes a mapping.
  - The chain list. Each row has an activator, solo, name, volume and pan; double-click renames a chain, right-click deletes it, and + adds one.
  - The selected chain's devices, with a + menu of audio effects.
- Right-clicking a native control inside a rack offers "Map … to" any macro, or "Remove Mapping". Mapped controls draw in the macro colour and ignore direct edits.
- The automation editor lists chain devices' controls.

## Signal flow

- Each chain is a serial path from the rack input through its devices to the chain's mixer.
- A chain with no devices passes the rack input to its mixer.
- Audible chain mixers connect to the rack output, where the chains sum.
- A muted chain is disconnected. When any chain is soloed, only soloed chains connect.
- A rack without chains passes its input unchanged.
- A deactivated chain device is bypassed, and a deactivated rack is bypassed as a whole.

## Macros

- A mapped control's value is `minimum + macro × (maximum − minimum)`, interpolated in the control's knob position. That is the same scale its editor and automation use.
- The value is written into the device settings whenever the macro or a mapping changes, in the same undo step.
- When a macro is mapped for the first time, it takes the control's current position within the range, so mapping does not move the control.
- A control follows only one macro. While mapped, it cannot be edited directly or given automation points.
- Controls that already have automation cannot be mapped. Only native device controls can be mapped.

## Projects

- A rack is stored as an engine rack type marked as Halftrack's (`halftrackRack`), referenced by exactly one rack on an audio track.
- Each chain ends in a Halftrack mixer that carries the chain's name, mute and solo.
- The eight macros are stored in a `HALFTRACKMACROS` child, and each device keeps its own mappings as knob positions.
- Loading rejects the file and keeps the open project when it finds any of these:
  - foreign or shared rack types;
  - instruments or MIDI effects in a rack;
  - nested racks;
  - devices after the last chain mixer;
  - malformed macros or mappings;
  - mapped controls that also have automation.
- Loading rebuilds the rack connections from the chain order, outside history.
- Duplicating a track gives the copy its own rack, chains and mappings. Removing a rack or its track also removes its rack type.

## Evidence

`tests/RackTests.cpp` renders a generated stereo tone through a Utility at −6 dB and checks:

- grouping keeps the sound;
- an empty chain adds the dry input, so the chains sum;
- chain volume, hard pan, mute, solo and two solos;
- MIDI effects and instruments are rejected;
- a neutral EQ in a chain keeps the sum;
- Macro 1, mapped to the EQ output over ±24 dB, sets −12 dB at a quarter, and the render follows it;
- mapped controls are locked against direct edits and automation;
- reordering, bypass and removal inside a chain;
- an identical render after reopening, with no history;
- independent racks on duplicated tracks;
- rack types are removed with their track or rack;
- chain deletion and undo, and ungrouping and undo;
- an empty rack passes its input;
- corrupted macro values, foreign racks and bad mappings are rejected;
- frozen-track locks.

The UI suite checks Shift-click selection, Cmd/Ctrl+G grouping, the rack panel's macros and chain devices, + chain, chain volume and activator rows, the mapped-control colour, a macro knob driving the mapped EQ output, a snapshot and undo.

## Not implemented

- Instrument, MIDI Effect and Drum Racks with chains.
- Key, velocity and chain-select zones.
- Nested racks (the engine does not allow them).
- Macro automation, MIDI mapping of macros and variations.
- Mapping third-party plug-in or Utility parameters.
- Rack presets.
- Any comparison with reference output.
