# MPE contract

Halftrack's MPE support follows the reference's MPE note editing and recording: each note can carry its own pitch bend, slide and pressure, recorded from an MPE controller or drawn in the piano roll, and played back with each note on its own MIDI channel.

## Behaviour

- **Expression.** A note's pitch bend (−48 to 48 semitones), slide (CC 74, 0–1) and pressure (0–1) are curves of points at beats from the note's start, ordered and inside the note.
  - Playback ramps between points, in 1/64-beat steps, and holds the last.
  - `setMidiNoteExpression` sets one curve as one undo step, and no points removes it.
  - Curves are saved with the note, copied with it, and validated on load.
- **Playback.**
  - A clip with any expression plays as MPE, in a lower zone of 15 member channels: each note on a channel of its own, with its bend, slide and pressure sent there.
  - Each note's channel gets a 48-semitone pitch bend range (RPN 0) as the note starts.
  - The built-in synth and the native instruments honour RPN 0 on each channel, so they bend MPE notes correctly whatever their own Bend range. Other MIDI keeps the device's range.
- **Recording.**
  - A MIDI input switched to MPE (Audio & MIDI Setup's MPE Inputs menu, kept with the device settings) records its notes with their own bend and pressure.
  - Its bends are read at 48 semitones in a lower zone of 15 member channels.
- **The editor.**
  - The piano roll's lane chooser has a Note Expression (MPE) submenu: Note Pitch Bend, Note Slide and Note Pressure.
  - The lane shows the selected note's curve over the note; clicking adds a point and drags it, and double-clicking or right-clicking a point deletes it.
  - Notes with a pitch bend show it on the note in the grid.

## Engine changes

`cmake/PatchMpeTiming.cmake` fixes where expressive notes and their expression land in MIDI clips' raw-beat playback sequences. They were placed in seconds, which then played as beats, so at 120 BPM everything came at half its time. See [dependencies](../dependencies.md#tracktion-mpe-timing-patch).

## Evidence

`tests/MpeTests.cpp` renders sine voices at 120 BPM:

- **A bend under a held chord.** C4 bent up an octave over its first beat sounds C5 while the G4 beside it keeps its pitch; one undo restores C4.
- **The ramp.** It passes F♯4 halfway (368 Hz), and the bent note holds C5 to its end at beat 8.
- **The Poly Synth** bends the same note to C5 although its own Bend range is 2 semitones.
- **Refusals:** a bend past 48 semitones, a point after the note's end, unordered points and an unknown note.
- **Keeping the data.** A copied note keeps its bend. Slide, pressure and bend reopen, and a bend of 100 semitones in a saved file is refused while keeping the open project.
- **Live playback through a MIDI Monitor.** The two notes play on channels 2 and 3, each with a 48-semitone range, and the first with its slide and pressure.
- **Recording from a virtual controller switched to MPE:** a note bent +12 semitones with pressure 64 records with that bend and a pressure of 0.5. Unknown inputs are refused.

The UI suite chooses Note Pitch Bend for a selected note, clicks the lane over it to bend it 24 semitones, and undoes it.

## Not yet covered

- The native instruments do not yet respond to slide or pressure; they receive them.
- The zone layout is fixed, so upper zones and member-channel counts are missing.
- Sending MPE to external plug-ins is untested against an MPE plug-in.
- Editing several notes' expression at once is missing.
