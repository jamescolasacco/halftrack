# Audio warping contract

Warped audio clips follow the project tempo with their length fixed in beats, as in the reference workflow ([audio clips, tempo and warping](https://www.ableton.com/en/live-manual/12/audio-clips-tempo-and-warping/), consulted 2026-10-20). Algorithms and defaults below are Halftrack's.

`Session::setAudioClipWarp(clipId, { mode, bpm })` sets `WarpMode::off`, `stretch` (Signalsmith default preset) or `efficient` (Signalsmith cheaper preset) and the source's own tempo (20–999 BPM). The engine's auto-tempo stores the source length in beats (`bpm × sourceSeconds / 60`), so tempo changes remap the clip's start, end and offset on the beat grid; unwarped clips keep their seconds. Warping at the current project tempo keeps the visible length; changing the clip tempo rescales it; turning warp off returns to source seconds and renders bit-identically to the original unwarped clip. Session slot clips always follow the tempo (warp off is rejected) and rescale their beat loop when their tempo changes. Reversed or grouped clips must be unreversed/ungrouped first; source-time trimming, splitting and copying remain limited to unwarped Arrangement clips. `Session::estimateClipTempo` assumes whole 4/4 bars doubled or halved into 80–160 BPM; snapshots report it for unwarped clips.

Evidence (`tests/WarpTests.cpp`): a generated click track with one 1 kHz burst per 120 BPM beat renders with every onset within about 1 ms of the 60/80/90/100/180/240 BPM grid and a preserved 1 kHz pitch; tempo-estimate cases; unity-ratio, faster-source and efficient-mode grids; exact unwarped restoration; undo/redo; save/reopen of mode, tempo and beat length; beat-position preservation of a late clip; Session-slot rules; out-of-range tempo and source-time edit rejection. Measuring this exposed the engine priming/drift defects fixed by [the stretch alignment patch](../dependencies.md#tracktion-time-stretch-alignment-patch).

Not implemented at first: graphical warp-marker and segment editing, transient-based Beats mode, Re-Pitch, Tones/Texture/Complex Pro variants, automatic warping of long samples on import, tempo detection from audio content, and any comparison with reference stretch output. The sections below cover what has been added since.

## Warp markers (2026-10-21)

A warp marker pins a point of a warped clip's source to a beat, counted from the source's start at the clip's own tempo, as the reference's markers pin samples to the beat grid. `Session::setAudioClipWarpMarkers (clipId, markers)` replaces a clip's markers.

- **Validity.** Markers must increase in both source time and beat and lie within the source; at most 1024. An unwarped clip takes none, and an empty list removes them.
- **The start.** The engine always keeps a start marker at source 0, beat 0; `ClipInfo::warpMarkers` lists it first.
- **Between markers** the audio stretches to fit. After the last marker it keeps the clip's tempo up to the source's end, through a Halftrack end marker that is not listed.
- **Engine times.** A marker's engine time is its beat × 60 / clip BPM; Halftrack writes the engine's marker tree directly.
- **History.** Edits are one undo step and reopen with the project.
- **Warp off.** Turning Warp off rests the markers, and turning it back on restores them.
- **Timing.** An engine patch fixes the warp reader's timing; see [dependencies](../dependencies.md#tracktion-warp-marker-timing-patch).

Evidence, from the warp suite on a 120 BPM click track:

- markers must be valid;
- clicks follow a nudge that pins source second 2 to beat 3.8 within 0.35 ms;
- a 2x squeeze that pins source second 1 to beat 1 places every click within 3.3 ms, with its 1 kHz pitch kept;
- undo steps back through the edits;
- the markers reopen, rest while Warp is off and return;
- removing the markers puts the clicks back on the beat grid.

The clip view draws a warped clip in its beats, each segment of the warp map over its own beats, with a yellow handle at each movable marker:

- **Adding.** Double-clicking the waveform adds a marker at that beat, pinned to the source point already playing there, so nothing moves until it is dragged.
- **Removing.** Double-clicking a marker removes it; removing the last movable marker removes the markers altogether.
- **Dragging** a marker moves its beat, on a grid that follows the zoom (the coarsest power-of-two fraction of a beat at least 12 pixels wide) unless the grid is bypassed. Each drag is one undo step. Since 2026-10-26 a drag moves the selected markers within the reference's 5–999 BPM limits ([marker selection](#marker-selection-moves-and-seg-bpm-2026-10-26)).

The main UI suite checks adding, a drag onto the grid, its undo, and removal.

Missing: automatic markers at transients and quantizing markers (see [Warp commands](#warp-commands-2026-10-22-revised-2026-10-26) for what the reference's code shows of them).

## Tempo and transient analysis (2026-10-22)

The reference reads an audio file's transients and tempo when it is first used. It shows the transients in the clip view's sample display, and Auto-Warp Long Samples warps a long sample at its tempo, from its first downbeat. Halftrack's analysis is its own. No reference analysis file (`.asd`) was read.

**The estimate.** `tempo::estimate` (`src/TempoDetection.*`) works in five steps:

1. **Onset curve.** It measures how much a 1024-point spectrum rises at each 10.7 ms step, on compressed magnitudes, less its mean over 0.2 s. A second curve does the same below 200 Hz.
2. **Rough tempo.** The onset curve's autocorrelation is read as a comb at half a beat and at 1, 1.5, 2, 3 and 4 beats, for every tempo from 80 to 160 BPM in 0.05 BPM steps. A tempo outside that range shows as its double or half.
   - The comb's peak over its median is the confidence.
   - Below 1.3, the sound has no steady beat, and nothing is returned. So does a sound shorter than 7.5 beats at 80 BPM.
3. **Beat grid.** Within 1.5% of the rough tempo, in 0.01 BPM steps, it finds the period and phase at which the onsets of the whole sound pile up most. The bass curve weighs as much as the full one, so kicks rather than hats mark the beat.
4. **Exact tempo.** Twice over, a least-squares line runs through the transients within 20 ms of the grid's beats. These are the transients Beats warping finds. The tempo is rounded to 0.01 BPM.
   - **Whole bars.** A sound that holds a whole number of 4/4 bars, up to 16, takes the tempo that fills them exactly. This applies when the bar count falls within six standard errors of the line's slope, at least 0.05% and at most 1.5%.
5. **First beat.** The first grid beat whose onset reaches a quarter of the 90th-percentile beat, moved onto the transient within 30 ms.

**On import.** With Auto-Warp Long Samples on, a sample longer than 30 s is analysed once, up to its first ten minutes.

- **With a tempo found:**
  - the clip's start is moved to the first beat;
  - the clip is warped at the found tempo in the default warp mode;
  - outside Re-Pitch, a warp marker pins that beat at the same tempo, so nothing moves until it is dragged.
- **With no steady beat:** the clip takes the whole-bar length guess, as before.

The import and its warping are one undo step. Shorter samples keep the length guess, as the reference takes short samples for whole-bar loops.

**Transients.** `Session::audioClipTransients (clipId)` lists the transients in a clip's source, in source seconds. They are read once while the file is unchanged.

- The clip view marks them along the top of a warped clip's waveform.
- Double-clicking within 5 pixels of one pins a new warp marker exactly on it; elsewhere, a double-click adds a marker as before.

**Evidence.** `tests/TempoAnalysisTests.cpp`:

- **Generated drum loops.** Twelve loops at 84.5–152 BPM, at 44.1, 48 and 96 kHz, with lead-ins, swing and a held chord, are each cut to a length whose whole-bar guess is wrong. Each is found within 0.05 BPM (all exactly) with its first beat within 2 ms. Loops at 70 and 172 BPM give 140 and 86.
- **A three-minute song** at 126.5 BPM with an eight-bar chord intro is found exactly, its first beat within 0.6 ms, in 0.16 s.
- **No tempo.** Noise, a held tone and a four-second loop give none.
- **File reads.** A file read a block at a time gives the same estimate.
- **Import.** With Auto-Warp on, a 40-second loop at 97 BPM after 0.6 s of silence:
  - is warped at 97.00 BPM, not its length's 96;
  - has a marker and its start on the first beat;
  - lists its transients on its beats;
  - renders in Beats with its next 15 beats within 0.02 ms of the project's 120 BPM grid and its first at the start;
  - is undone in one step.
  Forty seconds of noise take the length guess with no marker.
- **The UI suite** checks a click fixture's three transients and a double-click 3 pixels beside one pinning a marker on it.

**Measured on reference material.** `--tempo-evaluate <folder> [report.json]` reads the reference's Core Library loops whose names give their tempo; there were 169. Results (macOS arm64, 2026-10-22; the report is kept in ignored `artifacts/reverse-engineering/tempo/`):

- **The 69 loops of 6 s or more:**
  - the estimate finds 54 (78%) within 0.5 BPM;
  - 8 beatless pads give none;
  - 5 give a related tempo (2/3, 3/4 or 4/3 of it, from triplet, shuffled or accented patterns);
  - 2 give another.
- **The length guess** finds 65 of the 69 (94%), as these loops are cut to whole power-of-two bars.
- **Shorter loops** have too few beats to analyse.
- **Speed:** 6.8 ms a loop.

Missing: downbeat (bar) detection, tempo that varies through a recording, keeping analyses with the file between sessions, and a side-by-side comparison with the reference's own auto-warp results.

## Warp commands (2026-10-22, revised 2026-10-26)

Right-clicking a warped clip's waveform offers the reference's warp commands, after a static reading of its code on 2026-10-26 (Live 12.2.1, arm64 slice, sha256 b37cb738442bba7787457fe751b0e3f9fbdabc662d8d491d1d9aa46b120bd3fc; binary-assisted research, raw notes in the ignored `artifacts/` folder).

**The point** a command works from is the right-clicked warp marker's beat, or the beat under the pointer on the clip view's grid (unless the grid is bypassed), as the reference's menu takes it.

**Warp From Here (Straight).** `Session::warpFromHere (clipId, sourceSeconds)` takes the audio from the point as one steady tempo, found from the content after the point with the same estimate as Auto-Warp, and put on the reference's grid of 0.02 BPM.

- **Markers.** The clip keeps its markers before the point and takes one exactly at the point, as the reference's straight warps place it. The point keeps the beat where it already plays, so nothing before it moves. Markers after the point go.
- **Tempo.** From the point the clip plays at the found tempo. It keeps its place and length, ending with its audio if that now comes sooner.
- **Refusals.** It needs a stretching warp mode, a point at least a second before the audio ends and after the earlier markers, and a steady beat from there. It is one undo step.

**Warp N BPM From Here.** `warpFromHere (clipId, sourceSeconds, bpm)` does the same at exactly the given tempo, without looking at the audio. The menu gives it the project's tempo, labelled with two decimals ("Warp 120.00 BPM From Here"), as the reference's does.

**Warp as N-Bar Loop.** `Session::warpAsLoop (clipId, count, inBeats)` takes the whole sample as that many bars of the project meter (or beats), with no markers, and loops it from its start, as the reference's does. The menu offers the length the sample suggests (`Session::loopLengthFor`, from the whole-bar guess in 80–160 BPM), rounded as the reference rounds it: 1, 2, 4 or 8 bars, then multiples of 8 up to 64, then whole bars, and in beats when that is shorter than a bar ("Warp as 2-Beat Loop"). It takes 1 to 256 bars or 1 to 1024 beats, refuses a tempo outside 20–999 BPM, and is one undo step.

**Undo.** The engine's warp marker list was made as an undoable edit the first time a clip took markers, so undoing that edit left the engine holding a list the clip no longer had, and the clip's next marker edit failed. The list is now made outside the undo history.

Evidence (`tests/TempoAnalysisTests.cpp`):

- A loop cut to four bars at 105 BPM, warped at 120 and then Warp as 4-Bar Loop, plays at 105 BPM over 16 beats, looping them; as an 8-beat loop it plays at 52.5 BPM.
- The loop lengths offered: 16 beats as 4 bars, 3 beats as 1 bar, 2.5 beats as 2 beats, 1.2 beats as 1 beat, 6 and 13 bars as 8, 40 bars as 40 and 70 bars as 70.
- 3.2 s of noise and then a loop at 97 BPM, warped at 120 in Beats, then Warp From Here (Straight) at the loop's first hit:
  - it finds 97 BPM on the 0.02 BPM grid and pins the point at 3.2 s on beat 6.4, where it played;
  - rendered, each later hit sounds a beat apart within 3 ms.
- Warp 100.00 BPM From Here at the same point plays exactly 100 BPM from it, after the earlier command was undone; a tempo under 20 BPM is refused.
- Unwarped clips, 0 or 300 bars, and points in the last second or before the audio are refused, and each command undoes in one step.

The UI suite checks the menu's one-bar suggestion for two seconds at 120 BPM. It also checks that Warp From Here reports finding no steady beat in two seconds of clicks, changing nothing.

Missing, from the same reading:
- Warp From Here (with tempo changes) and Warp From Here (Start at the project tempo), which need the reference's beat tracker: markers on every bar from the whole beat at or below the point.
- Set 1.1.1 Here and the menu's Quantize. Warp Selection as N-Bar Loop, Insert Warp Marker and Delete are in [the time selection](#the-time-selection-and-the-sample-displays-menu-2026-10-26).

## Marker selection, moves and Seg. BPM (2026-10-26)

After the same static reading of the reference's code, the clip view selects, moves and deletes warp markers as the reference's sample display does.

- **Selecting.** A click on a marker selects it alone.
  - Cmd-click (Ctrl on Windows and Linux) adds or removes one.
  - Shift-click, with markers selected, selects the run from the clicked marker to an anchor. The anchor is the end of the selection farther from the click, and further Shift-clicks keep it.
  - A click on a marker already selected selects it alone at the release, unless the selection was dragged, so several markers drag together.
  - A click on the waveform away from the markers, or Escape, deselects them. Showing another clip does too.
  - A marker made by double-clicking is selected, or added to the selection with Cmd or Shift. Selected markers are drawn white.
- **Dragging.** Pressing a marker and moving more than 3 pixels drags the selected markers by the beats the grabbed one moves, on the grid unless the grid is bypassed. Shift at the press does not drag them; the reference then moves the waveform, which was not read. Each drag is one undo step.
- **Moves.** `Session::moveWarpMarkers (clipId, sourceSeconds, deltaBeats)` moves the markers at those source seconds, keeping their audio.
  - As the reference's limiter does, from the first moved marker to the last, each one beside a marker that stays cuts the move so the segment between them spans 5 to 999 BPM.
  - A move cut to nothing changes nothing. The start at (0, 0) does not move.
- **Keys**, with the clip view focused, as a click gives it:
  - Left and Right move the selection to the next grid line from its rightmost marker, or by a pixel's beats with the grid's snapping off.
  - Shift+Left and Right add the marker before or after the selection.
  - Cmd+Left and Right select that marker alone, wrapping round the ends; with several selected, they leave the leftmost or rightmost.
  - Delete and Backspace delete the selected markers. With none selected they delete nothing else from the clip view.
- **Deleting.** `Session::deleteWarpMarkers (clipId, sourceSeconds)` deletes markers in one undo step, as the reference's Delete does.
  - The reference deletes back to front, and deleting the last marker puts its end marker after the new last one, at the tempo up to the deleted marker.
  - So after a run of deleted markers at the end, the audio after the new last marker keeps the tempo it had up to the first of them. That becomes the clip's tempo, within Halftrack's 20–999 BPM.
  - The clip keeps its place, length and loop in beats. Double-clicking a marker deletes it the same way.
- **Seg. BPM** shows the tempo of the segment up to the marker selected last: 60 times its beats over its seconds.
  - Typing or dragging a value moves that marker alone. Its segment takes the tempo (5–999 BPM) as far as the next segment's 5 to 999 BPM allow.
  - With no marker selected, it shows and sets the clip's tempo after its last marker, as before.
- **:2 and ×2** halve and double the clip's tempo: halved, the clip plays twice as fast over half the beats. Their tooltips had said the opposite.

Differences from the reference:
- Halftrack's start marker at (0, 0) is fixed and not drawn. The reference's first marker, whose Seg. BPM is the tempo after it, corresponds to Halftrack's empty selection.
- A click on the reference's marker moves the insert marker there; Halftrack's leaves the insert marker where it was. The time selection came after this section ([below](#the-time-selection-and-the-sample-displays-menu-2026-10-26)).
- Marker beats are not rounded to the reference's 1/3843840 beat.
- Not implemented: transient pseudo markers (shown on hover, converted by Cmd-dragging), Shift-dragging the waveform or a transient, and audio preview while the arrow keys move markers.

Evidence:
- `tests/WarpTests.cpp`, on markers every second at 120 BPM:
  - a one-beat move moves one marker, in one undo step;
  - moves are held at 5 BPM after and before a marker, and at 999 BPM with nothing after the last marker;
  - a move cut to nothing adds no undo step;
  - two markers moved together keep the segment between them;
  - the start does not move.
- Deleting, on segments of 120, 180 and 60 BPM:
  - deleting a marker before others keeps the clip's tempo;
  - deleting the last two leaves 180 BPM after the remaining marker, in one undo step, and the clip keeps its 8 beats;
  - rendered, the clicks after the marker sound 0.75 s apart within 1 ms. They are 6.5 ms early, evenly, as the stretcher places clicks it slows;
  - undo restores the markers and the tempo.
- The UI suite, on the click fixture:
  - a new marker is selected and Seg. BPM shows 120.00; typing 60 moves the marker so its segment plays at 60 BPM, the clip's tempo staying;
  - Right moves the marker to the next grid line;
  - a Cmd-double-click adds a second marker to the selection, and a click without a drag selects one alone;
  - Shift+Right adds the next marker, and Cmd+Right leaves the rightmost and then wraps to the first;
  - Escape deselects;
  - with both selected by Cmd-clicks, a drag far left stops the first where its segment from the start plays at 5 BPM, keeping their spacing, in one undo step;
  - Delete deletes both in one undo step, the clip's tempo staying 120, and with none selected deletes nothing.

## The time selection and the sample display's menu (2026-10-26)

After the same reading, a warped clip's sample display has the reference's time selection and insert marker, and its menu its marker and loop commands.

- **Selecting.** A drag on the waveform, away from the markers, selects time on the grid unless the grid is bypassed, and deselects the markers without Cmd or Shift. A click places the insert marker, drawn while the view has the keys. A Shift-click extends the selection from its far end. Showing another clip ends it.
- **Keys:**
  - Cmd+I inserts warp markers (`insp`): at each transient in the selection without a marker, or else at the selection's start and end, or the insert marker without a selection, each within the sample and free of a marker; the new markers are selected;
  - Delete deletes the selected markers, or else those in the selection;
  - Return selects the markers in the selection;
  - Cmd+L loops the clip over the selection, keeping a start inside it; with no selection it switches the loop off or on;
  - Escape deselects the markers, and then ends the selection.
- **The menu**, in the reference's order with the commands Halftrack has: Insert Warp Marker and Delete when they would act, the loop's command (Loop Selection, or Activate Loop or Deactivate Loop), then the warp commands. A right-click outside the selection ends it and moves the insert marker there, as the reference's does.
- **Warp Selection as N-Bar Loop** replaces Warp as N-Bar Loop when there is a selection. `Session::warpSelectionAsLoop (clipId, fromSeconds, toSeconds, count, inBeats)` plays the selected audio as that many bars (or beats), looped, at the tempo that fits it, at most 999 BPM, with no markers, in one undo step. The length offered is the whole-bar guess for the sample, in the selection's share of it, rounded as Warp as Loop's is.
  - The reference puts the selection's start at 1.1.1 with a marker there. Halftrack's clips keep their source's start on the first beat, so its loop and start lie where the selection plays: the same audio loops at the same tempo, with the loop's position counted from the source's start. Below 20 BPM, Halftrack's lowest clip tempo, it is refused.

Evidence:
- `tests/TempoAnalysisTests.cpp`: a four-bar loop at 105 BPM, its middle two bars warped as a 2-bar loop, plays at 105 BPM looping from beat 5 in one undo step; its first half bar as one beat plays at 52.5 BPM from the first beat; a selection that would play below 20 BPM, an empty one and 0 bars are refused; undo returns the loop.
- The UI suite, on the click fixture: a drag selects time on the grid; Cmd+I marks the transient in it and selects the marker; Return selects none, and after a Shift-click extends the selection, the marker now in it; Escape deselects the marker first; Cmd+L loops the clip over the selection; Delete deletes the marker in it in one undo step; a second Escape ends the selection; a click places the insert marker, where Cmd+I puts a marker.

## First play of a warped clip (2026-10-24)

A warped clip's first play after the playback graph was built began late by its stretcher's latency, about 150 ms in Complex and half a second in Beats, in the Arrangement and the Session alike; later plays began on time. The readers prime their stretcher with the clip's first audio when the graph builds them, and in real time they read the file cache without waiting, so audio not yet cached primed them with silence, kept until they were repositioned. `cmake/PatchStretchPriming.cmake` lets the cache reader wait for that audio while the graph builds them, then read without waiting ([dependencies](../dependencies.md)). The warp modes suite plays a constant level's first Arrangement play and first Session launch in Complex and Beats and finds each sounding as the next play does, near the unwarped clip.

Seeking during playback to audio not yet cached can still begin silently for the stretcher's latency, as reading must not wait in the audio callback.

## Defaults not yet the reference's

The reference warps in Beats by default and warps long samples (static reading of its preferences; see the [preferences contract](preferences-contract.md)). Halftrack keeps Complex and leaves long samples unwarped until its Beats plays as cleanly: switching showed a 0.25 s constant clip falling silent between Beats segments when its tempo differs (Halftrack's transient loop is off by default), and a constant level overshooting its gain slightly in Beats. These are recorded in the [open questions](../open-questions.md).

