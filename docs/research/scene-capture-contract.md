# Capture and Insert Scene

Status: partial, SES-001.

The reference's Capture and Insert Scene (Create menu, Cmd/Ctrl+Shift+I) makes a new scene from what plays in the Session View. Halftrack's `Session::captureAndInsertScene (afterSceneIndex, sceneId)` does the same; the behaviour comes from the reference's manual.

## Behaviour

- **The scene.** A new scene is inserted after the given one, or at the end. It holds a copy of each ordinary, unfrozen track's playing Session clip, one per track. The copies are clips of their own, with new identities for them and their notes.
- **Refusals.** Nothing playing (or the transport stopped) is refused, as is a full scene list of 256.
- **Undo.** It is one undo step.

The Session menu offers it while the transport plays; Cmd/Ctrl+Shift+I runs it.

## Evidence

`tests/SceneCaptureTests.cpp` gives two MIDI tracks clips in two scenes, each holding a note of its own pitch. It checks:

- with nothing playing, nothing is captured;
- playing the first bass clip and the second keys clip, capturing after the second scene inserts a third holding copies of exactly those clips, with their own identities;
- launched, the captured scene plays both;
- one undo removes it, leaving the rest.

The UI suite checks that Cmd/Ctrl+Shift+I says when nothing plays.

## Not yet covered

- The reference's handover of playback to the captured scene's clips at once.
- Capturing the playing tempo and meter into the scene.
- Consolidate Time to New Scene.
