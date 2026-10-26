# Performance capture contract

As in the reference, pressing Arrangement Record while Session clips play records the performance into the Arrangement.

## Behaviour

- **Starting.** `Session::record` is allowed with no track armed when a Session clip is playing or queued; the transport records for the performance.
- **The start time** is where the engine has played up to, from its sync point, or the insert marker when the transport was stopped. A clip already playing joins there from the point it has reached, with its played beats wrapped into its loop.
- **Changes.** Every launch and stop through Halftrack's launch commands records the track's change at its exact launch boundary: slots, scenes, groups, Stop buttons, Stop All Clips and Session takes.
- **Stopping** at the engine's played time writes each track's clips into the Arrangement:
  - each played clip becomes a copy, with new clip and note identities, from its start to the next change or the stop;
  - the Arrangement material in that range is split out and replaced, where it can be split.

  All of it joins the take's single "Record take" undo step.
- **Clip automation** (2026-10-26). A played Session clip's automation envelopes are written into its track's lanes over the span it played, as the manual says Session clip automation is always recorded to the Arrangement ([clip envelopes](clip-envelopes-contract.md)).

## Evidence

The launcher suite's `performanceTests` runs two MIDI tracks offline with the engine player at 120 BPM on a beat grid. It checks:

- that recording is refused with nothing armed and nothing playing;
- launching a scene, recording half a beat in, launching another clip on A and stopping B on later beats;
- that A holds the old clip's first quarter second, scene 1's clip from 0.25 s with a 0.25 s offset to 1 s, scene 2's clip from 1 s to the stop at 2.25 s, then the rest of the old clip;
- that B holds its clip from 0.25 s to its stop at 2 s;
- one-step undo and redo.

Missing: follow-action transitions, Gate and Repeat retriggers, legato phase, loop-brace wrapping, recording mixer and device moves without Automation Arm, and replaying the capture against the live performance's render.
