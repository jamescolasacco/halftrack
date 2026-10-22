# Control surfaces

Status: partial, UX-008.

The reference supports many control surfaces through its own scripts, Mackie Control among them, chosen in its Link, Tempo & MIDI preferences with an input and output port. Halftrack supports the Mackie Control protocol, whose messages are widely documented. Surfaces from many makers speak it, natively or in a Mackie mode. No reference script was read or copied.

## Supported surfaces

| Protocol | Status | Tested with |
| --- | --- | --- |
| Mackie Control (MCU), eight strips with master | Partial (below) | A test surface on virtual MIDI ports |
| Mackie Control Extender, HUI, Push, APC, Launchpad and others | Not supported | — |

No physical surface has been measured.

## Behaviour

**The protocol.** `src/MackieControl.*` reads a surface's messages and builds its feedback:

- faders as pitch bend on channels 1-8, the master on 9;
- V-Pots and the jog wheel as relative CC 16-23 and 60;
- buttons as notes 0-112: Rec, Solo, Mute, Select, V-Pot push, bank and channel, Cycle, Click, the transport, and fader touch;
- feedback: fader positions, button lights, V-Pot rings, meters, and the two 56-character scribble-strip rows (7 characters a strip, by Mackie SysEx).

**Choosing.** `setControlSurface ({ input, output })` opens the surface's ports. Halftrack opens them itself and closes the engine's input for that port, so the surface's notes play into no track and do not reach All MIDI Ins. Audio & MIDI Setup's Control Surface menu sets the Mackie Control input and output, or no surface. The choice is kept with the engine's settings and reopened at startup while the ports exist. `midiInputPorts()` lists the input ports.

**What it does.** The eight strips show eight tracks from the bank's first:

- **Fader:** volume, along the interface fader's taper.
- **V-Pot:** pan, 0.02 a tick; pressing it centres.
- **Rec, Solo and Mute:** switch arm, solo and mute.
- **Master fader:** the master volume.
- **Play, Stop and Record:** Stop while stopped returns to the start.
- **Cycle and Click:** switch the loop and the metronome.
- **Rewind and Fast Forward:** move a bar.
- **The jog wheel:** moves a sixteenth a tick.
- **Bank and Channel:** move the strips by eight or one. Bank Right does not move past the last track.

Every change goes through the same Session edits as the interface: undo, automation capture and arm rules apply. A touched fader's moves are one undo step, ending when it is let go. Without touch messages, moves group until the surface rests for half a second, as MIDI mappings do.

**Feedback.** Twenty times a second the surface is sent what has changed:

- each fader's position, except while it is touched;
- the V-Pot rings;
- the Rec, Solo and Mute lights;
- the Play, Stop, Record, Cycle and Click lights;
- names on the top row and volumes on the bottom.

## Evidence

`tests/MackieTests.cpp` first checks the protocol: every fader, V-Pot, jog and button message, and every kind of feedback.

Then it builds a surface from two virtual MIDI ports the test makes; the Session sees only those. Through the surface it checks:

- names on the scribble strips, and a fader moved to 0 dB;
- a touched fader set to -6 dB, and undone in one step;
- the master fader;
- a V-Pot, and its centring push;
- Mute (with its light), unmute and Solo;
- Play with its light, Stop, and Stop again returning to the start;
- Cycle and Click;
- banking with eight tracks or fewer, Channel Right and its names;
- a rename reaching the strip;
- removing the surface.

The UI suite chooses and removes a surface from Audio & MIDI Setup. Systems that cannot make virtual MIDI ports skip both port checks.

## Not yet covered

- Select, Flip, the assignment buttons (sends, plug-ins) and V-Pot modes beyond pan.
- Meters and the timecode display: the protocol builds them, but the Session does not send them yet.
- Extenders, two surfaces at once, and the reference's other surface types.
- The reference's handshake with Mackie units.
- Physical surfaces.
