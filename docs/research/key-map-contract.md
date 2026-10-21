# Key Map Mode and mapped buttons contract

Halftrack's Key Map Mode follows the reference's: computer keys are mapped to controls, as a controller is in MIDI Map Mode. Both modes can now also map buttons, not only faders and knobs.

## Targets

- **Controls.** A track's volume and pan, the master volume, and device controls keep their targets (see the MIDI mapping contract). This includes hosted plug-in parameters (see the [parameter identity contract](parameter-identity-contract.md)).
- **Buttons.** Each button below is named by the identity of what it belongs to:
  - a track's activator, solo, arm and clip stop (`track:<track>:mute`, `:solo`, `:arm`, `:stop`);
  - a clip slot (`slot:<track>:<scene id>`);
  - a scene's launch (`scene:<scene id>`);
  - a device's switch (`device:<device>:on`);
  - the transport's Play, Stop, Record, Loop and Metronome, and Stop All Clips (`transport:play`, `:stop`, `:record`, `:loop`, `:metronome`, `:stopClips`).
- **Pressing.** A switch toggles. Launches launch, Stop All Clips stops them, and Stop stops. Play starts playback and does nothing while playing. Record starts recording, or stops it while recording.
- **Solo.** A mapped solo toggles that track without releasing others.

## Key Map Mode

- **Turning it on.** KEY in the control bar, or Cmd/Ctrl+K, turns Key Map Mode on. It leaves MIDI Map Mode, and MIDI Map Mode leaves it. The overlay marks every mappable control in orange, including the Session View's clip slots, scene launch buttons, clip stop buttons and Stop All Clips. Each mapped control shows its key.
- **Mapping.** Clicking a control selects it. The next key maps to it, as one undo step, and the control stays selected for another key. Backspace or Delete removes the selected control's mapping.
- **Which keys.** Any printable character other than a space can be mapped, without case. A key drives one control and a control has one key: mapping a key elsewhere moves it.
- **The list.** In place of the browser, the mapping list shows the key mappings, each with its key and a button to remove it.
- **Playing.** Outside Key Map Mode, a mapped key presses its button, or switches its control between the mapping's minimum and maximum, towards whichever end the control is further from. Mapped keys take precedence over the computer MIDI keyboard and over plain-key shortcuts. Keys with Command, Option or Control, and typing in text fields, are left alone.
- **Saving.** Key mappings are saved with the project. A mapping whose control has gone is hidden until undo brings the control back. On load, a key mapping must be one printable character with a known kind of target, unique in key and in target. Otherwise the project is refused and the open one is kept.

## Controllers and buttons

In MIDI Map Mode, a button maps to a controller like any control. The controller presses the button each time its value rises above 63 from 63 or below. The first value above 63 also presses it. A button's mapping has no range to set.

## Evidence

`tests/KeyMapTests.cpp` checks:

- **Targets.**
  - Which targets are buttons.
  - Only existing targets can be mapped.
  - Learning a key is one undo step, without case.
- **Keys pressing buttons.**
  - Q switches the activator, W solo, E arm and R the EQ's switch.
  - T sends the volume to the end it is further from, then to the other.
  - P plays, and again keeps playing; O stops; L and K switch the loop and metronome.
  - Through the synthetic device, J launches a clip slot, G stops the track's clip and H launches the scene.
- **Key rules.**
  - Mapping a used key elsewhere moves it, and a control keeps one key.
  - Unmapped keys, spaces and unprintable keys are left alone.
  - A mapping can be removed.
- **Saving.**
  - A deleted device's mapping is hidden, and undo brings it back.
  - Every key mapping reopens.
  - A two-letter key is refused, keeping the open project.
- **A controller on a button.** Rising above 63 presses it; staying high does not press again; rising again after falling does. Its range cannot be set.

The UI suite checks these behaviours in turn:

- KEY and Cmd/Ctrl+K switch Key Map Mode.
- The overlay marks a track's activator and selects it on a click; Q maps it, and the list shows it.
- Outside the mode, Q switches the track.
- Backspace removes the mapping.
- In the Session View, the overlay marks clip slots, scenes and Stop All Clips.

## Not yet covered

- Mapping MIDI notes, as the reference does for pads; only controllers are mapped.
- Relative encoders and takeover modes.
- Keys mapped to the computer MIDI keyboard's own keys while it is on, measured against the reference.
- Mapping the tempo, the crossfader, sends and rack macros.
