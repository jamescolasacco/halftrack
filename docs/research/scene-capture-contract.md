# Capture and Insert Scene

Status: partial, SES-001.

The reference's Capture and Insert Scene (Create menu, Cmd/Ctrl+Shift+I) makes a new scene from what plays in the Session View. Halftrack's `Session::captureAndInsertScene (afterSceneIndex, sceneId)` does the same; the behaviour comes from the reference's manual.

## Behaviour

- **The scene.** A new scene is inserted after the given one, or at the end. It holds each ordinary, unfrozen track's playing Session clip, one per track.
- **Playback.** The new scene plays at once with no interruption, as the manual describes. The scenes the clips came from keep copies of them, with new identities for the copies and their notes.
- **How playback carries on.** The reference describes copies placed in the new scene. Halftrack instead moves each playing clip's slot into the new scene and leaves the copy in the old one. The engine keeps a slot's playback across graph rebuilds, so nothing restarts and held notes sound on. Launching the copies in phase would cut held MIDI notes and add the engine's 40-sample stop fade to audio. The one visible difference is that the clips in the new scene keep their original identities.
- **Refusals.** Nothing playing (or the transport stopped) is refused, as is a full scene list of 256.
- **Undo.** It is one undo step.

The Session menu offers it while the transport plays; Cmd/Ctrl+Shift+I runs it.

## Evidence

`tests/SceneCaptureTests.cpp` sets up three tracks at 48 kHz in 128-sample blocks:

- two MIDI tracks with clips in two scenes, each clip holding a whole-clip note of its own pitch;
- an audio track playing a 330 Hz sine read from memory.

It checks:

- with nothing playing, nothing is captured;
- playing the first bass clip, the second keys clip and the sine, then capturing after the second scene:
  - inserts a third scene holding exactly those clips;
  - the third scene plays at once and the old scenes stop;
  - the old scenes hold copies with their own identities;
- one undo removes the captured scene, and the clips play on in their own scenes; redo captures again;
- the same performance rendered with and without the capture differs by 0.0 at worst over the 0.4 s that follow, while the held notes and the sine sound.

The UI suite checks that Cmd/Ctrl+Shift+I says when nothing plays.

## Not yet covered

- Capturing the playing tempo and meter into the scene.
- Consolidate Time to New Scene.
