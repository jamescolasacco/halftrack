# Value controls contract

Halftrack's knobs and faders take the reference's value gestures: a finer drag with Shift, and keys for the control clicked last.

## Evidence

- Live 12 manual, [Live Keyboard Shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), consulted 2026-10-24. Section 42.6, Adjusting Values, gives the same keys on both platforms:
  - the up and down arrow keys decrement and increment;
  - with Shift they move "in Octaves or Fine Adjustments";
  - Shift gives finer resolution when dragging;
  - Delete returns to the default;
  - 0 to 9 type in a value, "." or "," go to the next field (bar, beat, sixteenth), Escape cancels the entry and Enter confirms it.
- Section 42.4 resets a device or plug-in parameter with Delete or a double-click. Section 42.7 gives breakpoint envelopes the same finer resolution with Shift.
- The reference's drag rates and arrow steps were not found by name among its strings (static reading of Live 12.2.1 arm64, 2026-10-24). They are unmeasured.

## Halftrack

- **Fine drag.** While Shift is held, every knob and fader (`ValueSlider`, `src/ValueControls.h`) moves at a tenth of its usual rate, whatever its style. A fader that jumps to the click still jumps there, and then moves finely.
- **The control clicked last** takes the value keys until a click elsewhere in the window (`MainComponent`'s `valueKey`):
  - **Up and Down** step it.
    - A value in semitones moves a semitone, or an octave with Shift: the clip's Transpose, a Drum Rack pad's tune, and device controls in semitones.
    - A value with at most 200 steps moves one step.
    - Any other value moves a hundredth of its travel, or a thousandth with Shift, and at least one step.
  - **Delete or Backspace** returns it to its default. A track's mixer controls reset as double-clicking them does, with the controls linked to them (`Session::resetTrackControl`).
  - **Typing** a digit, a minus sign or a point opens a field over the control showing what is typed. Return, or a click elsewhere, sets the value, read as the control reads its own text box. Escape leaves it.
  - Each change from the keys is one undo step, as a drag is.
  - Keys with Cmd, Ctrl or Option, and any other key, go on to the window.
- **Missing.** The number fields (the Control Bar's tempo, signature and loop, and the clip's positions) are still text fields that neither drag nor take the value keys. Neither do "." and "," move between a position's fields. Halftrack draws no mark on the control clicked last.

## Evidence in Halftrack

The UI suite checks:

- that dragging a track's pan knob up pans it by the knob's drag rate, and with Shift a tenth as far;
- that the volume fader, from where it is clicked, moves a tenth as far with Shift;
- that after a click on the pan knob, Delete centres it in one undo step, Up pans it a step right in one undo step, and Down a step left;
- that Up raises the volume fader a hundredth of its travel, and Shift+Up by a finer step;
- that typing a minus sign opens the field, and Return sets the -12 dB typed in one undo step;
- that Escape leaves the value, and a click elsewhere sets what was typed and ends the value keys, so Up no longer moves the fader;
- that after a click on the clip's Transpose knob, Up steps it a semitone and Shift+Up an octave.
