# Re-Pitch warping contract

Halftrack's Re-Pitch warp mode follows the reference's Re-Pitch mode. A Re-Pitch clip follows the project tempo by playing its audio faster or slower, like tape, so its pitch moves with the tempo instead of being preserved.

## Behaviour

- **Choosing.** `WarpMode::repitch` in `setAudioClipWarp`, or Re-Pitch in the clip view's warp mode chooser. Like the stretching modes it keeps the clip's length in beats, so the clip's time changes with the tempo. Each change is one undo step and it reopens.
- **Playing.** The engine resamples the clip's audio at the ratio of the project tempo to the clip's tempo, block by block, so a tempo map moves the pitch where the tempo changes. No time stretcher runs.
- **Warp markers** rest while a clip is in Re-Pitch, as they do with Warp off, and return when a stretching mode is chosen. New markers are refused in Re-Pitch.

## Evidence

`tests/RepitchTests.cpp` renders an 8 s 1 kHz sine recorded at 120 BPM and checks:

- that at 120 BPM it keeps its 8 seconds and 1 kHz;
- that at 132 BPM it lasts 120/132 of the time and sounds 1.1 kHz, while the same clip stretched keeps 1 kHz;
- that under a map holding 120 BPM to beat 8 and then ramping to 240 within a quarter beat, it sounds 1 kHz and then 2 kHz;
- that the mode is one undo step and reopens.

## Not yet covered

Warp markers in Re-Pitch, the reference's Beats, Tones, Texture and Complex Pro modes, and a comparison of the resampling quality with the reference.
