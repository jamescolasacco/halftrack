# Device preset and default set contract

Halftrack's own devices save presets into a user library that the browser lists, and the open project can become the default set for new projects. This follows the reference's device presets and default set ([Managing Files and Sets](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/), consulted 2026-10-20). The file formats are Halftrack's own.

## Interface

- `saveDevicePreset(pluginId, file, name, overwrite)` writes a native device's or the Utility's settings, inside or outside a rack, a plug-in's identity and state, or a rack whole (see [Rack presets](#rack-presets-2026-10-25)).
  - The format is `HALFTRACKPRESET` version 1: the device type, the native kind and one attribute per control key.
  - An existing file is replaced only with `overwrite`. A plain device's sample choices and automation are not included.
- `readDevicePreset(file, preset)` validates a preset. It rejects other versions, unknown kinds, rack kinds, and non-numeric or out-of-range values. Controls missing from the file keep their defaults, so presets survive appended controls.
- `loadDevicePreset(trackIndex, file, pluginId)` adds the preset's device in one undo step. An instrument preset replaces the instrument, and instrument and MIDI-effect presets need a MIDI track. If the settings fail after the device is added, the device is taken back out.
- `saveDefaultSet(file)` writes the open project as a template, with media by absolute path, without changing the project.
- `newProjectFromDefaultSet(file)` opens a template as a new, untitled, unchanged project with no history.

## Interface in the app

- The user library is `~/Music/Halftrack/User Library`. Its `Presets/<device>/<name>.halftrackpreset` files hold presets, and `Default Set.halftrack` holds the default set.
- A device's menu offers Save Preset..., which asks for a name and confirms before replacing a preset.
- The browser lists each device's presets under it; search matches device or preset names. Choosing a preset loads it onto the selected track.
- File > Save as Default Set saves the template. New Project and Cmd/Ctrl+N start from it when it exists.
- Without a saved default set, the app starts, and New Project and Cmd/Ctrl+N make new projects, from a built-in one like the reference's built-in default Set, as read from its bundle on 2026-10-22 (`Session::newBuiltInDefaultProject`). It holds two MIDI tracks without instruments, two audio tracks, a return with Halftrack's reverb and one with its delay, eight scenes, and 120 BPM in 4/4. With automatic names these show 1-MIDI, 2-MIDI, 3-Audio, 4-Audio, A-Room Reverb and B-Stereo Delay; the reference's devices are named Reverb and Delay. A project or audio files named at launch replace it. `Session::newProject`, which the suites build on, keeps two audio tracks.

## Evidence

`tests/PresetTests.cpp` checks:

- a Saturator preset holding exact settings, the overwrite refusal, and name and device rejection;
- loading onto another track with single-step undo;
- a Utility round trip;
- a Poly Synth preset needing a MIDI track and then replacing the instrument with undo;
- a MIDI-effect preset needing a MIDI track;
- five damaged presets rejected;
- a default set restoring tempo, tracks and mixer as an untitled, unchanged project without history.

The UI suite saves a preset through its dialog, finds it under its device in the browser, loads it, and starts New from a saved default set, then without one from the built-in default set. The track-order suite checks the built-in default project's tracks, devices, names, scenes, tempo and meter, and that it is untitled, unchanged and without history.

## Not implemented

- Sample-carrying presets of a lone Sampler or Drum Sampler (a rack's devices keep their samples).
- Hot-swapping presets.
- Preset previews.
- Track and clip templates.

## Plug-in presets

A plug-in's preset keeps its identity (its `PluginDescription`) and the state it saves (`getStateInformation`, as base64) in the same `.halftrackpreset` format, with `device="plugin"`.

- **Saving.** The device menu's Save Preset works for any loaded plug-in, into `Presets/<plug-in name>` in the user library. It refuses to replace a file unless asked, as for native presets.
- **Loading.** The preset adds the plug-in and restores its state as one undo step; a plug-in that cannot be found takes the addition back.
- **Refusals.** A preset missing its plug-in or state is refused without changing the project.
- **The browser.** The Plug-ins list shows each plug-in's presets under it, and they can be put in collections.

`tests/PluginPresetTests.cpp` saves the controlled VST3 gain plug-in at a quarter gain as a preset. It then checks that loading the preset onto a track playing 0.5 renders 0.125, that one undo renders dry again, and that overwriting without asking and a preset without its plug-in are refused. The UI suite lists a preset under a stand-in plug-in. Plug-ins' own preset formats (`.vstpreset`, `.aupreset`) and program lists are not read.

## Hot-Swap (2026-10-24)

The reference's manual: Q or a device's Hot-Swap Presets button links the device to the browser, which selects it in its folder; Return or a double-click loads the chosen preset in its place, from its own folder or another device's, of the same device type; loading a device's folder loads its defaults. Q, Escape, the X in the Hot-Swap bar above the browser's list, the device's own button and leaving the browser end it. With no device selected, Q takes an audio track's first audio effect or a MIDI track's instrument ([Working with Instruments and Effects](https://www.ableton.com/en/live-manual/12/working-with-instruments-and-effects/), section 23.2.4, consulted 2026-10-24).

- **The engine.** `Session::hotSwapDevice` replaces a chain's device (a track's instrument included) in its place, in one undo step named Hot-swap, by a preset, a native device's defaults, the Utility or a plug-in. Only the same type takes the place: instrument for instrument, audio effect for audio effect, MIDI effect for MIDI effect. A preset of the device's own kind loads into it and keeps it. A swap that fails part way is taken back whole.
- **Linking.** Q, or the arrows button in a device's title bar, links the selected device, or with none an audio track's first audio effect or a MIDI track's instrument. The browser shows the device's category (Plug-Ins for a plug-in) with its row selected, and a bar above the list names it.
- **Swapping.** Return or a double-click on a device, preset or plug-in puts it in the device's place, and the link follows the new device, so swapping can go on.
- **Ending.** Q, Escape, the bar's X, the device's button again, or a category the device's kin are not in (Plug-Ins and the collections keep it).
- **Samples.** As the manual's section 23.2.4.1 has it, the Sampler's waveform and a Drum Sampler's selected pad have a Hot-Swap Sample button, always shown, which opens the sample's folder in the browser with it selected. Return or a double-click on a sound loads it into the Sampler or pad, folders still opening; the button again, the bar's X, Escape, Q or a category other than Samples, the places and the collections end it.

`tests/PresetTests.cpp` checks a preset of the device's own kind loading into it, another audio effect and an audio effect's preset taking a device's place in the chain's order, one undo restoring it, an instrument and a MIDI effect swapping for their own types, and the refusal of other types and unknown chains or devices. The UI suite links a Saturator with Q, swaps an EQ Three in with Return, ends it with Escape and undoes the swap, links and unlinks a device from its button and the bar's X, links a MIDI track's instrument, and ends a link by choosing Samples.

The UI suite also opens the Sampler's sample from its button, loads another sound into it with Return and ends with Escape, and does the same for a Drum Sampler pad, ending with the button.

Not yet: Drum Rack pads' samples, devices inside racks, showing the sample buttons only on hover, and the reference's option to replace a device as soon as a preset is selected.

## Rack presets (2026-10-25)

The reference's manual, consulted 2026-10-25: a device's Save Preset button, in its title bar, saves its current settings as a preset in the User Library's Presets folder, under its device ([Working with Instruments and Effects](https://www.ableton.com/en/live-manual/12/working-with-instruments-and-effects/), section 23.2.5; the section does not name racks, which are devices there). Section 23.2.3 lists `.adg` among Live's device files, and whole chains can be saved as presets ([Instrument, Drum and Effect Racks](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/), section 25.4). What the reference's rack preset holds was not read. Halftrack's format is its own.

- **Saving.** A rack's device menu now offers Save Preset..., as other devices' do, into `Presets/<rack name>` (Audio Effect Rack, Instrument Rack, Drum Rack or MIDI Effect Rack). The preset (`device="rack"`) holds the rack device's tree: its chains with their settings and devices, nested racks, a Drum Rack's returns, its macro mappings, macro values, names and how many show. Automation is left out. Devices' samples are kept by their paths.
- **Reading.** A rack preset is checked as a project's racks are before anything changes: its kind, nesting, chain and device settings, the devices each chain accepts and in what order, unique identities, mappings and samples. A missing sample keeps its reference and plays nothing, as in a project.
- **Loading.** Choosing it adds a rack of its kind (an Instrument or Drum Rack as the track's instrument, needing a MIDI track, as a MIDI Effect Rack does) and gives it the preset's contents, in one undo step. Its chains and devices take new identities, and the macro mappings follow them, those to the rack's chain selector included.
- **Hot-Swap.** A rack preset hot-swapped onto a rack of its kind fills that rack in place, keeping the rack and the automation of its own macros; onto another device of its type it takes that device's place.
- **The browser** lists each rack's presets under it, as other devices' presets.

`tests/RackTests.cpp` (`presetTests`) saves an Audio Effect Rack of two chains, an EQ inside one, a named macro mapped to the EQ's output and turned to 32. It checks that the preset reads back as a rack preset, that replacing it needs asking, and that loading it onto another track is one undo step whose chains and devices have new identities and the same settings. The copy's mapping follows its own EQ, its macros and names are the preset's, and the copy renders with 0.0 difference from the original. Turning the copy's macro leaves the original's EQ. A preset saved from a rack with macro automation holds none. Hot-swapped onto the rack it came from, it fills it with new chains and keeps the rack's automation, one step; onto a Saturator, it takes the Saturator's place. An Instrument Rack preset needs a MIDI track and becomes the instrument. A kind that does not match, a missing rack, a chain at 99 dB, a repeated chain identity and an instrument in an audio effect chain are each refused without changing the project. The UI suite finds a rack preset under the Audio Effect Rack in the browser and loads it.
