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
- the master volume (`master`);
- a native device or rack control (`device:<plugin>:<index>`).

Buttons are targets too (see the [key map contract](key-map-contract.md)). Each controller or note drives at most one target, and each target has at most one controller or note. A note and a controller with the same number are different. Mapping a controller, note or target again moves the mapping. The controller's 0–127 moves the control's fader or knob position (the same taper as automation) linearly from minimum to maximum. An inverted range turns the other way, and switches and choices take the nearest step.

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
- **Mixer moves.** Volume follows the fader taper from silence to +6 dB, and pan reaches both sides. Other channels and controllers do nothing, and malformed messages are refused.
- **Ranges.** A range limits volume to −24–0 dB, an inverted range reverses pan, and out-of-range ends are refused.
- **Undo.** A five-message burst is one undo step.
- **Device controls.** A Channel EQ Mid control moves from −12 to +12 dB, its HP switch switches, and the master follows its controller.
- **Automation.** A controller overrides playing automation.
- **Removal and projects.** Removal works, mappings reopen with the project, five damaged projects are rejected (a bad note flag among them), and a deleted device's mappings are inert.
- **Notes.** A note-off while learning maps nothing; C3 maps to the activator as a note, as one undo step, beside controller 60 on the same channel. Its note-on toggles the activator and its note-off does nothing; other channels and notes do nothing; malformed notes are refused. A note replaces a target's controller. A note on the volume, with a −12–0 dB range, switches it between the ends. Through the synthetic device, a note launches a Gate clip and its note-off stops it, and a controller does the same rising above 63 and falling to 63. Note mappings reopen as notes, and a note mapped twice is refused.
- **Real input.** A virtual MIDI controller, created through the engine's device manager, is learned and then drives a fader through the real input path. A note played into it while learning maps the activator without reaching the armed MIDI track it feeds; the mapped note then switches the activator without reaching the track, while an unmapped note still does, and Capture MIDI keeps only the unmapped note.

## Not implemented

- Note ranges: a slot played chromatically from a range of keys, and a control given evenly spaced values across a range.
- Pitch bend and 14-bit controllers.
- Per-input Remote switches (every input other than the computer keyboard acts as a remote).
- Takeover modes (Pick-Up, Value Scaling) and relative encoders.
- MIDI output feedback.
- The reference's inverted switch ranges (values outside the range turn a switch on). A controller mapped to a switch control, such as a device's HP switch, takes the nearest step along the range instead.
- The reference's confirmation when a message already mapped elsewhere is learned again; Halftrack moves the mapping without asking.
