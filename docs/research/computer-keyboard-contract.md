# Computer MIDI keyboard contract

Halftrack's computer MIDI keyboard follows the reference's Computer MIDI Keyboard: with it on, the computer's keys play notes into a MIDI input named Computer Keyboard.

## The input

- **Listing.** Computer Keyboard is always listed among the MIDI inputs, and tracks choose it like any other. All MIDI Ins includes it, as the reference's All Ins does.
- **Identity.** It is a virtual input of the engine, made when the Session starts. Test Sessions, which have no audio devices, make it when the keyboard is first turned on (see [open questions](../open-questions.md#a-second-virtual-midi-input-and-test-recordings)). Its identity comes from its name, so a project that uses it opens with it.
- **Monitoring and recording.** Notes from the keys are monitored and recorded through the track's input like a controller's.
- **Off.** Turning the keyboard off ends every note it holds. While it is off, its notes are refused.

## The keys

- **Turning it on.** M, or the KEYS switch beside MIDI in the control bar, turns it on and off. Its tooltip lists the keys.
- **Notes.** A S D F G H J K L ; ' play the white keys from C, and W E T Y U O P the black keys between them, over an octave and a half. The A key starts on C3 (note 60).
- **Octave.** Z and X move the octave down and up, so the A key plays from C-2 (note 0) to C8 (note 120). At C8, keys above G8 play nothing.
- **Velocity.** C and V move the velocity down and up from 100 in steps of 20, between 1 and 127. A change of octave or velocity is shown in the status bar.
- **Releasing.** A held key repeating plays nothing more. Releasing a key ends the note it began, even if the octave changed meanwhile. Releases are found from the key state when it changes, and from the window timer.
- **Other shortcuts.** While the keyboard is on, these keys play instead of their shortcuts, as in the reference: A for Automation Mode, Z and X for zoom, T for audition. Command, Option and Control combinations and typing in text fields are left alone.

## Auto-Arm

As in the reference, selecting a MIDI track arms it while Exclusive Arm is on and no track is armed by hand, so the keyboard or a controller plays it at once.

- **Its input.** A track with no input is given All MIDI Ins with Auto monitoring, unless another track has All MIDI Ins.
- **Moving the arm.** Selecting another track moves the arm, and takes back the input auto-arm gave. Selecting an audio track only releases it.
- **Showing it.** An auto-armed track's arm button is a lighter red, and its tooltip says why it is armed.
- **By hand.** Arming, disarming or choosing an input by hand ends auto-arm for that track; the track keeps what it has. Arming another track by hand releases the auto-armed one.
- **Limits.** Auto-arm is not an undo step and is not saved. It does not change while recording or exporting. A Session that has not yet scanned its MIDI devices scans them before giving All MIDI Ins.

`tests/AutoArmTests.cpp` checks that:

- selecting a MIDI track gives it All MIDI Ins, Auto monitoring and the arm, and the computer keyboard then plays it;
- selecting another MIDI track moves the arm and the input, and undo skips auto-arm;
- selecting an audio track releases it;
- a track's own input is kept while armed and after release;
- disarming by hand ends auto-arm, and arming another track by hand keeps it off;
- All MIDI Ins held by another track is not taken;
- nothing arms with Exclusive Arm off;
- an auto-armed track records, and the arm moves again afterwards.

The UI suite checks that selecting a MIDI track auto-arms it with All MIDI Ins and a marked arm button, and that selecting an audio track releases it.

## Evidence

`tests/ComputerKeyboardTests.cpp` checks the key layout and the octave and velocity keys with their limits. It also checks that a repeating held key plays nothing more and that a key held across an octave change ends its own note.

Then, through the synthetic device, it checks:

- Computer Keyboard is listed once.
- Notes are refused while the keyboard is off, and out-of-range notes and velocities are refused.
- A monitored track hears a key and goes silent on release, and All MIDI Ins hears it too.
- An armed track records G3 at velocity 0.8 for about a beat.
- Turning the keyboard off while a key is held ends the note.

The UI suite checks that the keyboard starts off with KEYS in the control bar. It then checks these behaviours in turn:

- M turns the keyboard on and lights KEYS.
- A plays a note rather than switching Automation Mode.
- The key's release ends the note.
- X and Z move the octave, and the status bar names it.
- The KEYS switch turns it off and ends a held note, after which A switches Automation Mode again.

## Not yet covered

- Remembering the switch between sessions.
- Live's default of having it on in a new installation.
- Keyboard layouts other than US QWERTY. Keys are taken by character, so other layouts play their own characters' positions.
- Key presses timed more finely than the UI's event delivery.
