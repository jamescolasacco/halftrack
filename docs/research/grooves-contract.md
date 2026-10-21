# Groove pool contract

MIDI clips play with grooves from a project groove pool, after the reference's groove pool. Sources:

- the manual's [Using Grooves](https://www.ableton.com/en/live-manual/12/using-grooves/) chapter, consulted 2026-10-20;
- the groove files in the installed reference application, Live 12 Suite 12.2.1. Their XML was read; no file is copied or shipped.

The reference's own symbol table names the same model: a song-level groove pool, a transport groove amount, each clip's groove reference and a default groove. Those parameters are Base, Quantize, Timing, Random and Velocity. This is binary-assisted research; the arithmetic below is Halftrack's own and is not measured against the reference.

## What the reference documents and its files show

- **The controls.** Each groove has Base, Quantize, Timing, Random and Velocity. Velocity runs from -100 to +100, and negative values invert the accents. Global Amount scales Timing, Random and Velocity for every groove, up to 130%.
- **Non-destructive playback.** A groove changes playback without changing the notes. Groove notes exactly on the Base grid leave the clip's notes there. Commit writes the groove into the notes and sets the clip's groove to None.
- **Extraction.** Grooves are extracted from the playing portion of a clip.
- **File structure.** A groove file (`.agr`) is XML. Under `Ableton/Groove` it holds:
  - `Name`;
  - a MIDI clip whose `Loop` gives `LoopStart`/`LoopEnd`, with notes timed in beats from the clip start;
  - `Grid`, `QuantizationAmount`, `TimingAmount`, `RandomAmount` and `VelocityAmount`.
- **Grid values.** The 219 library files use `Grid` 0–5. Their names identify 0 as 1/4, 1 as 1/8, 2 as 1/8T (`Quantize 8T`), 3 as 1/16, 4 as 1/16T (`Quantize 16T`) and 5 as 1/32.
- **Swing grooves.** A swing groove named N% places every second grid line at N% of its pair. "Swing 16ths 52" has its offbeat sixteenths at 0.26 beats, and "Swing 8ths 66" at exactly two thirds of a beat.
- **Quantize grooves.** These have no notes, Quantization 100 and Timing 0.
- **Lengths.** Grooves run 4, 8 or 10 beats (one 5/4 file).
- **Note attributes.** Note attribute order varies between files.

## Halftrack's model

A groove (`Session::GrooveInfo`) is a name, a Base, a length (1/960 to 1024 beats), the four amounts, and up to 4096 events (beat, velocity 1–127) ordered inside the length. A note at beat `b` in its clip is placed as follows:

1. **Grid line.** The note's grid line is the nearest multiple of the Base step `s`. Each event belongs to its own nearest line, modulo the groove's length in lines. When two events share a line, the closer one defines it.
2. **Quantize.** Quantize pulls the note toward its line by its percentage. It ignores Global Amount.
3. **Timing.** If the line has an event, the note moves by that event's distance from the line. The distance is scaled by Timing × Global Amount.
4. **Random.** The note moves by up to ±half a Base step, scaled by Random × Global Amount. The jitter comes from a hash of the clip, the pitch and the note's beat, so every note differs while renders and Commit repeat it exactly.
5. **Velocity.** If the line has an event, the note's velocity is multiplied by `1 + V × (event / loudest event − 1)`, with V = Velocity × Global Amount, and clamped to 1–127. Lines without events leave the note's velocity alone.

The note keeps its length. On playback a note moved before the clip start sounds from the start with its end kept, and Commit does the same.

## Interface

- **Library and files.**
  - `grooveLibrary()` is Halftrack's generated library: Quantize 1/4–1/32, and 8th, 16th and 32nd swing at 54, 58, 62, 66 (exactly 2/3) and 71%.
  - `readGrooveFile(file, groove)` reads `.agr` files, plain or gzip. Disabled notes are skipped, and a chord becomes one event at its loudest velocity. Damaged XML, non-numeric times, unknown grids and out-of-range settings are rejected.
- **Pool edits.** Each is one undo step.
  - `addGroove` copies a groove into the pool with a new id, and numbers repeated names.
  - `extractGroove(clipId)` makes a groove from a MIDI clip's active notes. A looping clip uses its loop.
  - `setGroove` replaces a groove's name, Base, amounts and pattern.
  - `removeGroove` also sets the clips that used the groove to None.
  - `setGrooveAmount` sets Global Amount, 0–130%.
- **Clips.**
  - `setClipGroove(clipId, id)` assigns a groove to an Arrangement or Session MIDI clip; an empty id means None. Audio clips and clips on frozen tracks are refused.
  - `commitClipGroove` writes the grooved placement into the notes and sets the clip to None, in one undo step.
- **Reporting.** `ClipInfo::grooveId`, `Snapshot::grooves` and `Snapshot::grooveAmount` report the state.

## Playback

Halftrack's engine behaviour overrides Tracktion's `createPlaybackMidiSequence` hook. The engine calls it on the message thread while building the playback graph, never in the audio callback. For a clip with a groove, the hook places a copy of the clip's MIDI list and plays the copy, so the clip's notes are never changed. A looping Session clip's list is mapped back to beats in the clip, so playback matches Commit.

The engine does not watch Halftrack's groove state. A watcher on the Session's current Edit therefore restarts playback when the pool or a clip's groove changes, undo and redo included. A replaced Edit is never touched. No Tracktion patch is involved.

## User interface

- **Groove section.** Every MIDI clip's editor has a Groove section beside the note editor. It holds the clip's groove chooser, Commit and Groove Pool.
- **Groove Pool window.** It lists each groove's name (double-click to rename), Base, Quantize, Timing, Random and Velocity, with a remove button. It also has Global Amount and Add Groove, which offers the library, a groove file, or extraction from the selected clip.
- **Browser.** The Grooves category lists the library and `.agr` files in `User Library/Grooves`. Return or double-click adds a groove to the pool and gives it to the selected MIDI clip.
- **Clip menus.** Arrangement and Session MIDI clip menus offer Extract Groove.

## Projects

The pool is a `HALFTRACKGROOVEPOOL` child of the Edit, holding `amount` and `HALFTRACKGROOVE` children with `HALFTRACKGROOVEEVENT` events. A clip's groove is its `halftrackGroove` id. Loading rejects these and keeps the open project:

- a second pool, or an amount outside 0–130;
- more than 256 grooves, or duplicate or empty ids;
- non-numeric or out-of-range settings or events;
- unordered events;
- a groove id on anything but a MIDI clip, or one that is not in the pool.

## Evidence

`tests/GrooveTests.cpp` (`grooves-acceptance`) renders sixteen eighth notes through a sine synthesizer and measures every onset and accent. The renders show that:

- the clip plays straight without a groove;
- 8th Swing 66 delays the offbeats by 1/6 beat;
- Timing 50% halves the delay, Global Amount 0% straightens it and 130% exaggerates it, and undo restores each;
- an accent groove plays the offbeats at 64/127 of the downbeats' level, on time;
- the committed clip renders identically to its grooved playback, and one undo restores the notes and the groove;
- a reopened project renders identically.

The suite also checks:

- **The pure model.** Offsets, Quantize ignoring Global Amount, Velocity's scaling and inversion, Random's range and repeatability, and neutrality.
- **Live playback.** The synthetic audio device plays straight, swings as soon as the playing clip gets a groove, and straightens on undo. A control build without the watcher failed this check.
- **Pool edits.** Pool names and validation, removal resetting Arrangement and Session clips, and undo.
- **Extraction.** A chord's loudest velocity becomes one event.
- **Groove files.** `.agr` reading, plain and gzip, and rejection of damaged files.
- **Projects.** Seven kinds of damaged project are rejected and the open project stays.

The UI suite covers:

- the Groove section;
- adding a groove from the Grooves browser;
- choosing None and the groove again;
- the pool window's Timing, Base and Global Amount controls;
- Commit and its undo;
- Extract Groove from the clip menu;
- removing a groove;
- undo back to an empty pool.

It also captures snapshots of the clip view and the pool window.

## Not implemented

- Grooves on warped audio clips (the reference commits them as warp markers).
- Hot-swap, saving a groove to a file, and a default groove for new clips.
- Dragging clips or grooves into the pool.
- The control bar's Global Amount slider.
- Measured comparison with the reference's placement arithmetic. See [open questions](../open-questions.md#groove-defaults).
