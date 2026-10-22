# Browser preview contract

Halftrack follows the reference's browser preview, described in [Working with the Browser: Previewing Files](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), consulted 2026-10-21. With the Preview switch on at the bottom of the browser, selecting a file plays it, and the arrow keys step through files previewing each. The volume is set separately from the mix.

Halftrack plays a file as the reference's **Raw** mode does: once, at its own tempo. The reference's default, starting at the next bar in the project's tempo and looping, is not implemented.

## Behaviour

- **Playing.** `previewFile(file)` plays an audio file through a separate preview Edit, created by Tracktion's `createEditForPreviewingFile`, on the same audio device as the project. The project, its mix, its undo history and its unsaved state are untouched, and the preview adds to the project's output when both play.
- **Replacing and ending.** Previewing another file replaces the current one, and `stopPreview()` silences it. A preview ends at the end of its file.
- **Volume and output.** Previews play at the cue volume on Cue Out, as the reference's do ([cue contract](cue-contract.md)). `setPreviewVolume(dB)` sets the cue volume, from −70 dB (off) to +6 dB, −6 dB at first and kept with the project.
- **Refusals.** Previewing fails without an audio device, and for unreadable, missing or non-audio files.
- **Reporting.** `Snapshot::previewing` is the playing file, empty when nothing is playing. `Snapshot::previewVolumeDb` is the preview volume.

## User interface

The Samples browser shows a Preview switch and a preview volume slider above its folder buttons. With Preview on, selecting a sample, by click or arrow key, previews it. Selecting a folder, or turning Preview off, stops the preview.

## Evidence

`tests/PreviewTests.cpp` (`preview-acceptance`) checks through the synthetic device:

- **Level.** A 0.5-amplitude tone previews at 0.5 while the project stays untouched.
- **Volume.** A preview starts at the cue volume of −6 dB, plays at the file's level at 0 dB, −6 dB scales it again, and out-of-range volumes are refused.
- **Ending, replacing and stopping.** The preview stops at the end of its one-second file, a new file replaces the old one, and Stop silences it.
- **Refusals.** Text and missing files are refused, and an offline Session cannot preview.
- **Mixing.** A preview adds to a playing project.

The UI suite checks that the switch and volume appear only in the Samples browser, and that the slider sets the preview volume.

## Not implemented

- Tempo-synced, looping preview, which is the reference's default with Raw off.
- The Preview tab's waveform and scrubbing.
- Shift+Enter previewing without the switch.
- MIDI file and device preset preview.
- A separate cue output.
- Hot-swap.
