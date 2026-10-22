# Plug-in sidechains and bus layouts

Status: partial, PLG-009.

The reference manual says plug-ins that support sidechaining show sidechain controls on the left of the device. Choosers there select any of the internal routing points, and the source is a trigger only, "never actually heard". The manual was consulted on 2026-10-21 ([Using Plug-ins](https://www.ableton.com/en/live-manual/12/using-plug-ins/)). Track routing offers three tap points: Pre FX, Post FX and Post Mixer ([Routing and I/O](https://www.ableton.com/en/live-manual/12/routing-and-i-o/)).

The manual names no default tap. Every sidechain in the installed reference's 25 demo sets that names a plain track, twelve in all, reads its Post FX tap (`AudioIn/Track.N/PostFxOut`), so Halftrack defaults to Post FX. The Compressor preset's sidechain block starts switched off with no input (`AudioIn/None`).

## Bus layouts

`PluginInfo::inputBuses` and `outputBuses` list a loaded hosted plug-in's audio buses as channel counts: its main bus first, then auxiliary inputs (sidechains) and outputs. The engine enables every bus a plug-in offers.

On a stereo track:

- the main bus receives the track's audio;
- auxiliary inputs receive silence until a sidechain feeds them;
- auxiliary outputs are not routed anywhere;
- only the main output continues down the chain.

`PluginInfo::canSidechain` is true when the plug-in is loaded and its first auxiliary input has channels.

## Sidechain

`SidechainSettings` holds three things: whether the sidechain is on, the source track, and that track's tap (`InputTap`, Post FX by default).

**Choosing.** `sidechainSources (pluginId)` lists the tracks a plug-in's sidechain may hear: the audio and MIDI tracks other than its own and the returns. A track is left out when the plug-in's track already reaches it through track outputs, track inputs or another sidechain, because that would feed back. The loop check counts a sidechain whether it is on or off, so switching one on can never create a loop. Track inputs (Audio From) and outputs (Audio To) refuse routes that would loop through a sidechain.

**Setting.** `setPluginSidechain (pluginId, settings)` changes all three as one undo step and refuses:

- a plug-in without an auxiliary input;
- a source that is not offered;
- an unknown tap.

A sidechain that is on with No Input hears silence. Switching it off keeps the source.

**Channels.** The first time a source is chosen, the track's two channels are wired to the main input and the source's two to the first auxiliary input. A mono bus receives both channels of its pair.

**Taps.**

- Pre FX: the source's audio before its devices (after its instrument on a MIDI track).
- Post FX: the audio after its devices and before its mixer.
- Post Mixer: the audio after its volume, pan and muting.

The source's own output does not matter, so a source played Sends Only, which is never heard itself, still feeds the sidechain. Soloing another track mutes the source, which silences only the Post Mixer tap, as with track inputs.

**Delay compensation.** A source delayed by a plug-in reaches the sidechain on the same sample as the plug-in's main input.

**Exports, stems and freezing.**

- Renders include the sidechain.
- A stem of the plug-in's track, rendered with that track soloed, still hears the Pre FX and Post FX taps.
- Freezing the plug-in's track renders the sidechain into the freeze, as it sounded then.

**Saving.** The source, its wiring, the tap and the switch are saved in the plug-in's state. A project whose sidechain names a missing or return track, or an unknown tap, is refused without replacing the open project.

**Track changes.**

- Removing the source leaves the sidechain on, with No Input, in the same undo step.
- Duplicating the plug-in's track keeps the copy's source.

**The UI.** A plug-in with a sidechain input shows a column at the left of its device panel:

- the Sidechain switch;
- the source chooser: No Input, then each track it can hear;
- the tap chooser, which is enabled while a source is chosen.

Choosing a track switches the sidechain on. Each control has a spoken title naming the plug-in.

## Engine

`cmake/PatchSidechainTaps.cmake` makes the engine's sidechain read the same taps as track inputs (see [dependencies](../dependencies.md)). Halftrack's playback watcher rebuilds the graph when the tap or switch changes. The engine already rebuilds it for source changes.

## Evidence

`tests/SidechainTests.cpp` uses the VST3 sidechain probe (`tests/TestSidechainPlugin.cpp`), which has stereo main, sidechain and auxiliary buses. Its left output is its main input's left channel and its right output is its sidechain's left channel. Track A plays 0.5 Sends Only and track B plays 0.25 through the probe. The suite checks:

- the probe's layout; a stereo-only effect has no sidechain and refuses one;
- silence on the sidechain with no source;
- A's 0.5 on the sidechain and B's 0.25 unchanged, in renders and live;
- undo and redo, with live playback following each;
- each tap, with a −6 dB Utility on A before its −6 dB mixer, both in renders and live: 0.5 Pre FX, 0.354 Post FX, 0.25 Post Mixer;
- muting A, which silences Post Mixer only;
- the switch, which silences the sidechain and keeps the source;
- three loops refused;
- 127 samples of plug-in delay on A compensated to the sample;
- B's stem;
- frozen B;
- duplication, saving and reopening, and a tampered project refused;
- removing A, then undo.

The UI suite chooses a source, a tap and the switch in the device panel and undoes each step.

## Not yet covered

- The reference's sidechain Gain, Mix and Mute controls.
- Routing auxiliary outputs (multi-output instruments) to other tracks.
- Choosing mono, surround or other main-bus layouts.
- Plug-ins that change their layout while loaded.
- Frozen sources: the patch sends a frozen track's audio at every tap, but no test measures it yet. A Sends Only track cannot be frozen at all yet.
- Sidechains on group tracks or in racks, and plug-ins on returns (untested).
- Master as a source.
- AU sidechain measurement.
- Reference timing comparison.
