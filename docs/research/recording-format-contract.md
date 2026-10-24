# Recording format contract

Halftrack's recording file type and bit depth follow the reference's recording preferences, which choose the file type and bit depth that new audio takes are written in.

## Behaviour

- **Choosing.** `setRecordingFormat ({ type, bitDepth })` sets WAV, AIFF or FLAC at a bit depth the type can hold: 8, 16, 24 or 32 for WAV and AIFF, and 16 or 24 for FLAC. Other depths are refused with the depths the type allows, and so are changes while recording. The setting is applied to every audio input, and to inputs that appear later. Each input keeps it in the engine's settings, so it lasts between launches. `recordingFormat()` reports it. WAV at 24 bits is the default.
- **The UI.** Preferences' Record, Warp & Launch page has File Type and Bit Depth choosers. A refused choice leaves the chooser on the kept value.
- **Unwritable folders.** Before recording an audio take, in the Arrangement or a Session slot, Halftrack checks that it can write into the project's assets folder. If it cannot, recording is refused with the folder and what to do. Previously recording started and the take was lost without a warning.

## Evidence

`tests/RecordingFormatTests.cpp` records the generated input through the synthetic device and checks:

- the WAV default;
- takes in WAV at 16, 24 and 32 bits, AIFF at 24 and FLAC at 16 and 24, each read back with the right extension, depth and signal;
- refusals for FLAC at 32 bits, WAV at 12 bits, and any change during a take;
- that recording into a folder made unwritable is refused with the reason and leaves nothing recording, and that recording works again once the folder is writable (outside Windows, which ignores a folder's read-only flag).

The UI suite sets AIFF at 16 bits from Audio Setup and sees a FLAC 32-bit choice refused.

## Not yet covered

Disk-full detection during a take, a recording folder outside the project, and MIDI-only takes (which write no file) are not covered.
