# Device preset and default set contract

Halftrack's own devices save presets into a user library that the browser lists, and the open project can become the default set for new projects. This follows the reference's device presets and default set ([Managing Files and Sets](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/), consulted 2026-10-20). The file formats are Halftrack's own.

## Interface

- `saveDevicePreset(pluginId, file, name, overwrite)` writes a native device's or the Utility's settings, inside or outside a rack.
  - The format is `HALFTRACKPRESET` version 1: the device type, the native kind and one attribute per control key.
  - An existing file is replaced only with `overwrite`. Racks, sample choices and automation are not included.
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

- Rack and plug-in presets.
- Sample-carrying instrument presets.
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
