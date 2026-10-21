# Missing media contract

As in the reference, a project whose audio files or samples have moved still opens. The missing media is listed, and a search relinks it, much as the reference's File Manager does.

## Behaviour

- **Opening** keeps the reference of an audio clip, or a Sampler or Drum Sampler sample, whose file is not on disk; it does not refuse the project. Such a clip is marked `mediaMissing` and plays silence. A sample file that exists but cannot be read is still refused, as before.
- **`Session::missingMedia`** lists every missing clip file and sample.
- **Saving** writes a missing file's reference relative to the new project file, pointing where the file was, and collects everything that exists.
- **Locating.** `Session::locateMissingMedia (folder, found)` searches the folder and its subfolders, up to 200,000 files. It matches by file name, case-insensitively, and uses the first readable audio file for each name. It relinks clips and samples, restarts playback and marks the project changed, without an undo step.
- **The UI:**
  - File > Locate Missing Media picks the folder and reports how many files it found;
  - missing clips are dimmed and labelled with the file's name;
  - opening a project reports how many media files are missing.

## Evidence

`tests/MissingMediaTests.cpp` saves a project with an audio clip and a Sampler sample, moves its Assets folder away, and reopens the project in a fresh session. It then checks:

- that both files are reported missing, without the project being marked changed;
- that the render is silent;
- that saving elsewhere keeps the clip's reference;
- that a missing folder is refused, and a non-audio decoy with the right name is ignored;
- that searching the files' new home relinks both, marks the project changed without an undo step, and renders within 1e-6 of the original;
- that saving collects the files again.

Missing: matching by size or checksum, the reference's File Manager views, and relinking files that move while the project is open.
