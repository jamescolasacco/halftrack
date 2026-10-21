# Preferences contract

Halftrack's preferences follow part of the reference's Record, Warp & Launch preferences. They live in a versioned file beside the engine's settings and are edited in the Preferences dialog (File menu).

## Settings

| Setting | Default | Effect |
| --- | --- | --- |
| Exclusive Arm, Exclusive Solo | on | A plain click releases every other armed or soloed track (and, for solo, group), and Cmd/Ctrl-click adds. Off, the two swap. |
| Default Warp Mode | Complex | The mode Session audio clips are warped in, and the mode Auto-Warp uses. Off is refused. |
| Auto-Warp Long Samples | off | Arrangement imports longer than 30 seconds are warped at their estimated tempo. |
| Default Launch Mode, Default Launch Quantization | Trigger, Global | What new Session clips (imported, created or recorded) launch with. |

An import that the preferences warp is still one undo step.

## The file

- **Saving.** Preferences are written atomically with a `version` attribute (currently 1).
- **Missing file.** The defaults apply.
- **Older file.** Its settings are kept, and settings it lacks get their defaults.
- **Newer file.** Its unknown settings are ignored.
- **Unreadable file.** A file that cannot be read, or that holds an out-of-range value, is moved aside as "Halftrack Preferences (unreadable).xml" unchanged. The defaults apply, and the app reports the reason when it starts.
- **Reset.** Reset to Defaults writes the defaults.

## Evidence

`tests/PreferencesTests.cpp` checks:

- **The defaults** with no file, and a change saved with its version and read by the next Session; Off is refused as the warp mode.
- **New Session clips:** an imported Session audio clip warps in Beats and launches as Gate on the bar, the import is one undo step, and a new Session MIDI clip launches as Gate.
- **Auto-Warp:** a 40-second sample warps in Beats at its estimated tempo while a 10-second one does not.
- **Reset** persists.
- **Other files:**
  - an older file keeps its one setting and gains the defaults;
  - a newer file's unknown setting is ignored;
  - an out-of-range value, and a file of junk, are set aside with the defaults kept and the reason given.

The UI suite switches Exclusive Solo off in the Preferences panel, sees a plain click add a second soloed track, and resets.

## Not yet covered

- The reference's other preferences (Look/Feel, File/Folder, Library, Plug-ins, and the rest of Record, Warp & Launch) are missing.
- Migration between platforms is untested on Windows and Linux machines.
