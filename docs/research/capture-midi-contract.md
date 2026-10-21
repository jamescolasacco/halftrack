# Capture MIDI contract

As in the reference, Halftrack keeps listening to MIDI played into armed or In-monitored MIDI tracks, and Capture MIDI turns what was just played into a clip.

## Behaviour

- **Listening.** A capture tap on each MIDI input instance records note-ons and note-offs with the track, their arrival in the audio device's stream time (as the engine stamps them), and, while the transport plays, the beat that time fell on, mapped as the engine maps recorded MIDI. The taps re-attach whenever the edit's inputs change, since the engine can replace its input instances then.
- **Which tracks.** A track keeps notes only when its input is armed or its monitoring is In. Each track keeps at most 4096 notes.
- **Capturing** (`Session::captureMidi`, the Capture button, Shift+Cmd+C) takes each track's phrase since its last pause of over ten seconds and ends any held notes:
  - **played while the transport ran:** notes keep their beats in a clip from the bar the first fell in, lasting whole bars;
  - **played with it stopped:** notes are timed at the current tempo from the first note, in a clip starting at the insert marker;
  - **in Session View** the clip goes into the track's first empty slot.

  The kept notes are then cleared, and the whole capture is one undo step.

## Evidence

`tests/CaptureMidiTests.cpp` drives a generated controller through the synthetic device. It checks:

- that an unarmed, unmonitored track keeps nothing and capture is refused;
- that with the track armed and the transport stopped, two notes become a one-bar clip at the insert marker (beat 4), in order, the first at the clip's start with its velocity;
- that capturing empties the kept notes, and that one undo removes the clip;
- that a note played at beat 5.5 while playing lands 1.5 beats into a clip starting at beat 4;
- that an In-monitored, unarmed track captures into its first empty Session slot.

The main UI suite checks that the Capture button stays disabled until notes are played and that Shift+Cmd+C is handled.

Missing: tempo detection when stopped, step input, MPE data, and capture of material longer than the reference's history.
