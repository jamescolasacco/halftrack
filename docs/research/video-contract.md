# Video

Status: partial, PRJ-015.

The reference imports movies into the Arrangement: a movie's sound becomes an audio clip, and its picture plays in the Video window as the transport moves. Export can write a movie with the rendered sound. Halftrack follows that workflow on macOS, through the system's AVFoundation; the behaviour comes from the reference's manual.

## Behaviour

**Import.** `Session::importVideo (movie, track, start)` takes QuickTime and MPEG-4 movies (`.mov`, `.mp4`, `.m4v`):

1. The movie is copied into the project's assets.
2. Its sound is written there as a 24-bit WAV at its own rate. A silent movie gets silence of its length, so its clip lasts as long as its picture.
3. The sound becomes an audio clip on the chosen track, or on a new track named after the movie. The clip keeps the movie and is named after it.

It is one undo step. Auto-Warp Long Samples is not applied, and a file with no picture is refused.

**Video clips.**

- **Trimming and moving.** A video clip trims, moves and splits like any audio clip, its picture following its source offset.
- **Speed.** It plays at its own speed: Warp and Reverse are refused, and the clip view's switches are off.
- **Saving.** Save and Save As collect the movie into the project's assets and store it relative to the project.

**The picture.** `Session::videoAt (seconds)` names the movie and its time at a moment: from the first active video clip there, track by track, the clip's source offset plus the time into the clip.

**Pictures in the Arrangement.** A video clip shows pictures along the top half of its body, one after another, each from the time playing under it (to the nearest quarter second). They are made from the movie on a thread of their own, kept once made, and drawn as they arrive.

**The Video window.** View > Video Window opens a native window with JUCE's video player, silent because the clip plays the sound. Twenty-four times a second:

- **Stopped**, it shows the frame under the play head.
- **Playing**, it plays along and is moved back into step when it drifts more than 0.1 s.
- **Nothing to show.** Away from video clips it shows nothing.

**Create Video.** Export Audio's Create Video choice is enabled while the Arrangement holds a video clip. It writes a QuickTime movie beside the exported audio, the same name with `.mov`:

- **Picture.** The video clip under the export's start, for the exported time, copied without re-encoding.
- **Sound.** The exported mix.
- **Replacing.** An existing movie is replaced only once the window's Replace Existing Movie choice says so.
- **Refusal.** An export starting where no video clip plays is refused.

## Evidence

`tests/VideoTests.cpp` writes a two-second, 25 fps, 64 by 48 movie whose 50 frames each show their number as six black or white stripes, with a 440 Hz tone. It checks:

- the movie's details, and frames 0, 7, 9, 31 and 49 read back exactly;
- its sound: two seconds at 0.5 amplitude, 880 zero crossings a second;
- import onto a new track named after it, both files in the project's assets, unwarped, and one undo step;
- the frame at one second, and none after the clip;
- frame 12 at 0.6 s once the clip moves to 0.5 s and starts 0.4 s in;
- Warp and Reverse refused;
- reopening, and Save As collecting the movie into the new assets;
- **Create Video**, over 0.5–1.5 s:
  - a one-second movie of frames 10 to 30 whose sound equals the exported mix sample for sample;
  - an existing movie kept unless replacing is chosen;
  - an export start with no video clip refused;
- a file with no picture refused;
- a frame's picture at 24 pixels high: 32 wide, showing frame 20.

Where movies are unsupported, the suite checks that importing one is refused with the reason.

The UI suite imports a movie through the normal import path, paints the Arrangement and sees the clip's first picture made at the clip's height, then opens View > Video Window. It checks the window shows the movie at 0.6 s when the play head is there and nothing past the clip, closes it, and sees Create Video offered in Export Audio.

## Not yet covered

- Windows and Linux.
- Warping video clips, with the picture following the warp.
- Timecode display and frame-rate snapping.
- Re-encoding the picture (codec and size choices) and exporting picture beyond one clip.
- Measured synchronisation of the window's picture during playback.
