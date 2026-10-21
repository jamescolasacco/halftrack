# Clip envelopes contract

Halftrack's clip envelopes follow the reference's clip envelopes for mixer and device controls. While a clip plays, an envelope of the clip changes a control of its track.

## Behaviour

- **Targets.** An envelope names its track's mixer (volume or pan), or a control of a device directly on the track: a native device control or a plug-in parameter. Controls of devices inside racks and of other tracks are refused.
- **Modulation** moves a control from the value it would otherwise have, whether that comes from its fader or from track automation.
  - Volume modulation scales the fader position by 0 to 1.
  - Pan modulation adds −1 to +1.
  - Device modulation adds −1 to +1 halves of the control's range, so −1 takes a centred control to its minimum.
  - The control's own value and its saved project value are unchanged.
- **Automation**, for Session clips only, sets the control while the clip plays. Values are in the control's own units: decibels, pan, a native control's units, or a plug-in's 0–1. Arrangement clips modulate only.
- **Points and timing.**
  - Points are beats from the clip's content start, ordered, at most 4096, with values inside the range above.
  - A single point holds its value.
  - A linked envelope follows the clip, its position and its loop.
  - An unlinked one loops on its own length of 1 to 1024 beats from the clip's start.
  - A control can have a modulation envelope and an automation envelope at once, each with its own points and timing.
- **Editing.** `setClipEnvelope` sets one envelope as one undo step, and no points removes it. Envelopes are kept when a clip is split, duplicated, saved and reopened. A duplicated track's clips modulate the duplicate's own devices. Malformed envelope values in a project file are refused before the open project is replaced.

## Engine changes

Clip envelopes use the engine's clip curve modifiers. `cmake/PatchClipEnvelopes.cmake` fixes two faults that envelopes exposed:

- **Held values.** One-point track automation was ignored by the audio thread once a clip modulated the same control.
- **Deleted curves.** A plug-in kept for undo could read a deleted clip's curve, which crashed after undoing a track duplication.

Native devices read a modulated control's value even when no curve sets it. See [dependencies](../dependencies.md#tracktion-clip-envelope-patch).

## Evidence

`tests/ClipEnvelopeTests.cpp` renders a constant 0.5 clip at 120 BPM and checks:

- a volume modulation step at beat 4, from the whole fader to half its position (0.2027 of the level). It is undoable and redoable, moves with its clip, and is kept by both halves of a split and by a duplicate;
- the same step applied to −6 dB track automation, at half the automated position;
- full right pan modulation silencing the left channel;
- an unlinked four-beat envelope repeating where the linked one holds;
- refusals for out-of-range values, unordered points, automation on an Arrangement clip, controls off the track, unknown mixer controls, loops shorter than a beat and unknown clips;
- the EQ's Output modulated down by half its range playing at exactly −12 dB, and a duplicated track's clip modulating its own EQ;
- the controlled VST3 gain at a half, modulated down by half its range, playing at a quarter;
- reopening playing the same, and a doubled volume modulation in a saved file refused while keeping the open project;
- a Session clip automating its track's volume to −12 dB, measured live against the same clip without it.

## Not yet covered

- Clip envelopes have no editor in the clip view yet.
- Audio clips' own envelopes (clip gain, pan, transposition and sample offset) are missing.
- Sends, devices inside racks, and the reference's own envelope scaling are unmeasured.
- A Session clip's automation stops applying when the clip stops; whether the reference keeps the last value is unmeasured.
