# Track insertion and names contract

Where Insert puts a new track, and how tracks name themselves, follow a static reading of the reference's code (findings in the ignored `artifacts/reverse-engineering/tracks/`, 2026-10-23). Nothing was measured in the reference application.

## Insertion

`addAudioTrack (name, after)` and `addMidiTrack (name, after)` place the new track:

- right after the track `after` names, inside its group;
- after a group `after` names and everything in it, as the group's sibling;
- after the other tracks and before the returns when `after` is empty or names a return.

`addReturnTrack (name, after)` puts a return right after the return `after` names, otherwise after the other returns. Each insertion is one undo step.

The interface's Insert Audio Track (Cmd/Ctrl+T), Insert MIDI Track (Cmd/Ctrl+Shift+T) and Insert Return Track (Cmd/Ctrl+Option/Alt+T) pass the last selected track in track order. The reference uses the last entry of its selection in the same way. The new track is then selected alone. Insert MIDI Track makes an empty MIDI track, as the reference's does: no clip (Insert MIDI Clip adds one) and no instrument until one is dropped on it, so it shows n-MIDI. It is one undo step. Before, Halftrack's Insert MIDI Track also made and opened a clip and gave the track the built-in synth. `Session::addMidiTrack`, which the suites and imports use, still gives the track the built-in synth.

## Names

A track's own name is its stored name, and empty means automatic. `TrackInfo::name` is the name shown, and `TrackInfo::userName` is the track's own name; `GroupInfo` has the same pair.

- **Number.** An audio, MIDI or group track's number is its place among those tracks, groups and grouped tracks included and returns not, counting from 1. Automatic names renumber as tracks are inserted, moved or removed.
- **Automatic audio and MIDI names.** The number, "-", then the first match among:
  - a MIDI track's first instrument's name;
  - the name of the first clip the track was given;
  - Audio or MIDI.

  For example 1-Audio, 2-MIDI, 2-Subtractive or 3-Kick.
- **The first clip's name** follows that clip while it is the track's only clip, stays when more clips come, and goes when the last one goes. A clip named only for its track (the type word, or a number, "-" and the type word, with any count after it) does not name it. The name is kept with the project as `halftrackFirstClipName`, with `halftrackFirstClipId` for the clip it follows. Neither is part of the undo history.
- **Groups.** An automatic group name is its number and Group, as in 1-Group.
- **Returns.** A return shows its letter (A for the first return, then B and so on), "-", and then its own name, or its devices' names joined with " | ", or Return. For example A-Return, A-Room Reverb | Stereo Delay, or A-Verb.
- **Names of their own.** A name of its own is shown without a number. A leading run of # becomes the track's number, padded with zeros to the run's length: "## Keys" on track 3 shows 03 Keys.
- **Renaming** (`renameTrack`, `renameGroup`) trims the name. An empty name or the automatic name itself makes it automatic again; so does, on a return, its automatic part alone. On a return, its letter typed before the name (as when editing the shown name) is not kept twice. Each rename is one undo step, and renaming to the same name is none.

**Activators.** A track's activator shows its number, counted as in automatic names (groups included), and a return's shows its letter, as the reference's do (`TrackInfo::numberLabel`, `GroupInfo::numberLabel`). A group's shows its number, where Halftrack's showed G.

A new project's two tracks, imported audio and imported movies get automatic names. Their first clips then name imported tracks after their files. Duplicates keep the source's own name, or stay automatic, where Halftrack used to add " copy". Take, flatten, consolidate and stem files, and messages, use the name shown, so a return's stem is, for example, "Song A-Room.wav".

## Evidence

`tests/TrackOrderTests.cpp` checks:

- that a new project's tracks are named automatically;
- that a MIDI track inserted after the first goes right after it, named by its instrument (2-Subtractive), then 2-MIDI without it, and the track after it renumbers;
- that a return inserted after the first return lands between, and returns are lettered A, B, C in order;
- that an audio track inserted with a return selected goes after the other tracks, as one undo step;
- that return A's two devices name it;
- renaming, its undo step, a leading ## padded to 03, empty and automatic names returning to automatic, trimming, and a return's letter before its own name;
- that the first clip names a track, the name follows its only clip, stays with two clips, goes with none, and a clip named "3-Audio 2" does not name it;
- that a group is 1-Group and the tracks after it renumber, the activators showing 1 for the group, 2 to 4 for the tracks and A to C for the returns;
- that inserting after a grouped track joins the group, and inserting after the group goes after everything in it;
- that the names reopen unchanged and the project is not marked changed.

Moving tracks renumbers automatic names, so the track-order suite's identity checks compare names of their own. The video suite finds an imported movie's track named by its clip. The linked-track and parameter-identity suites find duplicates keeping the source's name. The stems suite names a return's stem with its letter, and the sends suite checks a return's rename and its undo with the letter. The UI suite checks:

- Insert MIDI Track making an empty track without an instrument, named n-MIDI, as one undo step, and Insert MIDI Clip adding the clip;
- Insert Audio Track after the selected track, selected, with the tracks after it renumbered;
- Cmd/Ctrl+Option/Alt+T inserting a return named A-Return, whose activator shows A;
- a return renamed from its label shown after its letter, and editing the shown name not repeating the letter.

## Not implemented

- The new track copying the selected track's Session width, its automatic colour from the reference's picker, and auto-arming on insertion (the reference's condition was not decoded).
- An unfolded selected group placing the new track inside the group at its end (medium confidence in the reading), and selected clips or slots taking part in the selection.
- Insert Return Track's limit of twelve returns.
- A new MIDI clip's name. Halftrack names it "MIDI clip"; the reference's default was not read.
