# Robustness suites contract

Four verification suites test existing behaviour under stress rather than adding features. Both are generated, deterministic and bounded.

## Graph edits during playback

`tests/GraphEditTests.cpp` plays a 1 kHz sine clip and a held MIDI chord through the synthetic device while making 96 edits from a fixed seed:

- adding and removing EQ Three devices;
- moving the audio track's output to Sends Only and back;
- changing its send to a return;
- swapping the MIDI track's instrument between the Poly Synth and the built-in synth;
- adding and removing audio tracks;
- changing the MIDI track's gain.

After every edit it plays three blocks and checks:
- the output stays finite and below 4;
- playback keeps going.

Afterwards:
- the play head has kept time within 0.2 s;
- once the edits are put back, the sine plays at its level within 1% (measured 1.0005).

## Malformed input

`tests/FuzzTests.cpp` saves a project using most of the model:
- audio clips, one warped in Texture with settings;
- a clip envelope, a device, volume automation, a return and a send;
- a MIDI clip whose notes have chance, velocity range and MPE pitch bend.

It then loads 300 mutants of it from a fixed seed. Each mutant either:
- replaces an attribute value with empty, non-finite, huge, negative or junk text, or a 4000-digit number;
- removes or duplicates an element;
- cuts the file short;
- or flips bytes.

Each load either succeeds, and the project then saves and reopens, or is refused leaving the open project unchanged. Junk audio, a WAV cut inside its header, a broken MIDI file and a preset with a non-finite setting are each refused without changing the project.

## What the fuzzing found

- **A crash.** A mutant naming a missing device as a clip envelope's target crashed the engine while connecting the curve. Projects with such a target are now refused on load, and the engine skips such a curve rather than dereferencing it (the clip envelope patch in [dependencies](../dependencies.md#tracktion-clip-envelope-patch)).
- **A save that would not reopen.** A mixer level outside the fader's 0–1 range loaded, and saving then wrote a stored level that the loader refuses. Mixer levels and pans out of range are now refused on load.

A local run of 1500 more mutants with another seed (`HALFTRACK_FUZZ_SEED`, `HALFTRACK_FUZZ_COUNT`) found nothing further.

## Stuck notes

`tests/StuckNoteTests.cpp` holds a long note on a sine voice at full sustain through the synthetic device. A held note ends within its release, leaving silence within half a second, when:
- the transport stops;
- it seeks past the clip;
- a loop inside the note stops;
- the tempo changes and the transport then stops;
- the sounding clip is deleted;
- the instrument is swapped;
- its Session clip is stopped;
- its track is deleted.

The note keeps sounding when a seek lands inside it, inside a loop, and across a tempo change.

## Large sessions

`tests/LargeSessionTests.cpp` builds 24 audio tracks of 16 clips each with an EQ Three, and 24 MIDI tracks of 16 clips of 32 notes, with a 64-point volume envelope on every track. It saves, reopens and renders the session, reporting each step's time and the memory the reopened project takes. Each is held under a generous limit: save 20 s, load 30 s, 2 GB, render 120 s. Locally it measured:

| Step | Measured |
| --- | --- |
| Save | 1832 KB in 0.66 s |
| Reopen (everything intact) | 0.06 s, 11 MB more memory |
| Render 32 s of audio | 2.75 s |

## Long sessions

`tests/SoakTests.cpp` runs a three-minute scripted session through the synthetic device. Set `HALFTRACK_SOAK_SECONDS` for a longer one. The hosted macOS CI runner plays one minute instead, because its message dispatch between synthetic blocks is several times slower than a desktop Mac's and three minutes overran the suite's 600 s timeout. Linux and Windows CI and local runs play three.

- **The project.** Four audio tracks of a 20-second tone and two MIDI tracks of notes, looping over 16 s.
- **The edits.** One every five seconds, in turn: a gain, a Saturator added and later removed, a MIDI track muted or unmuted, a note added, undo, redo, a pan.
- **The takes.** Each minute, a two-second take is recorded on an armed track and undone, which restores the tone it split.
- **What it checks.** Every sample stays finite and under 8. The play head travels exactly what the blocks played in each five-second stretch, within one block. Resident memory and open file descriptors (or Windows handles) after the first minute are compared with the end.

Locally, over three minutes:

| Measure | Result |
| --- | --- |
| Worst stretch against the blocks | 5.33 ms (one block) |
| Takes recorded and undone | 3 of 3 |
| Resident memory | 61.2 MB at 1 minute, 61.1 MB at 3 minutes |
| Open descriptors | 3 throughout (the media is memory-mapped with its descriptor closed) |

An earlier version played a four-minute file without looping. Its resident memory grew about 30 MB a minute while playback mapped more of four copies of the file in. The looping version shows that growth was the media, not the Session.

## Not yet covered

- Dropouts on a real device over a long session.
- Hours-long runs, plug-in instruments and Session View launching in the soak.
- Real plug-ins and real device unplugging under edits are untested.
- Longer fuzzing runs, other file types (grooves, collections, settings) and concurrent edits from several threads are missing.
