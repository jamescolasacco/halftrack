# MIDI editor Preview contract

The reference's MIDI editor has a Preview switch above the piano ruler. With it on, you hear notes as you click the ruler's keys and as you add, select and move notes. With the track also armed, the arrow keys step record (see the [Capture MIDI contract](capture-midi-contract.md#step-recording)). Its state is stored per Set: `MidiPrelisten`, false in the reference's default set (`DefaultLiveSet.als`, Live 12.2.1).

## Behaviour

- **The setting.** `Session::notePreview` / `setNotePreview`:
  - off at first, covering every MIDI track in the project;
  - saved with the project as `halftrackNotePreview` on the edit;
  - not an undo step or an unsaved change, like the editing grid.

  Switching it off ends any preview notes.
- **Preview notes.** `Session::previewNote (trackId, pitch, velocity, on)` starts or ends a note on channel 1 of a MIDI track's instrument, or its MIDI To port. It goes through the engine's live MIDI injection, with the transport stopped or running.
  - It is refused for audio tracks, missing tracks, pitches outside 0–127 and velocities outside 0–1.
  - `stopNotePreviews` ends every note started, and opening a project ends the replaced one's.
- **The switch.** It is drawn as headphones above the keys, lit when on. While it is on, the piano roll sounds notes on the clip's track, at most eight at once:
  - a key while it is held, and each key a drag along the keys reaches (velocity 100);
  - a clicked note while the button is down, at its velocity;
  - a drawn note while the button is down, at the draw velocity;
  - a dragged note each time it reaches another pitch;
  - notes transposed with the arrow keys, for 200 ms.
- **Key track selection.** Shift-clicking a key adds every note of that pitch to the selection, or takes them out if all were selected. This works whether Preview is on or off, as the reference's manual describes.

## Simple defaults

These are recorded in the [open questions](../open-questions.md):

- the ruler-key velocity (100);
- the 200 ms sounding of arrow-key transpositions;
- the eight-note limit;
- a plain key click only sounding, without selecting.

## Real-time path

Preview notes reach the audio thread through Tracktion's `LiveMidiInjectingNode`. It takes a `juce::CriticalSection` in `injectMessage` and in every `process` call. That is a dependency lock (see [real-time safety](../realtime-safety.md)), not an owned lock-free path.

## Evidence

`tests/NotePreviewTests.cpp` (`note-preview-acceptance`) checks:

- the setting's default, that it is not an undo step or an unsaved change, and that it is saved and reopened;
- refusals;
- that a preview note on the built-in synth sounds with the transport stopped (output peak above 0.01) and is silent within half a second of release;
- two notes together, ended by `stopNotePreviews`;
- that switching Preview off, and opening another project while a note sounds, both end it;
- that previewing leaves the transport, history and saved state alone.

The main UI suite checks:

- the switch's place and default;
- that a key is silent with Preview off, and sounds while held with it on;
- that clicking a note and drawing one sound while the button is down, and stop on release;
- Shift-clicking a key selects its pitch's notes (Delete then removes just those);
- that step recording needs Preview on.

Not measured: whether the reference's preview velocity, the length it sounds for arrow-key moves, and its handling of horizontal drags match.
