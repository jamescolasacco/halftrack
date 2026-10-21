# MIDI mapping contract

Halftrack follows the reference's MIDI Map Mode, described in the manual's [MIDI and Key Remote Control](https://www.ableton.com/en/live-manual/12/midi-and-key-remote-control/) chapter, consulted 2026-10-21:

- the MIDI switch enters map mode and highlights assignable controls in blue;
- clicking a control and then sending a MIDI message assigns it;
- the Mapping Browser lists the mappings with their Min and Max;
- input ports act as remotes when their Remote switch is on.

## Model

A mapping is a controller (a CC number on a channel) and a target, with a minimum and maximum in the target's own units. The targets are:

- a track's volume or pan (`mixer:<track>:volume` or `:pan`);
- the master volume (`master`);
- a native device or rack control (`device:<plugin>:<index>`).

Each controller drives at most one target, and each target has at most one controller. Mapping a controller or target again moves the mapping. The controller's 0–127 moves the control's fader or knob position (the same taper as automation) linearly from minimum to maximum. An inverted range turns the other way, and switches and choices take the nearest step.

Mappings are saved in the project (`HALFTRACKMIDIMAP`). Loading rejects these and keeps the open project:

- channels outside 1–16, controllers outside 0–127, or non-numbers;
- unknown target kinds;
- a target or controller mapped twice.

A deleted device's mappings drop out of `Snapshot::midiMappings` and do nothing.

## Interface

- **Learning.** `setMidiLearnTarget(target)` starts learning. The next controller message maps to the target and moves nothing, and learning continues until the target is cleared. Opening a project ends learning.
- **Ranges and removal.** `setMidiMappingRange` sets the ends within the control's range, and `removeMidiMapping` removes a mapping; both are undoable.
- **Controller messages.** `handleMidiControl(channel, controller, value)` applies a controller message as it arrives. A controller's moves are one gesture, so one undo step, until it rests for half a second. Moves pass through the ordinary setters, so they override playing automation and are recorded with Automation Arm.
- **Ranges by target.** `midiTargetRange(target)` reports a target's range.
- **MIDI inputs.** A `MidiRemote` consumer is attached to every MIDI input instance of the current Edit, and reattached every second as devices come and go. It copies controller changes into a fixed ring under a spin lock on the MIDI thread and applies them on the message thread.

## Evidence

`tests/MidiMappingTests.cpp` (`midi-mapping-acceptance`) checks:

- **Learning.** Learning creates a full-range mapping without moving the control, and a controller or target can be remapped.
- **Mixer moves.** Volume follows the fader taper from silence to +6 dB, and pan reaches both sides. Other channels and controllers do nothing, and malformed messages are refused.
- **Ranges.** A range limits volume to −24–0 dB, an inverted range reverses pan, and out-of-range ends are refused.
- **Undo.** A five-message burst is one undo step.
- **Device controls.** A Channel EQ Mid control moves from −12 to +12 dB, its HP switch switches, and the master follows its controller.
- **Automation.** A controller overrides playing automation.
- **Removal and projects.** Removal works, mappings reopen with the project, four damaged projects are rejected, and a deleted device's mappings are inert.
- **Real input.** A virtual MIDI controller, created through the engine's device manager, is learned and then drives a fader through the real input path.

## Not implemented

- The Map Mode user interface (next commit).
- Note and key mappings: buttons, clip launch, transport.
- Per-input Remote switches (every input acts as a remote).
- Takeover modes (Pick-Up, Value Scaling) and relative encoders.
- MIDI output feedback.
- The reference's inverted switch ranges (values outside the range turn a switch on). Halftrack switches take the nearest step along the range instead.
