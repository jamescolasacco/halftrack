# Value controls contract

Halftrack's knobs, faders and number fields take the reference's value gestures: a finer drag with Shift, number fields dragged by their parts, and keys for the control clicked last.

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
- **Number fields** (`ValueField`) drag by their parts, the runs of digits in their text, as the reference's fields do: 3 pixels up or down a step, a tenth as fast with Shift. Each drag is one undo step.
  - The Control Bar's tempo steps its whole beats or hundredths, from 20 to 999 BPM.
  - Its time signature steps its numerator (1 to 99) or its denominator (1, 2, 4, 8 or 16).
  - Its loop start steps bars, beats or sixteenths, keeping the loop's length; a bar keeps the place within the bar through meter changes.
  - Its loop length steps bars, beats or sixteenths, at least a sixteenth.
  - Its song position steps bars, beats or sixteenths and moves the insert marker, as do the arrow keys and a typed position, as the reference's Arrangement Position fields do (manual section 6.3: drag up or down, type a number and press Enter, or use the up and down arrow keys; adjusting them moves the insert marker). While playing, playback goes on and the insert marker moves. Neither is an undo step. During a count-in the field shows the count and does not drag.
  - The clip's Start, End, Loop Position and Loop Length step bars, beats or sixteenths. End stays after the start, and a length at least a 64th of a beat.
  - A scene's tempo and time signature fields in the Session drag the same way, from the scene's own value or else the song's ([scene tempo](scene-tempo-contract.md)). They do not take the value keys.
  - An audio clip's tempo steps its whole beats or hundredths, from 20 to 999 BPM, a tenth as fast with Shift, as the manual describes its BPM field (section 9.2.4.1: type the BPM, or "hold Shift while dragging" for fine increments).
  - A click without a drag selects the field, a double-click edits its text, and an uneditable field (the tempo while it is automated) does not drag. The clip's fields, edited before on a single click, now take a double-click, as the Control Bar's do.
- **The control clicked last** takes the value keys until a click elsewhere in the window (`MainComponent`'s `valueKey`):
  - **Up and Down** step it.
    - A value in semitones moves a semitone, or an octave with Shift: the clip's Transpose, a Drum Rack pad's tune, and device controls in semitones.
    - A value with at most 200 steps moves one step.
    - Any other value moves a hundredth of its travel, or a thousandth with Shift, and at least one step.
  - **Delete or Backspace** returns it to its default. A track's mixer controls reset as double-clicking them does, with the controls linked to them (`Session::resetTrackControl`).
  - **Typing** a digit, a minus sign or a point opens a field over the control showing what is typed. Return, or a click elsewhere, sets the value, read as the control reads its own text box. Escape leaves it.
  - **On a number field**, Up and Down step the part clicked; with Shift, the last part, for a fine adjustment, except the time signature's, whose numbers do not run from coarse to fine. Typing starts editing its text with what is typed, which Return sets and Escape leaves.
  - **On a position or length** (bars, beats and sixteenths), typing replaces the part clicked alone (2026-10-25). While editing, "." or "," selects the next part, whose digits the next typed ones replace, and after the last part adds a point, as the reference's key list has them go to the next field. Typed commas read as points.
  - Each change from the keys is one undo step, as a drag is.
  - Keys with Cmd, Ctrl or Option, and any other key, go on to the window.
- **Missing.** The scenes' tempo and signature fields do not take the value keys. Halftrack draws no mark on the control clicked last.

## Evidence in Halftrack

The UI suite checks:

- that dragging a track's pan knob up pans it by the knob's drag rate, and with Shift a tenth as far;
- that the volume fader, from where it is clicked, moves a tenth as far with Shift;
- that after a click on the pan knob, Delete centres it in one undo step, Up pans it a step right in one undo step, and Down a step left;
- that Up raises the volume fader a hundredth of its travel, and Shift+Up by a finer step;
- that typing a minus sign opens the field, and Return sets the -12 dB typed in one undo step;
- that Escape leaves the value, and a click elsewhere sets what was typed and ends the value keys, so Up no longer moves the fader;
- that after a click on the clip's Transpose knob, Up steps it a semitone and Shift+Up an octave;
- that the tempo field has two parts, the loop start three and the signature two;
- that dragging the tempo's whole beats up two steps adds 2 BPM in one undo step without editing its text, its hundredths add 0.05 BPM in five steps, and Shift steps a tenth as fast;
- that after the click Down steps the whole beats, Shift+Up the hundredths, and typing a digit edits the field, whose confirmed text sets the tempo;
- that dragging the loop start's beats moves the loop two beats later keeping its length, and the length's bars add a bar;
- that dragging the song position's bars up two steps moves the insert marker to bar 3, Up a bar further, and a typed 2.2.1 moves it there; then that typing 3 on its beats replaces them alone, a comma moves on to the sixteenths, which a 4 replaces, and confirming moves the insert marker to 2.3.4;
- that dragging the signature's numerator and denominator up makes 5/4 and then 5/8, and Shift+Up steps the denominator clicked;
- that dragging the clip's Length field's beats up three steps lengthens its loop three beats in one undo step;
- that dragging an audio clip's tempo's whole beats up two steps adds 2 BPM in one undo step;
- that dragging a scene's tempo's whole beats up three steps makes it 143 without asking, its hundredths with Shift a tenth as fast, and its signature's denominator down a step makes 7/4, each one undo step.
