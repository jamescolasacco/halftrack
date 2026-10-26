# Clip launch modes contract

Session clips carry the reference workflow's per-clip Launch box: a launch mode, a legato switch and a clip launch quantization that can override the project grid ([launching clips: Launch box](https://www.ableton.com/en/live-manual/12/launching-clips/), consulted 2026-10-20). The descriptions are public workflow documentation. They are not measured identity with the reference application.

## Interface

`Session::LaunchSettings { mode, quantisation, legato }` is reported as `ClipInfo::launch` for every clip. Arrangement clips report the defaults and reject edits. `setClipLaunchSettings(clipId, settings)` changes a Session clip in one undo transaction. Equal settings are a no-op that adds no history; unknown clips and out-of-range enums fail without changing state. The settings are three clip properties (`halftrackLaunchMode`, `halftrackLaunchQuantisation`, `halftrackLaunchLegato`). They are saved with the project and copied when clips are duplicated. A missing or unknown stored value reads as the default.

Launch buttons are press/release pairs: `pressSlot(track, scene)` and `releaseSlot(track, scene)`. `launchSlot` remains a plain trigger and is still what scene launches use.

| Mode | Press | Release |
| --- | --- | --- |
| Trigger | launches on the clip's grid | ignored |
| Gate | launches on the clip's grid | stops on the clip's grid |
| Toggle | launches; if the clip is playing or queued, stops it (a queued launch is cancelled) | ignored |
| Repeat | launches, then restarts the clip every grid step while held | stops on the clip's grid |

Clip quantization offers Global (the control bar setting), None, 8/4/2/1 Bars, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T and 1/32, the reference's choices in its order. The control bar's global grid offers the same, without Global (`Session::launchQuantisations` and `clipQuantisations` list them; values added for the triplets keep the earlier ones' stored numbers). The launcher suite starts a clip on an eighth-triplet grid exactly a third of a beat in, and the UI suite finds the global menu's fourteen grids in order and chooses 1/8T. Grids come from the engine's tempo-sequence quantiser, so bar grids follow the time signature. A transport loop wraps a boundary past the loop end to the first grid point inside the loop, as the global grid already does. With None, or with Global set to None, Repeat does not retrigger.

Legato starts the launched clip from the playing clip's position on that track instead of from its start. It uses the engine's `LaunchHandle::playSynced`.

## Velocity

`LaunchSettings::velocityAmount` (0 to 1, shown as 0.0 to 100.0 % in the Launch box's Velocity, 0 at first) is the reference's Velocity Amount, from a static reading of its code (findings in the ignored `artifacts/reverse-engineering/notemap/`). It is saved as `halftrackLaunchVelocity` and is one undo step with the other launch settings; amounts outside 0 to 1 are refused, and a damaged stored one reads as 0.

- **The launch's velocity.** A launch by a mapped MIDI note has velocity v, the note's velocity over 127. Every other launch has v = 1: the mouse, a key, a controller, a scene, and Repeat's retriggers of a press. `pressSlot` takes the velocity, and mapped notes pass theirs.
- **The factor.** f = v × amount + (1 − amount), in single precision.
- **Audio clips.** The clip's gain moves to f times its position on the reference's clip gain scale (101 levels from −70 to +24 dB, built as the reference builds them, in `src/LevelScale.h`), and the launch plays at that gain. At 100 % and 0 dB clip gain, velocity 100 is 5.0 dB down, 64 is 16.2 dB down, and 1 about 64 dB down; at 50 %, velocity 100 is 2.1 dB down. A clip at or below the scale's −70 dB bottom is left as it is.
- **MIDI clips.** Each note-on velocity is multiplied by f, never below 1. Note-offs are left alone.
- **How long it lasts.** The engine takes the gain and scale up as the launch starts, or as it restarts a playing clip. They last through loops and graph rebuilds until the next launch, so a mouse launch restores full volume.

The launch-modes suite renders these levels against an independent model of the scale (the research's own script), within 2e-7. It checks the MIDI velocities through a MIDI Monitor, along with the amount's defaults, undo, validation and persistence. The UI suite edits Velocity in the Launch box and undoes it.

## Implementation

Repeat uses the engine launch handle's repeat loop (`LaunchHandle::setLooping`). The loop duration is one grid step at the play head, measured from the time-signature-aware grid. No UI timer runs, and the retrigger is sample-accurate in graph processing. Any launch, stop or transport cancellation on the track clears the loop. Held Gate/Repeat buttons are tracked per track and dropped when another launch replaces them, so a stale release cannot stop a different launch.

A clip slot mapped to a MIDI note, a controller or a computer key is pressed and released the same way: by the note-on and note-off, by the controller rising above 63 and falling back, or by the key going down and up (see the [MIDI mapping](midi-mapping-contract.md) and [key map](key-map-contract.md) contracts).

The interface sends a press on mouse down over a slot's launch triangle and a release on mouse up. Enter sends a press and ignores key repeat. It sends the release when the key goes up or keyboard focus leaves the launcher. The clip view shows a Launch box: between the Clip and Sample boxes for Session audio clips, and to the left of the note editor for Session MIDI clips.

## Engine correction

Generated-signal tests found an upstream defect in the repeat loop. When a loop point fell exactly on an audio-block start, `LaunchHandle::advance` produced an empty first section. `SlotControlNode` then skipped the whole block, and the retrigger lost its first block (128 frames in the test, with a 40-sample fade-in after it). Rounding could also put the point just past the block and miss the retrigger. `cmake/PatchLaunchRepeat.cmake` makes the handle locate the loop point itself and restart the whole block when the point is on the block start within 1e-9 beats. The test's second eighth-note retrigger lands on a 128-frame boundary. Before the patch, its burst onset was 149 frames late. With the patch, all four retriggers match the first burst within 1e-4.

## Evidence

`tests/LaunchModeTests.cpp` drives accelerated 48 kHz/128-frame callbacks at 120 BPM with generated constant, step and burst sources and synchronous readers. It checks:

- defaults, undo/redo, validation, persistence and duplication;
- a quarter-note clip ignoring a one-bar global grid;
- Gate replacement and release on eighth notes;
- Toggle start/stop/cancel;
- immediate None launches and Global bar waits;
- Repeat retriggers on the eighth-note grid, with stop on release and the repeat ending after an ordinary relaunch;
- legato and non-legato phase.

UI checks cover the Launch box controls, mouse and Enter press/release, and Toggle clicks through the synthetic device.

## Not implemented

- The reference's handling of a velocity change while a launch plays (whether it ramps), before any launch, in Arrangement playback, and for follow-action launches. Halftrack's starts at the launch and is full before one. A follow-action launch, like any launch without a note, plays at full velocity (since 2026-10-26; it kept the last launch's before).
- Gate/Repeat on scene launches.
- Any comparison with reference launch timing.
