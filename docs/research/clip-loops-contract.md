# Clip loops and edges

Status: partial, ARR-003 and ARR-012.

The reference's clip view sets where a clip's content starts (the Start marker), where an unlooped clip ends (the End marker), and a loop by its Position and Length, with a Loop switch. In the Arrangement, a looped clip can be made longer than its loop, which then repeats, and its edges trim it. Halftrack follows that model for MIDI clips and warped audio clips. The behaviour comes from the reference's manual and use; no reference file was read.

## Model

Every MIDI clip and warped audio clip reports, in the beats of its content:

- `contentStartBeat`: the beat it plays at its start (the Start marker);
- `loop`: whether it loops, and the loop's start and length. Unlooped, the loop gives the content it plays.

Looping, the clip plays from its start into the loop and round it for as long as the clip lasts. Notes are cut at the loop's end, as the engine's looped sequence does. Unlooped, it plays from its start for its length.

`Session::setClipLoop (clipId, loop, contentStartBeat)` sets both. It is one undo step, and the clip keeps its place.

- **Looping:**
  - the loop must last at least a 64th of a beat;
  - the start must lie in the loop. The reference also allows a start before the loop, which the engine's offset, counted from the loop's start, cannot hold.
  - a warped clip's loop must end within its audio.
- **Unlooped:** the start must lie within a warped clip's audio, and a warped clip is shortened to end with its audio.
- **Session clips** loop over their content when made. Unlooped, a Session clip plays once from its start, as the reference's one-shot clips do, and stops. It ends where its loop ended, and End sets its length.

`Session::setClipBeats (clipId, startBeat, lengthBeats, contentStartBeat)` places an Arrangement clip's edges in project beats. A Session clip stays at its slot's start (beat 0), so only its length and content start change. Moving the start edge and the content start together trims the clip without moving its content.

- A looped clip lasts as long as it is made.
- An unlooped warped clip cannot run past its audio, and an unlooped clip cannot start before its content.

**Other edits:**

- **Warp off.** Turning Warp off ends a clip's loop, keeping the beat it starts on, as unwarped audio does not loop here.
- **Tempo change.** Changing a warped clip's tempo scales its loop to cover the same audio.
- **Copying.** Copying, cutting, pasting and duplicating a MIDI clip carry its loop and start.
- **Refused for looped or offset MIDI clips:** splitting, range edits, controller editing and recording quantisation, as before.

## Interface

**The Loop box** sits beside the note editor for every MIDI clip, and in the audio clip view for a warped clip. Values are in bars.beats.sixteenths in the project meter: positions from 1.1.1, lengths from 0.0.0. It holds:

- **Start** and **End**. End applies to unlooped Arrangement clips.
- **Loop.** Turning it on loops what the clip plays.
- **Position** and **Length**, enabled while looping. Moving the loop past the start takes the start with it.
- **Set** buttons beside Start, End, Position and Length, as the reference's ([Clip View](https://www.ableton.com/en/live-manual/12/clip-view/), consulted 2026-10-24). While the clip plays, each puts its marker at the play position rounded to the global launch quantization; Position's and Length's turn the loop on. Set beside End waits while the clip loops.

**Edit > Clip Markers** (Set Clip Start, Set Loop Start, Set Loop End, Set Clip End, on Cmd+F9 to Cmd+F12 as the reference's keys) sets the same markers for the MIDI clip in the clip view: at the play position while it plays, rounded the same way, or else at the note editor's insert marker.

**The editors.** The note editor and the warped waveform:

- dim the content the clip does not play;
- draw the loop brace along the top and a start marker;
- follow the play head round the loop.

Clicking the note editor's ruler seeks to where that beat first plays. Its loop brace drags:

- its ends resize the loop;
- its middle moves the loop and the start with it;
- the start marker moves alone, kept in the loop.

A click on the loop brace also selects it, shown in orange, until a click elsewhere in the editor. Then, as the reference's brace keys (manual section 42.8), Left and Right move it by the note grid, Up and Down by its length, Cmd/Ctrl with Left and Right shorten and lengthen it by the grid, and Cmd/Ctrl with Up and Down double and halve it, each one undo step, the start kept inside the loop.

A click on the start marker's flag selects it the same way (2026-10-25), as the reference's manual has the start and end markers move with the arrow keys and Option move the whole clip region ([Clip View](https://www.ableton.com/en/live-manual/12/clip-view/), section 8.8, consulted 2026-10-25): Left and Right move it a note grid step as its drag does, kept inside the loop; with Option, or Shift as the reference's key list names Move Clip Region with Start Marker (section 42.12), a looped clip's loop moves with it. Each is one undo step. An unlooped clip has no end marker of its own in Halftrack's note editor, so its start marker's move already moves its region.

Cmd-click in the ruler moves the start marker to the snapped beat, and Cmd+Shift-click an unlooped clip's end marker, as the reference's keys do (manual section 42.8, Move Start/End Marker to Position; 2026-10-25); each is one undo step.

Drags snap to the note grid unless Alt is held, and each is one undo step. A warped clip's waveform drags the same way:

- **Brace and start.** The brace runs along the top and the start marker along the bottom.
- **Grid.** It snaps to the zoom-following grid that warp markers use.
- **Bounds.** It keeps the loop within the audio.

**The Arrangement:**

- **Drawing.** A looped clip's notes or waveform are drawn a pass at a time, with a notch where each pass begins.
- **Edges.** A selected MIDI or warped clip's edges drag in beats, snapped unless Alt is held. A looped clip's start edge moves its start round the loop. Each drag is one undo step.

## Evidence

`tests/ClipLoopTests.cpp` renders a four-beat MIDI clip with a loud note on its first beat and a quiet one on its second, and reads each beat's level. Renders show:

- the notes once unlooped;
- a two-beat loop repeating for the clip's length, and further when the clip is made eight beats long;
- a start inside the loop, and a one-beat loop of the quiet note;
- a start edge trimmed in, the content staying in place;
- the same loop from a duplicate, a pasted copy and a reopened project.

It also checks:

- undo of each edit;
- refusals: an empty loop, a start outside it, negative beats, and a Session clip moved off its slot's start;
- **a Session clip**, launched through the engine:
  - its two-beat loop repeats;
  - unlooped, it plays its two beats once and stops;
  - set to one beat from its second, it plays that beat alone;
- **warped audio** of alternating loud and quiet clicks:
  - a loop inside the audio;
  - a clip longer than its audio, still looping;
  - a tempo change that scales the loop;
  - unlooping, which ends the clip with its audio;
  - Warp off, which ends the loop where it starts.

The main UI suite turns Loop on from the Loop box and sets Length and Position, and checks that a start outside the loop is refused. It drags the loop brace's end, the brace and the start marker in the note editor's ruler, undoing each, Cmd-clicks the ruler to move the start marker and Cmd+Shift-clicks it to move an unlooped clip's end marker, and steps a clicked brace with Right, Cmd+Up and Down, undoing them; it moves a clicked start marker with Right and its region with Option+Right, undoing both. It also lengthens a warped clip's loop by its brace in the waveform. It sets the loop's end and start at the insert marker from Edit > Clip Markers, greys Set Clip End while looping, sets the start with Cmd+F9, and while the clip plays sets the loop's Position from its Set button. It drags a looped clip's right edge to repeat it and its left edge to trim it, undoing each, and shows the Loop box for a warped audio clip.

## Duplicate Loop (2026-10-26)

As the reference's manual describes in its Clip View chapter (read 2026-10-26), Duplicate Loop doubles a MIDI clip's loop and copies what it holds. In the reference's 12.2 menus the command is Edit > Duplicate (Cmd+D) acting on a selected loop: its Edit menu has no separate item (static reading of its menu tables), and a loop bar duplicates the same way.
- **What it does** (`Session::duplicateClipLoop`): the loop's length doubles. The notes starting in the loop, whole, under fresh identities, and its controller points are copied a loop's length later. Notes and points after the loop move on by its length, keeping their place after its end.
- **Clip length.** A Session clip lengthens with its loop; an Arrangement clip keeps its length.
- **Undo.** One undo step.
- **How it is reached.** With the note editor's loop brace selected, or a loop bar clicked, Cmd+D and Edit > Duplicate (which then reads "Duplicate Loop") duplicate the loop rather than the notes.
- **Audio.** Warped audio clips have no Duplicate Loop in Halftrack.

The clip-loops suite checks an Arrangement clip (its note and modulation point copied, the later note moved on, the clip as long, undo, and refusal without a loop) and a Session clip lengthening. The UI suite duplicates a loop bar's loop with Cmd+D.

## Not yet covered

- A start marker before the loop.
- Loops for unwarped audio clips.
- Splitting, consolidating and range edits of looped clips.
- The reference's behaviour when a long looped clip's loop is turned off; Halftrack keeps the clip's length for MIDI.
