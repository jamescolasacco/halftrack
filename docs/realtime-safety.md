# Realtime safety audit

Audit date: 2026-10-13. This is a source review and a regression guard for Halftrack-owned callback code. **The current complete host is not proven allocation-free, lock-free, or syscall-free. Known dependency callback paths take blocking locks.** Offline signal tests, a responsive interface, and active meters do not establish realtime deadlines.

## Owned processing path

The built-in processor is in [SubtractiveSynth.cpp](../src/SubtractiveSynth.cpp) and [SubtractiveSynth.h](../src/SubtractiveSynth.h). Its processing path is:

```text
message thread: state change -> complete settings packet -> bounded SPSC queue
processing thread: applyToBuffer -> consume settings -> timestamped MIDI -> render
```

The DSP owns 16 fixed voices, a 128-entry note-frequency table, and fixed sustain/pitch-wheel state for sixteen channels. Preparation initializes frequency data and centers all wheel ratios before processing. A pitch-wheel event performs one exponential calculation and scans the fixed sixteen voices; no voice, envelope or filter storage is reconstructed. Rendering visits fixed voice storage and writes into the supplied audio channels. It does not construct an audio buffer, read the project model, or call UI code. Note timing divides an existing block into spans; no temporary event container is constructed. Filter coefficients are updated from a complete settings packet at a block boundary.

`SynthParameterQueue` has 64 slots, with 63 usable packets because one slot distinguishes full from empty. The single producer writes a complete packet before publishing its index with release ordering; the consumer uses acquire ordering before reading it and publishes the freed slot similarly. Index atomics are checked with `is_always_lock_free` at compile time. The callback performs at most 64 pop attempts per block. On a full queue, the producer retains the newest settings and retries from its message-thread timer; the audio thread never waits. This assumes exactly one producer and one consumer. Export must suspend/release live graph processing before using that same processor on its worker.

The generated gain plug-in's callback applies a scalar gain to the supplied buffer. Its parameter creation, editor construction, and state streams occur outside `processBlock`.

These observations concern the owned bodies and their directly inspected helpers. Standard-library math, framework buffer methods, dependency dispatch, third-party processors, and generated machine code still need dynamic instrumentation before making a stronger guarantee.

## Executable source guard

Run:

```sh
python tests/realtime_audit.py
```

The guard requires the synth render/callback roots, the normalized track mixer callback in Session.cpp, the device-stop watch's IO callback in Session.cpp (it only clears the outputs the device manager adds to the engine's), and the generated gain callback to exist. It follows calls to helpers defined in the audited source/header files and rejects recognizable allocation, storage-growth, locking, waiting/signalling, file/system I/O, logging, UI dispatch, and throwing operations. Nine small positive/negative fixtures check the guard itself, including a forbidden operation hidden in a reachable helper. CI runs it on all three operating systems.

A renamed or missing required root fails rather than silently reducing coverage. Same-name overloads are conservatively included. This is a small source scanner, **not a C++ semantic parser**: macros, templates, indirect calls, virtual dispatch, hidden allocations, external methods, container implementations, and runtime contention can escape it. A pass means the inspected source patterns passed, not that the application is realtime certified. Extend the roots and audit scope whenever another owned processor is added.

The initial local run passed the guard and its eight fixtures against the new synth. Native synth signal tests are owned by [MidiTests.cpp](../tests/MidiTests.cpp); their integration run is tracked separately in [progress](progress.md).

[RealtimeTests.cpp](../tests/RealtimeTests.cpp) adds a runtime guard in `BUILD_TESTING` builds. It replaces standard C++ scalar/array new/delete (including sized, nothrow, and supported aligned variants), honoring allocation failure/new-handler behavior. A thread-local scope counts calls only during the test operation; an explicit allocation/deallocation control case checks that the hook is active. The processing case prepares storage first, then exercises both oscillators, parameter updates, more notes than the voice limit, sustain/release/reset, and blocks of 0/1/3/64/127/256/511/1024 samples. Separate cases check queue overflow/order and 10,000 complete ordered packets with a concurrent producer. Thread creation, scheduling waits, result logging, and assertions stay outside the measured consumer scopes.

The runtime cases **passed locally** on macOS / Apple Silicon in the combined Release acceptance run on 2026-10-13 at 14:01 EDT (**1/1 CTest test, 17.33 seconds**). The log is `build/Testing/Temporary/LastTest.log`; this was the integration working tree before the final follow-up edits, not a final release attestation. The calibration observed one new/delete pair, and all measured synth/queue cases observed zero. The concurrent case delivered all 10,000 ordered, coherent packets. Windows/Linux runtime results and GUI checks are still pending for this candidate. It measures only replaceable C++ new/delete calls reached by those owned DSP/queue operations. It does not intercept `malloc`, custom allocators, platform calls, third-party libraries, or locks, and it does not instrument the entire host callback. Production builds with `BUILD_TESTING=OFF` omit the replacement operators. No lock-contention measurement or callback deadline trace was performed.

The mixer follow-up at c805766 passed the full local acceptance suite in 48.01 seconds, including the existing owned allocation checks and 35 mixer renders. The source guard now covers seventeen owned bodies and passes nine self-tests. The new mixer calls the inherited engine volume/pan processor and then applies a bounded constant gain to the first two channels. The inherited processor is an explicit dependency boundary: its VCA gain lookup uses `vcaTrackLock`, so this callback is not claimed to be transitively lock-free. The runtime allocation hook still certifies only its explicitly measured synth/queue scopes, not every mixer/framework call. Latest remote results are tracked in [progress](progress.md).

## Native launcher ownership and MIDI handoff

The clip-launcher patch in `8606b05` adds a `std::atomic<uint64_t>` to each native audio track. Bit zero marks Session ownership; the remaining bits form a generation that changes when the message thread releases ownership. A compile-time `is_always_lock_free` assertion checks the atomic type. At a scheduled launch boundary, the processing thread attempts a single compare/exchange against its previously read generation. If Return to Arrangement has changed that generation, the stale claim fails. The retry loop in `releaseLauncherOwnership()` belongs to the message-thread release path, not the audio callback. This is a scoped ownership publication mechanism, not a proof that all surrounding engine calls are lock-free or wait-free.

Ownership is retained after Session Stop so arrangement audio cannot leak back while the UI timer is delayed. Empty-slot takeover uses a per-track native launch handle, advanced at the shared boundary without allocating a hidden clip. Source-free tracks retain a switching node when needed for scheduled or active silence. The dependency launch-handle path has its own synchronization; this extension does not remove or certify those locks. The switching node also sends note-offs for held arrangement MIDI when taking over a MIDI-only source. Its merged event list uses `choc::sorting::stable_sort` from the pinned `choc_NonAllocatingStableSort.h`: timestamps come first, equal-time note-offs precede other events, and original order is retained within each class. The helper works in place with stack temporary storage. Event count still determines work, and framework MIDI storage/move behavior, native launch-handle calls, and the rest of the graph are separate audit boundaries.

Generated native-graph regressions exercise delayed UI publication, stop, graph rebuild, stale generation rejection, MIDI looping, and both same-pitch and different-pitch takeovers. These passed in the local launcher subset recorded in [verification](preview-verification.md#session-extension-local-verification). They test state and output; the existing owned synth/queue allocation hook does not instrument the complete switching node. No launcher deadline, contention, syscall, or physical-device measurement is claimed. The exact four patched files and hashes are in [dependencies](dependencies.md#tracktion-clip-launcher-patch). The known host locks below still apply.

The separate `PatchRenderLatency.cmake` correction preserves sliced-buffer offsets and partial-block trimming in the offline writer. It changes no real-time callback. Its generated export comparisons cannot strengthen the callback, deadline, or physical-latency claims in this audit.

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


## Pitch-wheel follow-up (2026-10-14)

The source guard passes nine self-tests and eighteen owned callback/helper bodies after adding `pitchWheel`. The prepared-DSP allocation fixture now exercises first-event bending, channel 1/16 state, active/sustained/releasing voices, controller reset and full reset. It reports zero intercepted C++ allocation/deallocation calls in the full eight-suite Mac run (122.25 seconds). The sample-timing tests exercise the real plug-in callback, while allocation instrumentation remains scoped to the owned DSP and queue. Standard-library math, dependency locks, physical MIDI and callback deadlines retain the limitations above.


## Channel utility (2026-10-14)

The utility plugin wrapper and prepared DSP are now source-guard roots; the guard passes nine self-tests across 28 owned bodies. The full nine-suite Mac run passes in 145.41 seconds. Utility runtime cases exercise 512 parameter combinations across zero/short/odd/large blocks, mono/stereo buffers, filter switches and reset, with zero intercepted C++ new/delete calls. A bounded-queue test rejects overflow at 63 packets, and a concurrent producer supplies 10,000 complete ordered packets without consumer allocations. The DSP computes coefficients from fixed parameter packets and never reads model/UI state. The limitations above still apply: this is not a malloc, lock, syscall, deadline or whole-host certification.


The subsequent third-order bass/two-pole DC and constant-power balance refinement passes all nine local suites in 146.16 seconds. The callback guard now covers 29 bodies; prepared filter/balance changes and the existing Utility queue stress still report zero intercepted new/delete calls. Computing filter coefficients and trigonometric gains on receipt of a packet introduces standard-library math but no new owned allocation, lock or I/O path. Worst-case callback timing remains unmeasured.

Native effects extend the owned scope: EQ, compressor, delay and room reverb use fixed coefficient/state storage and delay memory allocated only in preparation. Their 34-value SPSC packets have fixed capacity; audio validates and applies complete packets without model reads. Runtime guards cover parameter extremes, mono/stereo blocks, reset and queue saturation. A two-thread test checks 10,000 intact packets. Source scanning now includes NativeEffects.cpp. These checks still exclude engine locks, third-party plugins, non-C++ allocators and deadline guarantees.

The final combined effects/note-tools/tempo source `6467066` passes nine source-guard fixtures across 42 owned callback/helper bodies and all eleven local Mac suites. Synchronized delay retains a prepared 12-second stereo buffer for the longest supported division at 20 BPM; tempo arrives in the existing bounded packet. Zero intercepted C++ allocations in the tested owned scopes is distinct from syscall/contention/deadline proof, and the previously documented engine limitations remain.


## MIDI receipt clock boundary

The pinned input-node interruption correction keeps receipt ages in fixed 256-entry storage and retains old releases/controller state without replaying old attacks. Processing adds bounded arithmetic and classification only; it does not acquire a new lock, allocate, perform I/O or query a new clock. Native input buffers and synchronization remain dependency boundaries, with no whole-host guarantee.

A separate source-path audit found that cached millisecond time can stop advancing while the timer thread waits for desktop dispatch. The interruption age window is about 110 ms at 256 frames/48 kHz, while that wait can reach 300 ms. Receipt now calls `juce::Time::getMillisecondCounter()` only when a construction-time flag identifies neither a track input nor a hosted input. This clock may enter platform code; it is confined to the audited non-audio ingress: MIDI/timer threads and graph/configuration setup. The audio callback still uses its existing cached counter. Track-derived inputs are excluded even on the missing-source fallback; hosted inputs are also explicitly excluded.

The pinned builder normally routes valid track and hosted inputs through separate nodes. Physical input and ordinary virtual input reach this node on system MIDI threads; scheduled physical releases use a timer thread. Searching current Halftrack source finds no callback injection into those ordinary virtual inputs. Owned input-change cleanup uses the separate `AudioTrack` injection path on the message thread. This audit does not certify arbitrary future callers of the public input API. Future custom virtual-input bridges must preserve this thread boundary or explicitly classify their source before reusing the accurate receipt path. Message event timestamps are not receipt clocks: driver configuration, stream offsets and platform clock origins prevent substituting them portably.
