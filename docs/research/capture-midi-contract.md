# Capture MIDI contract

As in the reference, Halftrack keeps listening to MIDI played into armed or In-monitored MIDI tracks, and Capture MIDI turns what was just played into a clip.

## Behaviour

- **Listening.** A capture tap on each MIDI input instance records note-ons and note-offs with the track, their arrival in the audio device's stream time (as the engine stamps them), and, while the transport plays, the beat that time fell on, mapped as the engine maps recorded MIDI. The taps re-attach whenever the edit's inputs change, since the engine can replace its input instances then.
- **Which tracks.** A track keeps notes only when its input is armed or its monitoring is In, though a note it kept still ends when released after the track is disarmed. Each track keeps at most 4096 notes.
- **Capturing** (`Session::captureMidi`, the Capture button, Shift+Cmd+C) takes each track's phrase since its last pause of over ten seconds and ends any held notes:
  - **played while the transport ran:** notes keep their beats in a clip from the bar the first fell in, lasting whole bars;
  - **played with it stopped:** notes are timed at the current tempo from the first note, in a clip starting at the insert marker;
  - **in Session View** the clip goes into the track's first empty slot.

  The kept notes are then cleared, and the whole capture is one undo step.

## Step recording

As in the reference's MIDI editor, with the clip's track armed and the transport stopped, held keys are entered note by note from the insert marker.

- **Right Arrow** adds every note held on the track's inputs at the insert marker, one grid step long at the velocity played, and moves the marker a step on.
  - Held through another Right Arrow, the same notes lengthen by a step.
  - A different chord held starts a new step.
- **Left Arrow**, while the last step's notes are still held, takes them out and moves the marker back to where they began.
- **Undo.** Each step, lengthening and removal is one undo step.
- **Otherwise the arrows behave as usual.** With nothing held, the track unarmed, or the transport playing, they move the marker or the selected notes.
- **Held notes.** `Session::heldInputNotes` gives the notes held now from the capture taps above (after delivering pending ones), in pitch order with their channel and velocity.
- **Simple defaults** (see [open questions](../open-questions.md)):
  - Halftrack's MIDI editor has no Preview switch, which the reference also requires, so arming the track is enough.
  - Steps stop at the end of the clip's content, where the insert marker stops.

## Evidence

`tests/CaptureMidiTests.cpp` drives a generated controller through the synthetic device. It checks:

- that an unarmed, unmonitored track keeps nothing and capture is refused;
- that with the track armed and the transport stopped, two notes become a one-bar clip at the insert marker (beat 4), in order, the first at the clip's start with its velocity;
- that capturing empties the kept notes, and that one undo removes the clip;
- that a note played at beat 5.5 while playing lands 1.5 beats into a clip starting at beat 4;
- that an In-monitored, unarmed track captures into its first empty Session slot.

The same suite checks that an armed track reports its held notes in pitch order with their velocities, that they drop out when released (also when released while the track was disarmed), and that an unarmed, unmonitored track reports none.

The main UI suite checks:

- that the Capture button stays disabled until notes are played and that Shift+Cmd+C is handled;
- step recording with notes held on the computer keyboard into an armed track:
  - Right Arrow adds a two-note chord a 1/16 step long at the velocity played;
  - holding through Right Arrow lengthens it, and Left Arrow takes it out and moves the marker back;
  - a new chord follows from the moved marker;
  - with nothing held the arrow only moves the marker, and an unarmed track does not step record;
  - each step is one undo step.

Missing: tempo detection when stopped, the MIDI editor's Preview switch, MPE data, and capture of material longer than the reference's history.
