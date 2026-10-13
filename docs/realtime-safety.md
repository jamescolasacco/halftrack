# Realtime safety audit

Audit date: 2026-10-13. This is a source review and a regression guard for Halftrack-owned callback code. **The current complete host is not proven allocation-free, lock-free, or syscall-free. Known dependency callback paths take blocking locks.** Offline signal tests, a responsive interface, and active meters do not establish realtime deadlines.

## Owned processing path

The built-in processor is in [SubtractiveSynth.cpp](../src/SubtractiveSynth.cpp) and [SubtractiveSynth.h](../src/SubtractiveSynth.h). Its processing path is:

```text
message thread: state change -> complete settings packet -> bounded SPSC queue
processing thread: applyToBuffer -> consume settings -> timestamped MIDI -> render
```

The DSP owns 16 fixed voices, a 128-entry note-frequency table, and fixed sustain state. Preparation initializes frequency data before processing. Rendering visits fixed voice storage and writes into the supplied audio channels. It does not construct an audio buffer, read the project model, or call UI code. Note timing divides an existing block into spans; no temporary event container is constructed. Filter coefficients are updated from a complete settings packet at a block boundary.

`SynthParameterQueue` has 64 slots, with 63 usable packets because one slot distinguishes full from empty. The single producer writes a complete packet before publishing its index with release ordering; the consumer uses acquire ordering before reading it and publishes the freed slot similarly. Index atomics are checked with `is_always_lock_free` at compile time. The callback performs at most 64 pop attempts per block. On a full queue, the producer retains the newest settings and retries from its message-thread timer; the audio thread never waits. This assumes exactly one producer and one consumer. Export must suspend/release live graph processing before using that same processor on its worker.

The generated gain plug-in's callback applies a scalar gain to the supplied buffer. Its parameter creation, editor construction, and state streams occur outside `processBlock`.

These observations concern the owned bodies and their directly inspected helpers. Standard-library math, framework buffer methods, dependency dispatch, third-party processors, and generated machine code still need dynamic instrumentation before making a stronger guarantee.

## Executable source guard

Run:

```sh
python tests/realtime_audit.py
```

The guard requires the synth render/callback roots, the normalized track mixer callback in Session.cpp, and the generated gain callback to exist. It follows calls to helpers defined in the audited source/header files and rejects recognizable allocation, storage-growth, locking, waiting/signalling, file/system I/O, logging, UI dispatch, and throwing operations. Nine small positive/negative fixtures check the guard itself, including a forbidden operation hidden in a reachable helper. CI runs it on all three operating systems.

A renamed or missing required root fails rather than silently reducing coverage. Same-name overloads are conservatively included. This is a small source scanner, **not a C++ semantic parser**: macros, templates, indirect calls, virtual dispatch, hidden allocations, external methods, container implementations, and runtime contention can escape it. A pass means the inspected source patterns passed, not that the application is realtime certified. Extend the roots and audit scope whenever another owned processor is added.

The initial local run passed the guard and its eight fixtures against the new synth. Native synth signal tests are owned by [MidiTests.cpp](../tests/MidiTests.cpp); their integration run is tracked separately in [progress](progress.md).

[RealtimeTests.cpp](../tests/RealtimeTests.cpp) adds a runtime guard in `BUILD_TESTING` builds. It replaces standard C++ scalar/array new/delete (including sized, nothrow, and supported aligned variants), honoring allocation failure/new-handler behavior. A thread-local scope counts calls only during the test operation; an explicit allocation/deallocation control case checks that the hook is active. The processing case prepares storage first, then exercises both oscillators, parameter updates, more notes than the voice limit, sustain/release/reset, and blocks of 0/1/3/64/127/256/511/1024 samples. Separate cases check queue overflow/order and 10,000 complete ordered packets with a concurrent producer. Thread creation, scheduling waits, result logging, and assertions stay outside the measured consumer scopes.

The runtime cases **passed locally** on macOS / Apple Silicon in the combined Release acceptance run on 2026-10-13 at 14:01 EDT (**1/1 CTest test, 17.33 seconds**). The log is `build/Testing/Temporary/LastTest.log`; this was the integration working tree before the final follow-up edits, not a final release attestation. The calibration observed one new/delete pair, and all measured synth/queue cases observed zero. The concurrent case delivered all 10,000 ordered, coherent packets. Windows/Linux runtime results and GUI checks are still pending for this candidate. It measures only replaceable C++ new/delete calls reached by those owned DSP/queue operations. It does not intercept `malloc`, custom allocators, platform calls, third-party libraries, or locks, and it does not instrument the entire host callback. Production builds with `BUILD_TESTING=OFF` omit the replacement operators. No lock-contention measurement or callback deadline trace was performed.

The mixer follow-up at c805766 passed the full local acceptance suite in 48.01 seconds, including the existing owned allocation checks and 35 mixer renders. The source guard now covers seventeen owned bodies and passes nine self-tests. The new mixer calls the inherited engine volume/pan processor and then applies a bounded constant gain to the first two channels. The inherited processor is an explicit dependency boundary: its VCA gain lookup uses `vcaTrackLock`, so this callback is not claimed to be transitively lock-free. The runtime allocation hook still certifies only its explicitly measured synth/queue scopes, not every mixer/framework call. Latest remote results are tracked in [progress](progress.md).

## Known host gaps

These findings are from the exact pinned sources, not assumptions about an entire library. Framework revision: `37c894f83d379179b2070d437ccd0f1cd9af9576`. Sequencing-engine revision: `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f`. Line numbers refer to those revisions.

| Path | Source evidence | Consequence |
| --- | --- | --- |
| Device-manager dispatch | `AudioDeviceManager::audioDeviceIOCallbackInt`, line 1090, acquires `audioCallbackLock`; line 1098 calls `tempBuffer.setSize` | A blocking lock exists before the engine callback; size changes can require buffer growth. [Source](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp#L1083). |
| Engine callback / MIDI time update | `DeviceManager::audioDeviceIOCallbackInternal`, line 1391, takes a shared context lock; `broadcastStreamTimeToMidiDevices`, line 1188, takes a shared MIDI-input lock | Shared mutexes are locks and can contend with mutation. [Source](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/modules/tracktion_engine/playback/tracktion_DeviceManager.cpp#L1364). |
| Input delivery | `WaveInputDevice::consumeNextAudioBlock`, line 1784, locks instances; the no-instance branch constructs an owning audio buffer at 1789. `copyIncomingDataIntoBuffer` calls `setSize` at 1175; consumers and recording contexts are locked at 1226 and 1241 | Enabling inputs brings additional locks and possible allocation paths into the callback. [Source](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp#L1171). |
| Recording handoff | Queue free/pending lists use locks at 1867 and 1891; exhaustion allocates a `QueuedBlock` at 1870; block loading resizes storage at 1841; `addBlockToRecord` calls `notify()` at 1989 | Disk writing is on a worker, but handing data to it is not a bounded lock-free path. Notification may enter platform synchronization code; syscall freedom is not established. [Source](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp#L1826). |
| External plug-in processing | `ExternalPlugin::applyToBuffer` takes `processMutex` at 1396; channel-mismatch branches use scratch buffers before invoking the external processor | The host's plug-in path includes a lock. Each external processor also has its own unknown realtime behavior. [Source](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp#L1375). |
| Playback resampling | `NodePlaybackContext::process`, lines 401–404, ensures interpolators and resizes a scratch buffer on its resampling path | Buffer/topology changes need a separate capacity/lifecycle audit. This branch is not evidence that every ordinary block allocates. [Source](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp#L341). |

This is a targeted list, not an exhaustive dependency call-graph audit. The presence of one confirmed blocking lock is enough to reject a blanket no-lock claim. The audit does not remove locks: their ownership and lifetime protections require a coordinated replacement design.

## UI and lifetime boundary

All `Session` commands, project edits, snapshots, editor lifetime, and model/undo operations belong on the message thread. They may allocate and perform disk I/O and therefore must never be called from an audio callback. The built-in synth publishes settings through its preallocated queue; it does not inspect a ValueTree during rendering. UI polling reads snapshots and engine meters; it does not supply made-up signal values.

The current integration uses the engine's supported graph-update mechanism for track, clip, transport, and external plug-in edits. That mechanism is not equivalent to a universally lock-free graph swap. A future strict host would need prepared immutable processing state, a nonblocking publication protocol, and deferred reclamation after the audio thread releases old objects. Recording would need preallocated bounded storage and an explicit nonblocking overflow policy. These changes require lifecycle and concurrency tests, not deletion of synchronization lines.

Before strengthening the claim, repeat these checks on the final revision and other platforms, extend dynamic instrumentation to callback wrappers and non-C++ allocation paths, measure worst-case block times under load, and audit teardown/device changes and every external processing path separately. Hardware timing tests remain separate from generated-source and headless tests.
