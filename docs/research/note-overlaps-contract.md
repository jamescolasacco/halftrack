# Note overlaps contract

Halftrack resolves overlapping notes of the same pitch in a MIDI clip as the reference does.

The rules come from a static reading of the reference's code on 2026-10-25: Live 12.2.1, arm64 slice, sha256 b37cb738442bba7787457fe751b0e3f9fbdabc662d8d491d1d9aa46b120bd3fc. This was binary-assisted research, not a clean-room process. Nothing was launched or measured, and the raw research notes stay in the ignored `artifacts/` folder.

## Behaviour

- **The rule.** For each pitch, the notes just added or edited win over the clip's other notes:
  - A note of the same pitch that starts before a winning note and sounds past its start ends there. Its tail is lost; the note is never split.
  - A note that starts at or after a winning note's start and before its end goes, even if it sounds on past that end. A note starting at the same moment goes too, whatever the two lengths.
  - Notes that only touch do not overlap.
  - Only the pitch matters. Other pitches, the clip's loop, and whether a note is deactivated play no part: a deactivated note cuts and is cut like any other.
- **Floating notes.** The notes being edited are the floating notes.
  - While the same notes stay selected and are edited again (moved, resized, transposed, nudged, transformed or deactivated), anything an earlier step cut short or took away comes back first. So dragging a note across another leaves the other note as it was.
  - Deleting the floating notes brings back what they took away.
  - A change of selection, any other edit, undo and redo all end this, keeping what has been resolved. Letting go of the mouse does not end it.
- **Which edits.**
  - Notes that are drawn, double-clicked, pasted, duplicated or step-recorded win as added notes. Moved, resized, transposed, nudged and transformed notes win as edited notes.
  - A copy drag leaves the copies where their notes were. The copies do not win; the dragged notes win over them when they move onto them. A note dropped back where it started takes its copy's place.
  - A lengthened note stops at the next selected note of its pitch.
  - Draw Mode's notes follow the same rule, and the note drawn on a step at an earlier pitch goes.
- **MIDI tools.**
  - A tool's notes go in one at a time, in order of start and then pitch. Each one wins over the earlier ones of its pitch. Then they win over the clip's other notes for good: the tool's edit ends there.
  - Rhythm first clears its key in the time range. A note sounding into the range ends at the range's start, and a note starting inside it goes.
  - Quantize first settles the notes it moves among themselves. Where one note holds the other, the longer stays, or the later when they are equally long. Where they only overlap, the earlier note ends where the later one starts.
- **Recording.** A recorded note wins over the clip's notes of its pitch. This applies to Session overdubs and to Arrangement Overdub merging a take into the notes it records over.
- **Not resolved.** Projects open with their notes as saved, as the reference's Sets do. MIDI files import their notes as they are.
- **Halftrack's own limit.** Halftrack's notes last at least a 960th of a beat, so a note that would be cut shorter than that goes instead. The reference keeps notes down to zero length.

## Evidence

The note-overlaps suite checks, with every result worked out by hand:
- adding a note: a note starting before it is cut, a note starting inside it or with it goes, and a longer note holding it is cut and not split. Touching notes and other pitches stay. A deactivated note is cut and stays deactivated. A cut that would be shorter than a 960th of a beat removes the note. One undo takes it all back;
- dragging a note over another, past it, onto its tail and away again, with the other note coming back with its identity and length. A drag in one gesture is one undo step;
- the floating session ending when the selection changes, after any other edit and after undo and redo, and lasting through re-selecting the same notes and deactivating them. Deleting the dragged note brings back what it took away;
- a copy left where its note is, cut while the note moves over it, whole again when the note moves off, and replaced when the note is dropped back;
- Quantize's own rule for contained, equally long and partly overlapping notes, the quantized notes cutting the clip's other notes, and the cut staying after the tool;
- Rhythm cutting a note that sounds into its range and clearing a note that starts inside it.

The take-lanes suite records a note on a key a little after an earlier note of that key with Arrangement Overdub on, and checks that the earlier note ends where the new one starts. The session-record suite does the same with a Session overdub.

The UI suite lengthens two selected notes of one key and checks that the first stops where the second starts. It also checks that notes drawn in Draw Mode over another note of their pitch take its place.

## Not yet covered

- The reference trims the per-note expression of a note it cuts and restores it within the session. Halftrack leaves the cut note's expression as it was.
- The reference's start drag stops at the previous selected note's end. Halftrack has no start drag yet.
- The reference compares exact positions rounded to its tick (1/3843840 beat). Halftrack compares the positions it stores.
- Not determined from the reading:
  - whether drawing a note leaves it floating (the reference's draw path may commit at once; Halftrack keeps it floating while it stays selected);
  - how the undo steps are grouped;
  - how the reference imports overlapping notes from MIDI files;
  - how overlapping notes play back.
