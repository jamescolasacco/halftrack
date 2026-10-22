# Output routing contract

Halftrack's outputs follow the reference's:

- the Output Config dialog in Audio & MIDI Setup;
- a track's Audio To "Ext. Out";
- the master track's Master Out.

## Behaviour

- **Output Config.**
  - Each pair of the audio device's output channels, starting on an even channel, is one stereo output or two mono outputs.
  - Each output can be switched off, except the Master Out.
  - Odd or missing channels are refused, and changes are refused while recording.
  - The layout is kept with the device settings, so the next launch opens with it.
  - `Session::audioOutputGroups`, `setAudioOutputPairStereo` and `setAudioOutputEnabled` do this; `outputNames` lists the outputs switched on. Audio & MIDI Setup's Output Config menu offers the same.
- **Ext. Out.**
  - A track plays straight to an output with `OutputKind::external`, bypassing the master, as one undo step. The project names the output.
  - The mixer's Audio To lists "Ext. Out" and the output for each output other than the Master Out.
  - A MIDI track without an instrument has no audio to send out and is refused.
  - So are returns and grouped tracks (they play into Master or their group), and outputs switched off or missing.
  - The Master Out itself is refused: in the engine everything playing to that output is the master.
- **Master Out.**
  - `setMasterOutput` moves the master, and every track playing to Master, to another output.
  - It is kept with the project and applied when the project opens, if that output is there.
  - It is refused for an output a track already plays to with Ext. Out.
  - The master strip in the Session mixer has a Main Out chooser (the UI uses the reference's name, Main, for the master track).

## Evidence

`tests/OutputRoutingTests.cpp` (`output-routing-acceptance`) plays a 440 Hz tone through a synthetic device with four outputs and measures each output channel's level. It checks:

- the two stereo pairs, with the master on outputs 1 and 2;
- the track playing there through the master, then on outputs 3 and 4 alone with Ext. Out, and back with one undo;
- refusals for the Master Out and a missing output;
- that the Master Out cannot move onto an output a track plays to, and moving it carries the whole mix to outputs 3 and 4;
- that the Master Out cannot be switched off, and is kept with the project;
- two mono outputs, a track on mono output 3 playing on that channel only, and an output switched off not being offered;
- odd and missing pairs refused;
- the layout kept for the next Session, and the pair joined again.

The UI suite checks:

- that a strip's Audio To offers Ext. Out to the other outputs and routes there;
- that the master strip's Master Out chooser moves the master and back;
- that Audio & MIDI Setup lays out Output Config beside Input Config.

## Cue

Cue Out, the cue volume and the Solo/Cue switch are in the [cue contract](cue-contract.md).

## Not yet

These are recorded in the [open questions](../open-questions.md#output-routing):

- An Ext. Out to the master's own output.
- The reference's channel naming ("3/4").
- Undo of Master Out.
