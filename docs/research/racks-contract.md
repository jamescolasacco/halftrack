# Rack contract

Racks follow the reference's Instrument, Audio Effect, MIDI Effect and Drum Racks ([Instrument, Drum and Effect Racks](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/), consulted 2026-10-20). A rack holds parallel chains of devices and sums their outputs, with zones, a chain selector and macros. This is Halftrack's own implementation from the public description. Zone fade shapes, ramp times and other numeric choices are simplest defaults, not measurements of the reference.

## Interface

- Racks are native device kinds 30–33. Instrument and Drum Racks are chosen as a MIDI track's instrument, a MIDI Effect Rack goes with the MIDI effects and an Audio Effect Rack with the audio devices. The device menu lists them under Racks.
- A chain holds MIDI effects, then at most one instrument, then audio effects and Utilities, up to 32 devices. Audio Effect Rack chains take audio devices only, and MIDI Effect Rack chains take MIDI effects only. Racks nest up to four deep. A rack holds up to 128 chains.
- Chain settings: name, volume (−100 dB is silence, top +6 dB), pan on the track mixer's law, activator, solo, and key, velocity and chain select zones. Each zone has a low and high limit and fade ranges inside them.
- Solo is exclusive in the chain list; Cmd/Ctrl-click adds chains.
- Sixteen macros, eight shown at first, with More/Fewer buttons. A macro maps to any built-in device control in the rack, a chain's volume, pan or send, or the chain selector, between a minimum and maximum. A minimum above the maximum inverts the mapping. A macro can drive many targets, and each target control takes one macro.
- A macro's name is its custom name, else its one target's control name, else "Macro N".
- Macros and the chain selector are the rack's own controls, so they automate and undo like any device control.
- Drum Rack chains are pads: each receives one note (or All Notes), plays a note of its own (C3 by default) and can join one of 16 choke groups. Pads send post-fader to up to six return chains inside the rack.
- Dropping an audio file on an empty pad makes a chain named after it with a one-shot Sampler. Dropping on a filled pad replaces its Sampler's sample. Dragging a pad onto another swaps them.
- The pad grid shows 16 of the 128 notes and moves by bank.
- Grouping: Cmd/Ctrl+G groups the selected devices into a rack of the right kind, and the instrument panel's Group button groups the instrument and the audio devices after it. Cmd/Ctrl+Shift+G ungroups a one-chain rack.
- Every command in the [architecture](../architecture.md#racks) is one undo step and validates before changing the project.

## Processing

- An empty Instrument or Drum Rack is silent; an empty effect rack passes its input.
- A note reaches every chain whose key and velocity zones hold it.
- Inside a key or velocity fade range the note's velocity is scaled by the position in the fade. The bottom fade gains (value − low + 1) / (fade low − low + 1), and the top fade gains the mirror of that.
- The chain selector gates notes for Instrument and MIDI Effect Racks and input for Audio Effect Racks. For Instrument and Audio Effect Racks, a selector inside a fade range scales the chain's output linearly. For MIDI Effect Racks it scales velocity.
- Outside its select zone a chain is silent only on a side that ends in a fade, and otherwise keeps its gain, so a chain's tail survives a selector move.
- Gains for the selector, volume, pan, activator and solo ramp linearly over 5 ms.
- Muting or soloing changes a chain's output. A MIDI Effect Rack's deactivated or unsoloed chain starts no notes.
- A note-off reaches the chains that received its note-on, with the note they played.
- Instrument chains receive the other short MIDI events. A MIDI Effect Rack passes non-note events straight through.
- Choke: striking a pad fades every other pad of its choke group linearly over 5 ms from the strike, at instrument-voice level, so effects after the instrument ring on. Pads in different groups ring together.
- Mapped controls take the macro's value at block rate; the control's own setting stays and returns when the mapping is removed. The first mapping of a macro moves the macro to the control's current position. Removing a mapping leaves the control at the value the macro gave it.
- Chains are delayed to the longest chain's latency, and the chain mix to the returns' latency. The rack reports the sum.
- Structure changes build a new immutable layout on the message thread. The audio thread adopts it without allocating or locking, and keeps delay lines of chains that remain.

## Projects

The rack's chains, returns, devices, mappings, visible-macro count and macro names are stored in its device state. Loading checks nesting, limits, unique canonical identities, every property's range and form, device kinds and order, and mapping targets. A malformed rack rejects the load and keeps the open project. Saving collects Sampler samples inside racks into the project's assets. Copying a track or chain gives its rack, chains and devices new identities and remaps the mappings.

## Evidence

`tests/RackTests.cpp` (the `racks-acceptance` suite, 461 checks) renders generated signals and compares them sample by sample against the expected arithmetic or a reference render. It checks:

- the zone and pan arithmetic;
- empty racks, parallel summing with chain gain and pan, activator, exclusive and additive solo, and undo;
- chain-selector gates and linear fades, with input stopped past a zone;
- device order, bypass, moves, removal, duplication, nesting, and latency alignment beside a lookahead limiter;
- grouping, ungrouping and Utilities in chains rendering like the original serial devices;
- key splits, overlaps, key and velocity fades, and the selector driven by a macro at two positions;
- MIDI Effect Rack chains in parallel, filtering by key, selector fades and deactivation;
- Drum Rack pads playing only their notes and exact samples, the play note, pad volume, pan and solo, pad swaps, All Notes, a choke fading linearly over 240 frames at 48 kHz, separate choke groups, and post-fader sends through returns and their removal;
- save, move and reopen of a Drum Rack rendering identically, and duplication with new identities;
- macro mapping ranges, inversion, remapping, chain-volume taper, unmapping, undo, macro automation over time, and save/reopen;
- rejection of eight malformed project files;
- no heap calls in 36 blocks of note floods, chokes, sends and macro-driven controls, nor in adopting a new layout.

The realtime source guard covers the rack callback and choke path. The UI suite drives the rack editor's chains, zone edges and fades, the macro knob and mapping list, More macros, chain-device power and removal, the Drum Rack drop, choke, returns, sends, pad drag and banks, and device selection with Cmd/Ctrl+G, Cmd/Ctrl+Shift+G and Delete.

## Not implemented

- Chain Selector filtering of MIDI controllers, macro variations, randomize and hot-swap.
- Chain colors, and third-party plug-ins inside racks.
- Drum Rack pad outputs routed to the project's returns, extracting chains to tracks, and ungrouping Drum Racks.
- Sample-accurate macro automation; it settles at block rate.
- Any comparison with reference output.
