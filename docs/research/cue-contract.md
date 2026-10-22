# Cue contract

Halftrack's cue follows a static reading of the reference's code and bundled default Set (Ableton Live 12.2.1, arm64; raw material in the ignored `artifacts/reverse-engineering/cue/`). It also follows the reference's manual, consulted 2026-10-22: Cue Out "must be set to an output other than that selected for Main", and previews are heard on the cue output.

## What the reference does (static findings)

- **The cue bus.** A hidden PreHear track whose mixer volume ("Preview Volume", the master's cue knob) is the cue level, saved per Set. The default Set holds it at −6 dB. The metronome and the browser's preview tracks are routed into it, so they always play at the cue level on Cue Out, in both modes.
- **Cueing is pre-fader listening.** A cued track feeds the cue from after its devices and before its volume, pan, group volume and crossfader, at unity and unpanned. Its activator gates only its main output and sends, so a cue is heard with the activator off. A track's sends are not in its cue. The Main has no cue switch.
- **One switch.** A track's Solo and Cue switch is one saved flag, read by the Solo/Cue mode. Switching modes keeps the flags, so solos become cues and back. Exclusive Solo and Cmd/Ctrl-click apply to cue switches too, and an exclusive click clears every other switch, so a plain click on an engaged switch turns them all off.
- **When Cue mode applies.** Cue mode needs a Cue Out sharing no channel with Main Out. Otherwise the mode is forced to Solo and the switch is disabled, and the saved choice comes back when the outputs separate. Cue Out starts on the same outputs as Main.
- **Undo.** The mode and the switches are not undo steps; Cue Out and the cue level probably are (medium confidence).

## Solo in Place

The reference keeps Solo in Place with the Set, on at first and not an undo step, and offers it as a menu command (static reading). With it on, soloing tracks keeps the returns audible, fed by the soloed tracks' sends, unless a return is soloed, which mutes the others. With it off, soloed tracks are heard without the returns.

Halftrack does the same: `Session::setSoloInPlace`, kept with the project and not an undo step, from the View menu's Solo in Place (where Halftrack keeps the reference's Options items). `tests/SendReturnTests.cpp` renders, sample by sample:

- a soloed source heard with its return, and without it once Solo in Place is off;
- that undo then takes back the solo, not the switch;
- that soloing a return beside a source mutes the other return.

The UI suite switches it from the View menu.

## Halftrack

- **Cue Out** (`Session::setCueOutput`) names an output switched on in the Output Config; empty is the Master Out's, as at first. It is kept with the project and, as the Master Out is, is not an undo step.
- **The cue volume** (`setCueVolume`, −70 dB for off to +6 dB, −6 dB at first) is kept with the project and is one undo step. The Samples browser's preview volume is the same setting (`setPreviewVolume`).
- **What plays on Cue Out**, at the cue volume:
  - the click, in both modes;
  - previews, in both modes;
  - in Cue mode, the cued tracks.

  With Cue Out on the Master Out's outputs, these play there, apart from the master's volume and meter.
- **Cue mode** (`setCueMode`) is kept with the project and is not an undo step. It is refused, and the master strip's Solo/Cue switch is disabled, unless Cue Out shares no channel with the Master Out. If they come to share one, Solo applies again and the choice is kept for when they no longer do.
- **The switches.** Each track's switch solos it in Solo mode and cues it in Cue mode, keeping its state between the modes (`Snapshot::TrackInfo::solo` reports it in both). A cued track plays on to its own output as before.
  - Exclusive Solo applies to cueing. A plain click on an engaged switch clears every switch, in both modes.
  - Groups cannot be cued: their switch is disabled in Cue mode.
- **The engine.** A patch (`cmake/PatchCue.cmake`, see [dependencies](../dependencies.md#tracktion-cue-patch)) gives the engine behaviour three hooks: whether a track is cued, the cue's output device, and the cue level, which is read on the audio thread from an atomic. On the cue's device, the node builder sums the cued tracks' Post FX taps (the tap Audio From uses, before the volume plug-in and the track's muting) and the click, each through a gain node that ramps the cue level across a block and puts a mono tap on every channel. The sum goes after the master's devices and meter.
- **The UI.** The Session mixer's master strip has Cue Out above Master Out, the Solo/Cue switch (reading Solo or Cue) and a CUE knob. In Cue mode the tracks' switches read C.

## Evidence

`tests/OutputRoutingTests.cpp` (`output-routing-acceptance`) plays a 0.5 sine on a track at −12 dB, panned hard left, through a synthetic device with four outputs, and measures each channel. It checks:

- the defaults, and that Cue mode is refused without a separate Cue Out;
- with Cue Out on outputs 3 and 4 and Cue mode on, cueing the track leaves its master outputs exactly as before and plays it on outputs 3 and 4 at the sine's own level: before the fader and pan, on both sides;
- with its activator off, Cue Out still hears it and the master does not;
- that the cue volume at −6 dB halves the cue alone, and is one undo step;
- that in Solo mode the same switch solos the track and Cue Out is silent, and Cue mode cues it again;
- that moving the Master Out onto Cue Out puts Solo back, keeping the choice and the switch, and moving it away restores Cue mode;
- that a group's switch is refused in Cue mode;
- that the click plays on Cue Out in both modes, and a preview plays there at the cue volume;
- that Cue Out, the cue volume and Cue mode are kept with the project, and a new project has the defaults.

`tests/MetronomeTests.cpp` compares a click at the cue volume of −6 dB, sample by sample. `tests/PreviewTests.cpp` checks that previews start at −6 dB and follow the cue volume. The UI suite uses the master strip's Cue Out, Solo/Cue switch and CUE knob, sees the tracks' switches read C in Cue mode, and checks that a plain click on a soloed track releases every solo.

## Not yet

These are recorded in the [open questions](../open-questions.md#cue):

- cueing groups;
- undo for Cue Out, and no undo for the switches, as the reference probably has them;
- the reference's ramp length, and the cue of a MIDI track's own MIDI.
