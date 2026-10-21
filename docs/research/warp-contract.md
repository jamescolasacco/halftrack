# Audio warping contract

Warped audio clips follow the project tempo with their length fixed in beats, as in the reference workflow ([audio clips, tempo and warping](https://www.ableton.com/en/live-manual/12/audio-clips-tempo-and-warping/), consulted 2026-10-20). Algorithms and defaults below are Halftrack's.

`Session::setAudioClipWarp(clipId, { mode, bpm })` sets `WarpMode::off`, `stretch` (Signalsmith default preset) or `efficient` (Signalsmith cheaper preset) and the source's own tempo (20–999 BPM). The engine's auto-tempo stores the source length in beats (`bpm × sourceSeconds / 60`), so tempo changes remap the clip's start, end and offset on the beat grid; unwarped clips keep their seconds. Warping at the current project tempo keeps the visible length; changing the clip tempo rescales it; turning warp off returns to source seconds and renders bit-identically to the original unwarped clip. Session slot clips always follow the tempo (warp off is rejected) and rescale their beat loop when their tempo changes. Reversed or grouped clips must be unreversed/ungrouped first; source-time trimming, splitting and copying remain limited to unwarped Arrangement clips. `Session::estimateClipTempo` assumes whole 4/4 bars doubled or halved into 80–160 BPM; snapshots report it for unwarped clips.

Evidence (`tests/WarpTests.cpp`): a generated click track with one 1 kHz burst per 120 BPM beat renders with every onset within about 1 ms of the 60/80/90/100/180/240 BPM grid and a preserved 1 kHz pitch; tempo-estimate cases; unity-ratio, faster-source and efficient-mode grids; exact unwarped restoration; undo/redo; save/reopen of mode, tempo and beat length; beat-position preservation of a late clip; Session-slot rules; out-of-range tempo and source-time edit rejection. Measuring this exposed the engine priming/drift defects fixed by [the stretch alignment patch](../dependencies.md#tracktion-time-stretch-alignment-patch).

Not implemented: graphical warp-marker and segment editing, transient-based Beats mode, Re-Pitch, Tones/Texture/Complex Pro variants, automatic warping of long samples on import, tempo detection from audio content, and any comparison with reference stretch output.

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

Missing: drawing and dragging markers in the clip view, automatic markers at transients, segment BPM display, and quantizing markers.
