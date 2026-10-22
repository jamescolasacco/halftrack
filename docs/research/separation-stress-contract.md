# Audio-thread separation under interface load

Status: partial, QA-005.

The audio thread must keep its deadlines whatever the interface is doing: searching, painting and resizing must not block it or take its time.

## The test

`tests/RealtimeDevice.h` is a test audio device that calls its callback from a real-time thread of its own (JUCE's real-time thread, with the block period as its period). It runs one 256-sample block every 5.33 ms at 48 kHz, as a driver does, and leaves the message thread to the test. Its input is a 440 Hz sine on the left and 660 Hz on the right, continuous from the first block. Each callback's start lateness and duration go into storage allocated before it starts. It opens no hardware.

`MainComponent::runSeparationStressTests` (`separation-acceptance`) builds a project with work for the audio thread:

- track 1 armed on the generated input;
- track 2 playing a 10-second clip through an EQ Three;
- a Poly Synth track playing six-note chords every half bar.

It records for eight seconds. Meanwhile the message thread, in a loop, does four things:

- searches a 3000-file, 30-folder browser library with seven different queries, through the browser index;
- paints the whole window into an image;
- resizes the window between 1440×900 and 1100×760, laying everything out again;
- runs the message loop for 4 ms.

It then checks:

- at least 90% of the expected callbacks ran: the audio thread kept running;
- at most 1% of callbacks took longer than their block period;
- at most 1% started a period late;
- the interface did all three kinds of work throughout;
- the take lasts the recording, starts with the generated input, and holds every sample in order. Each of both channels' samples equals the generated signal within 1e-3, so no block was lost or repeated. The signals repeat every 2400 samples, so a loss of a multiple of 2400 samples (75 blocks) at once could pass unseen.

## Measured

On Apple silicon (macOS 15), one run did about 840 searches, 840 full paints and 840 resizes in eight seconds. Over 1500 callbacks:

- median duration 0.147 ms, 99th percentile 0.317 ms, longest 4.04 ms, against a 5.33 ms period;
- start lateness 0.002 ms at the 99th percentile, 0.013 ms at most;
- an 8.00-second take of 384,000 samples, every one in order.

The numbers are printed on each run.

## A gap under heavy load

At a load average of about 16, with four suites at once, one run found a take with its first wrong sample at 236544 of 375040, on a block boundary. The engine silenced callbacks once its load meter passed 0.98, and a silenced callback dropped its input. Sessions now keep rendering at any load. `recording-stalls-acceptance` (`tests/RecordingStallTests.cpp`) checks this deterministically: a test-only callback on the device manager spins inside chosen callbacks, which the meter counts as the engine's time. See [open questions](../open-questions.md#cpu-overloads-during-takes).

## Not yet covered

- Native file dialogs, which are modal and cannot run in an automated test.
- Plug-in editor windows.
- Hardware drivers and their own threads (the MOTU M4 measurement covers one).
- Load from other processes. CI machines share their cores, so the deadline checks allow 1%.
