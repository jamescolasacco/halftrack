# Capture MIDI contract

As in the reference, Halftrack keeps listening to MIDI played into armed or In-monitored MIDI tracks, and Capture MIDI turns what was just played into a clip.

## Behaviour

Sources: the reference's manual, and a static reading of its capture code on 2026-10-26 (Live 12.2.1, arm64 slice, sha256 b37cb738442bba7787457fe751b0e3f9fbdabc662d8d491d1d9aa46b120bd3fc). This was binary-assisted research, not a clean-room process. The raw notes and the research model, an executable copy of the analysis, stay in the ignored `artifacts/` folder. Nothing was launched or measured.

- **Listening.** A capture tap on each MIDI input instance records note-ons and note-offs with the track, their arrival in the audio device's stream time (as the engine stamps them), and, while the transport plays, the beat that time fell on, mapped as the engine maps recorded MIDI. The taps re-attach whenever the edit's inputs change, since the engine can replace its input instances then.
- **Which notes.** A track keeps notes only when its input is armed or its monitoring is In, though a note it kept still ends when released after the track is disarmed. As in the reference:
  - a track's history starts anew with the first note after the transport started or stopped;
  - it holds up to 16384 notes, dropping the oldest 1024 when full.
- **Capturing** (`Session::captureMidi`, the Capture button, Shift+Cmd+C):
  - It takes the unfrozen tracks played last: those whose latest note started within 5 ms of the latest of all.
  - Each track's notes after its last silence of more than 20 seconds are kept (a silence runs from a note's end to the next note's start). Held notes end at the capture.
  - The kept notes are then cleared, and the capture is one undo step.
- **Played while the transport ran.** The notes keep their beats, in a clip from the bar the first fell in, lasting whole bars. No tempo is set.
- **Played with it stopped.** The reference's analysis (`src/CaptureTempo.cpp`) finds the tempo, fits a tempo map to the notes and loops them:
  - **The context.** The tempo is found freely, from 78 to 164 BPM, when no other track has clips, the tempo is neither automated nor following an external clock, and it is 120 BPM. "It" is the tempo before the last capture while that capture's tempo stands, so capturing again finds the tempo freely again. With no other clips at another tempo, the analysis searches around that tempo, favouring it. With other clips (or a controlled tempo), the project's tempo is a candidate with a bonus, and stays when the result is within 8.5 % of it.
  - **The analysis.**
    - It finds the last phrase: after the last silence of more than 1.7 s, or after a clearly longest gap inside it.
    - It gathers up to four tempo candidates from the notes' distances, weighting louder, lower and longer notes more.
    - For each candidate, a beam search places every note on a grid of half, triplet and sixteenth positions, keeping or changing the tempo between notes.
    - It scores how well that fits and how much the last four bars repeat, and takes the best. Candidates within 5 BPM of a better one are dropped, and the lower tempo wins a tie.
  - **The tempo** is rounded to 0.5 BPM (0.1 BPM for a single held note). It becomes the project's tempo; when it was weighed against other clips, its change is an undo step of its own, after the clips'. Clips, the insert marker and the loop keep their beats when it changes.
  - **The notes** are not quantised: each lands where the fitted tempo map puts it, on the reference's tick of 1/3843840 beat. A note starting less than 0.085 beat before the loop's start or end moves onto it.
  - **The loop** runs from the bar of the last phrase's first note to the bar after its end, from one to eight bars. A longer phrase loops its last 8, 4, 6, 5 or 7 bars, the first of those whose start cuts no note. The start marker is at the loop start, and earlier phrases stay before it in the clip.
- **Where the clip goes.**
  - In the Arrangement, at the insert marker, one loop long; nothing plays.
  - In Session View, into the selected scene when every captured track has an empty slot there; otherwise into the scene after those tracks' lowest clip, which is added when there is none. That scene becomes selected, and with Select on Launch the new clip's slot too. The new clips then play, as clicks on their launch buttons would start them.
  - The bar is the scene's own meter when it has one, else the project's.

## Step recording

As in the reference's MIDI editor, with the clip's track armed, the editor's [Preview switch](note-preview-contract.md) on and the transport stopped, held keys are entered note by note from the insert marker.

- **Right Arrow** adds every note held on the track's inputs at the insert marker, one grid step long at the velocity played, and moves the marker a step on.
  - Held through another Right Arrow, the same notes lengthen by a step.
  - A different chord held starts a new step.
- **Left Arrow**, while the last step's notes are still held, takes them out and moves the marker back to where they began.
- **Undo.** Each step, lengthening and removal is one undo step.
- **Otherwise the arrows behave as usual.** With nothing held, the track unarmed, Preview off, or the transport playing, they move the marker or the selected notes.
- **Held notes.** `Session::heldInputNotes` gives the notes held now from the capture taps above (after delivering pending ones), in pitch order with their channel and velocity.
- **A simple default** (see [open questions](../open-questions.md)): steps stop at the end of the clip's content, where the insert marker stops.

## Evidence

`tests/CaptureMidiTests.cpp` first checks the analysis against the research model's outputs for the same generated notes (recorded 2026-10-26), to 1e-9 beat:

- quarter notes at 100 and 90 BPM, eighths at 128 and quarters at 70 (taken as 140), with their tempos, loops and beats;
- a melody wobbling by 10 ms and eighths swaying around 100 BPM (104), not quantised;
- a short and a 3.1 s single note (120 and 77.4 BPM);
- a pickup landing on the downbeat;
- two phrases looping the last;
- twelve bars looping their last eight;
- rolled chords keeping their spread;
- played at 104 and 125 BPM in a 100 BPM project, as kept, beside other clips and as a hint;
- the 20 s and 30 s history rules, the grid of twelfths, the engine's tick and the rounding.

Then it drives a generated controller through the synthetic device and checks:

- that an unarmed, unmonitored track keeps nothing and capture is refused;
- that with the track armed and the transport stopped, quarter notes at 112.5 BPM set that tempo and become a two-bar loop at the insert marker (beat 4), on the beats, in order, with their velocity;
- that capturing empties the kept notes, and that one undo removes the clip and restores 120 BPM;
- that beside another track's clip, 112.5 BPM leaves the tempo at 120 while the notes still land on the beats, and that 90 BPM wins, its tempo change undone before the clip;
- that a note played at beat 5.5 while playing lands 1.5 beats into a clip starting at beat 4;
- that an In-monitored, unarmed track captures into the selected scene's slot and the clip plays, and that a second capture goes into the scene after the track's lowest clip.

The same suite checks that an armed track reports its held notes in pitch order with their velocities, that they drop out when released (also when released while the track was disarmed), and that an unarmed, unmonitored track reports none.

The main UI suite checks:

- that the Capture button stays disabled until notes are played and that Shift+Cmd+C is handled;
- step recording with notes held on the computer keyboard into an armed track (refused with Preview off):
  - Right Arrow adds a two-note chord a 1/16 step long at the velocity played;
  - holding through Right Arrow lengthens it, and Left Arrow takes it out and moves the marker back;
  - a new chord follows from the moved marker;
  - with nothing held the arrow only moves the marker, and an unarmed track does not step record;
  - each step is one undo step.

## Not yet covered

- The analysis has not been compared with the reference's own output; it follows the research model, which was not run against the reference either.
- Two hidden options of the reference that keep the tempo were not identified, and Halftrack has no Note Repeat, whose notes also keep it.
- When the transport ran, the reference's loop may follow the same rules as a stopped capture; Halftrack keeps whole bars from the first note.
- MPE data.
- Simplifications are listed in [open questions](../open-questions.md#capture-midi-boundaries).
