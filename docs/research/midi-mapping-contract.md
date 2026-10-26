# MIDI mapping contract

Halftrack follows the reference's MIDI Map Mode, described in the manual's [MIDI and Key Remote Control](https://www.ableton.com/en/live-manual/12/midi-and-key-remote-control/) chapter, consulted 2026-10-21:

- the MIDI switch enters map mode and highlights assignable controls in blue;
- clicking a control and then sending a MIDI message assigns it;
- the Mapping Browser lists the mappings with their Min and Max;
- input ports act as remotes when their Remote switch is on;
- a note mapped to a Session slot follows the clip's Launch Mode with its note-on and note-off, a note toggles a switch, and a single note toggles a continuous control between Min and Max;
- a static reading of the reference's code (findings in the ignored `artifacts/reverse-engineering/notemap/`) adds that a note on a stepped control steps to the next value and wraps, a note on a continuous control goes to Min from at or above the midpoint of Min..Max and to Max below it, whatever its velocity, and an absolute controller sets a switch on at 64 and above and off below rather than toggling it;
- an absolute controller acts on a slot as a note-on at 64 and above and a note-off at 63 and below;
- keys that are part of a remote assignment stop acting as input for MIDI tracks.

## Model

A mapping is a controller (a CC number on a channel) or a note (a note number on a channel) and a target, with a minimum and maximum in the target's own units. `MidiMapping::note` marks a note's mapping, whose `controller` is its note number. The targets are:

- a track's volume or pan (`mixer:<track>:volume` or `:pan`);
- a track's send to a return (`send:<track>:<return>`), from Off to 0 dB along the send law;
- the master volume (`master`);
- a native device or rack control (`device:<plugin>:<index>`).

Buttons are targets too (see the [key map contract](key-map-contract.md)). Each controller or note drives at most one target, and each target has at most one controller or note. A note and a controller with the same number are different. Mapping a controller, note or target again moves the mapping. The controller's 0–127 moves the control's fader or knob position (the same [law](mixer-laws-contract.md) as automation) linearly from minimum to maximum, so with the full range CC 64 is −13.84 dB, as in the reference. An inverted range turns the other way, and switches and choices take the nearest step.

Mappings are saved in the project (`HALFTRACKMIDIMAP`). A note's mapping carries `note="1"`, and a controller's has no flag, so projects with only controller mappings are unchanged. Loading rejects these and keeps the open project:

- channels outside 1–16, controllers or notes outside 0–127, a note flag other than 0 or 1, or non-numbers;
- unknown target kinds;
- a target, controller or note mapped twice.

A deleted device's mappings drop out of `Snapshot::midiMappings` and do nothing.

## Interface

- **Learning.** `setMidiLearnTarget(target)` starts learning. The next controller message maps to the target and moves nothing, and learning continues until the target is cleared. Opening a project ends learning.
- **Ranges and removal.** `setMidiMappingRange` sets the ends within the control's range, and `removeMidiMapping` removes a mapping; both are undoable.
- **Controller messages.** `handleMidiControl(channel, controller, value)` applies a controller message as it arrives. A controller's moves are one gesture, so one undo step, until it rests for half a second. Moves pass through the ordinary setters, so they override playing automation and are recorded with Automation Arm. A controller on a switch (an activator, solo, arm, device switch, Record, Loop or Metronome) sets it on at 64 and above and off below; on any other button, it presses as it rises above 63, and a clip slot is released as it falls to 63 or below.
- **Note messages.** `handleMidiNote(channel, note, velocity)` applies a note as it arrives; velocity 0 is its note-off. While learning, a note-on maps the note (a note-off maps nothing). A mapped note presses a clip slot with its note-on, carrying its velocity for the clip's Velocity amount (see the [launch modes contract](launch-modes-contract.md)), and releases it with its note-off, so Gate and Repeat clips play while it is held and Toggle clips start and stop; any other button is pressed by the note-on alone, so a switch toggles. On each note-on, whatever its velocity, a control moves as the reference's do: a two-state switch toggles, a choice or whole-number control steps to the next value and wraps from the mapping's maximum back to its minimum, and anything else goes to the minimum from at or above the midpoint of the mapping's range (in fader or knob position) and to the maximum below it.
- **Ranges by target.** `midiTargetRange(target)` reports a target's range.
- **MIDI inputs.** A `MidiRemote` serves every MIDI input of the current Edit except the computer keyboard and the inputs that gather others (All MIDI Ins), and is reattached every second as devices come and go. Through the engine hook below it sees each input's messages on the MIDI thread before the input's tracks do. It takes the mapped notes and controllers, and every note and controller while a mapping is being learned, copies them into a fixed ring under a spin lock, and applies them on the message thread. What it takes is not track input: an armed or monitored track does not play or record it, and Capture MIDI does not keep it. Every mapping change, undo and redo republishes the mapped set to the MIDI thread as atomic flags.

## Engine hook

`cmake/PatchRemoteMidi.cmake` gives the engine's MIDI input device a remote control (see [dependencies](../dependencies.md)). `MidiInputDevice::sendMessageToInstances` first offers each message to it, under a spin lock that `setRemoteControl` also takes, so a cleared remote is never called afterwards. A taken message goes to no instance (no track input, recording or consumer). A note-off goes where its note-on went, so a note held while a mapping is learned or removed cannot stick on a track or be lost from it. A physical input does not pass what its remote took on to the virtual inputs that gather it, so All MIDI Ins does not play a mapped note either, while the computer keyboard's notes, which reach All MIDI Ins directly, are unaffected.

## Map Mode

The MIDI switch in the control bar (Cmd/Ctrl+M) enters Map Mode, which:

- dims the working area and highlights every visible mappable control in blue with its channel and controller or note (`1/7`, `1/C3`): track volume and pan, the master faders, inline device knobs and menus, and the buttons;
- outlines the control being learned in orange; clicking a control learns it, and clicking elsewhere stops learning;
- replaces the browser with the mapping list, where each mapping shows its control path, its channel and CC or note (`Ch 1  CC 7`, `Ch 1  Note C3`, with C3 as note 60, as the reference names notes), Min and Max sliders and a remove button.

Leaving Map Mode ends learning. Controls take part by carrying a `midiMapTarget` property with their target key.

## Evidence

The UI suite checks the switch and Cmd/Ctrl+M, the overlay's targets, learning a fader by clicking it in the overlay, the list's Min, a note mapped to a solo switch named "Ch 1  Note C3" beside "Ch 1  CC 7", the remove button, and that leaving the mode ends learning and restores the browser.

`tests/MidiMappingTests.cpp` (`midi-mapping-acceptance`) checks:

- **Learning.** Learning creates a full-range mapping without moving the control, and a controller or target can be remapped.
- **Mixer moves.** Volume follows the volume law from silence to +6 dB, and pan reaches both sides. Other channels and controllers do nothing, and malformed messages are refused.
- **Ranges.** A range limits volume to −24–0 dB, an inverted range reverses pan, and out-of-range ends are refused.
- **Undo.** A five-message burst is one undo step.
- **Device controls.** A Channel EQ Mid control moves from −12 to +12 dB, its HP switch switches, and the master follows its controller.
- **Automation.** A controller overrides playing automation.
- **Sends.** A send's range is Off to 0 dB, and a return has none to itself. CC 64 gives −19.84 dB along the send law, 127 gives 0 dB, and 0 gives Off. The send's MIDI and key mappings reopen with the project and still move it.
- **Removal and projects.** Removal works, mappings reopen with the project, five damaged projects are rejected (a bad note flag among them), and a deleted device's mappings are inert.
- **Notes.** A note-off while learning maps nothing; C3 maps to the activator as a note, as one undo step, beside controller 60 on the same channel. Its note-on toggles the activator and its note-off does nothing; other channels and notes do nothing; malformed notes are refused. A note replaces a target's controller. A note on the volume, with a −12–0 dB range, switches it between the ends. Through the synthetic device, a note launches a Gate clip and its note-off stops it, and a controller does the same rising above 63 and falling to 63. Note mappings reopen as notes, and a note mapped twice is refused.
- **Real input.** A virtual MIDI controller, created through the engine's device manager, is learned and then drives a fader through the real input path. A note played into it while learning maps the activator without reaching the armed MIDI track it feeds; the mapped note then switches the activator without reaching the track, while an unmapped note still does, and Capture MIDI keeps only the unmapped note.

## Takeover Mode (2026-10-26)

The reference's Takeover Mode preference, after a static reading of its code (Live 12.2.1, arm64 slice, sha256 b37cb738442bba7787457fe751b0e3f9fbdabc662d8d491d1d9aa46b120bd3fc; binary-assisted research, raw notes in the ignored `artifacts/reverse-engineering/takeover/`). It decides what a controller does when its position and its control's value differ.

- **The setting.** `Preferences::takeoverMode` is None, Pick-Up or Value Scaling, None by default as the reference's is. It is one setting for every input and mapping, kept with the preferences, not the project. The Link, Tempo & MIDI page offers it below the control surfaces.
- **History.** Each input remembers each controller's last value, by input name, channel and controller, before anything acts on it. This happens while learning too. It is kept while the application runs and is not reset by opening a project, as the reference's inputs keep theirs. `handleMidiControl (channel, controller, value, port)` takes the input's name.
- **Which controls.** Continuous controls take it over: mixer and send levels, pans, the master, native device controls (whole-number ones included) and plug-in parameters. Choices, switches and buttons always follow the controller, as the reference's do.
- **The arithmetic** is `src/RemoteMapping.cpp`, ported from the research model in single precision, built without floating-point contraction so only the reference's fused multiply-adds fuse. It works in the control's fader or knob positions, as Halftrack's mappings do:
  - a controller value v gives `upper·t + lower·(1−t)` (fused) with `t = v/127`, or `t = (127−v)/127` for a reversed range;
  - **None** jumps the control to the controller;
  - **in step:** with Pick-Up or Value Scaling, a controller whose previous value lay within half a step (1/128 of the range) of the control's value follows it;
  - **Pick-Up** otherwise takes the controller only when its move from its previous value passed the control's value, or came within two steps of it; until then the control stays;
  - **Value Scaling** otherwise follows within two steps; a repeated value leaves the control; moving up, the control moves `(upper − value)·(new − previous)/(upper − previous)`, and moving down `(value − lower)·(previous − new)/(previous − lower)`, so it reaches the end when the controller does;
  - with no previous value from that input, Pick-Up and Value Scaling leave the control as it is;
  - a control outside the mapping's range is first taken to its nearer end.
- **Waiting.** While Pick-Up waits, `Snapshot::pickUp` names the control with its value and the controller's, and the status bar shows `<control> waiting for pick-up: E  <<  R`, or `R  >>  E` when the controller is below, the lower value first, as the reference's status bar arranges them. A move that picks it up clears it. A waiting message changes nothing and makes no undo step.

Differences from the reference:
- The reference's inputs remember every controller message. Halftrack's remember only those that reach the Session: mapped controllers, and every controller while one is being learned.
- The reference works in each parameter's own internal scale, which the reading did not identify for non-linear parameters such as Volume. Halftrack works in fader and knob positions.
- The status bar's English wording is Halftrack's; the reference's is in an encrypted resource.

Evidence (`tests/MidiMappingTests.cpp`):
- 17 cases of the arithmetic give exactly the research model's single-precision results: None, Pick-Up waiting, crossing, within two steps and from above, no previous value, a controller in step, Value Scaling up, down, a repeated value, within two steps, a narrower range, reversed, to the top and the bottom, and a control outside the range.
- In a Session, a mapped volume at −12 dB with Pick-Up stays when the controller moves below it, reporting the wait with no undo step; the controller passing it takes it over and ends the wait, and the fader then follows. Another input's first message of the same controller waits. Value Scaling moves the fader down in proportion, and None jumps it. A choice of an Eight-band EQ jumps even with Pick-Up.
- `tests/PreferencesTests.cpp`: the setting is saved and read back, and a value beyond Value Scaling makes the file unreadable.
- The UI suite: the MIDI page's chooser sets it, and the status bar shows the wait (`1-Audio / Volume waiting for pick-up: -52.12  >>  -6.00`) and clears it.

## Map modes (2026-10-26)

After the same reading, each controller mapping has one of the reference's ten map modes, `MidiMapping::mode`, kept with the project (`mode`, left out for Absolute).

| Mode | Steps up | Steps down |
| --- | --- | --- |
| Absolute | – | – |
| Relative (Signed Bit) | 1–64, sized through 1, 2, 3, 4, 5, 8, 10, 20, 50 (20 past it; 0 is +20) | 65–127, from 65 |
| Relative (BinOffset) | 64–127, from 65 through 1, 2, 3, 4, 5, 8, 20, 50 (50 past it; 64 is +50) | 0–63, from 63 |
| Relative (2's Comp.) | 0–65, from 1 (0 is +50) | 66–127, from 127 |
| Relative (Signed Bit 2) | 65–127, from 65 | 0–64, from 1 (0 is −50) |
| Absolute (14bit) | – | – |
| Relative (lin. Signed Bit) | v below 65: v | 64 − v |
| Relative (lin. BinOffset) | v − 64 | |
| Relative (lin. 2's Comp.) | v below 65: v | v − 128 |
| Relative (lin. Signed Bit 2) | 65 and above: v − 64 | −v |

- **Relative moves.** A relative message moves a continuous control a 256th of the mapping's range (in fader or knob position) per step, from its value now, within the range, the other way for a reversed range. A stepped device control (a choice or a whole number) moves one step a message in its own units, rounded half away from zero, and a two-state switch turns on as the encoder turns up and off as it turns down. Takeover does not apply.
- **Absolute (14bit)** pairs the mapped controller (0 to 95) with the one 32 above it as its fine half. The two halves wait for each other, in either order, and then act as one value from 0 to 127 in steps of 1/129, `((coarse × 128) | fine) / 129`. Takeover applies, with a previous value made the same way from both halves' previous values; a half not yet known counts as the reference's −129, as its arithmetic does. The fine half's controller stops acting as track input too.
- **Buttons.** A controller switches or presses a button as the reference's table for each mode says: above 63 in the absolute modes; below 65 in Signed Bit, lin. Signed Bit and lin. BinOffset, as the reference's table has it; 64 and above in BinOffset; 64 and below in 2's Comp.; 65 and above in Signed Bit 2; and never in lin. 2's Comp. and lin. Signed Bit 2.
- **Learning** detects the mode, as the reference's does, from the message and the controller's previous value from the same input:
  - the same value twice in a row: 1 gives Signed Bit, 63 BinOffset, 127 2's Comp. and 65 Signed Bit 2; any other repeat, Absolute;
  - a controller 32 from the one the target already has on the channel makes the pair Absolute (14bit), on the lower controller;
  - otherwise Absolute.
  - Learning the target's own controller again changes nothing; with a known previous value and another mode, the mode alone changes, outside the undo history, as the reference's does. Anything else is a new mapping, one undo step, taking both controllers of a 14-bit pair from other mappings.
- **Choosing.** `setMidiMappingMode (target, mode)` sets a controller mapping's mode in one undo step; Absolute (14bit) needs a controller from 0 to 95, and a note's mapping has none. In MIDI Map Mode, the status bar shows the mode of the control being learned when a controller maps it, with the reference's names, as its status bar shows its map mode chooser.
- **Loading** refuses a mode outside the ten, a mode on a note's mapping, a 14-bit pair above controller 95, and a controller taken twice, by a pair's fine half among them.

Evidence (`tests/MidiMappingTests.cpp`):
- The arithmetic gives the research model's results exactly: 25 relative values' steps, ten relative moves (a step on 0–1 and on 0–127, clamped, reversed, stepped, linear), the button table for every mode at 0, 1, 63, 64, 65 and 127, the 14-bit values, and the modes learning detects.
- In a Session: an encoder sending 1 twice is learned as Signed Bit with no extra undo step; its turns move the volume a step up, ten steps for a 7, and a step down for 65 with Pick-Up chosen; 2's Comp. chosen is one undo step and turns 127 down; CC 10 then CC 42 learn pan as a 14-bit pair, the coarse half alone moves nothing and both together place it; an encoder switches solo on turning up and off turning down; the modes reopen, and three damaged projects are refused.
- The UI suite: the status bar shows the learned controller's mode, sets Signed Bit and back, and shows none for a note.

## Not implemented

- Note ranges: a slot played chromatically from a range of keys, and a control given evenly spaced values across a range.
- Pitch bend.
- Per-input Remote switches (every input other than the computer keyboard acts as a remote).
- MIDI output feedback.
- The reference's inverted switch ranges (values outside the range turn a switch on). A controller mapped to a switch control, such as a device's HP switch, takes the nearest step along the range instead.
- The reference's confirmation when a message already mapped elsewhere is learned again; Halftrack moves the mapping without asking.
