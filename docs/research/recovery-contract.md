# Crash recovery contract

Unsaved work is copied every 30 seconds and offered back after a crash, after the reference's crash recovery ([Managing Files and Sets](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/), consulted 2026-10-20). This is Halftrack's own implementation. The recovered project opens untitled, so nothing is written over a saved project without a Save As.

## Interface

- `writeRecovery(file)` atomically writes the open project, with media at their absolute paths and the saved project's path as `halftrackRecoveryOf`. It does not collect media, change the project's file association, its unsaved state or its history, and it is refused while recording or exporting.
- `recoverProject(file, recoveredFrom)` validates and opens a copy like `loadProject`. The result is untitled, has unsaved changes and no history, and reports the project it came from (or none). A damaged copy is rejected, and the open project stays.
- `MainComponent::enableRecovery(file)`:
  - The app passes `<application data>/Halftrack/Recovery/Recovery.halftrack`. Tests pass their own file, so they never touch it.
  - Every 30 seconds unsaved work is copied, and a saved or new clean project removes the copy.
  - If a copy exists when recovery is enabled, Halftrack asks to Recover (open it as untitled work) or Discard (delete it). The copy is kept while the prompt is open.
  - Quitting removes the copy once the save prompt has been answered with Save or Discard.

## Evidence

`tests/RecoveryTests.cpp` checks:

- a recovery copy of an untitled project with audio, notes, a Sampler and a mixer change;
- that writing it leaves the project untitled, unsaved and with its history;
- that the recovered project in another Session is untitled and unsaved, has no origin and renders identically;
- for a saved project changed afterwards, that writing a copy leaves the saved file byte-for-byte unchanged, and recovery reports the original while keeping the unsaved change;
- rejection of a truncated copy.

The UI suite checks:

- the autosave tick writing a copy, and saving removing it;
- a leftover copy being offered, and surviving the tick while the prompt is open;
- Recover opening the work untitled and unsaved;
- Discard deleting the copy.

## Not implemented

- Several recovery copies, for example from several windows.
- Recovering recordings that were in progress.
- Recovery of an unsaved project's media when the scratch folder has also been lost.
