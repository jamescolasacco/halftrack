# Dependency inventory

The build uses the exact compatible revisions recorded here. New upstream releases do not automatically change the build.

| Component | Revision | License route | Purpose |
|---|---|---|---|
| JUCE | `37c894f83d379179b2070d437ccd0f1cd9af9576` | AGPLv3 | Shared native UI, CoreAudio/ASIO/WASAPI, AU/VST3 hosting, audio formats |
| Tracktion Engine | `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f` | GPLv3-or-later | Streaming, arrangement, native Session launcher, graph, recording, automation infrastructure, rendering |
| VST3 SDK bundled by JUCE | See pinned JUCE source | MIT | VST3 hosting and deterministic test plug-in |
| ASIO SDK bundled by JUCE | See pinned JUCE source | GPLv3 option | Windows device backend |
| Signalsmith Stretch | Bundled by pinned Tracktion revision | MIT | Selected stretching backend; editing and fidelity tests pending |

JUCE and Tracktion have additional bundled dependencies (including codecs, font rendering, resampling, queues, and allocators). Preserve their source notices. See [pinned JUCE license inventory](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/LICENSE.md) and [pinned Tracktion license](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/LICENSE.md). CMake records archive SHA-256 values and validates downloads. JUCE receives the Windows confirmed-exit coordinator patch described below. The Tracktion revision additionally receives the recording-safety, clip-launcher, offline-render, scanner, asynchronous plug-in, structural-sort and clip-startup-gain patches documented below.

Original Halftrack code and SVG marks are AGPL-3.0-only. No commercial JUCE or Tracktion license is selected. Their commercial terms apply to different licensing choices and are not a substitute for preserving open-source obligations. Preview packaging includes complete corresponding source archives with embedded third-party notices. Signed public releases still require distribution review. No third-party plug-in binaries or vendor sample libraries are redistributed.

VST2 support is not enabled: the historical SDK is not fetched. CLAP support needs its own host adapter. No promise of Intel-only plug-in bridging is made in the Apple Silicon preview.

## Tracktion recording-safety patch

`cmake/PatchTracktion.cmake` modifies only the pinned engine's `playback/tracktion_DeviceManager.cpp`. MIDI-device discovery used to clear playback contexts during an active recording, discarding the take. The patch defers automatic and explicit MIDI discovery while any engine context is recording, retries after 100 ms, and resumes discovery after recording stops. It runs on the message thread and preserves the user's scan-interval setting; it does not change the audio callback.

CMake applies this after dependency population for both downloaded archives and `FETCHCONTENT_SOURCE_DIR_TRACKTION` overrides. The script accepts only the exact original or already-patched file, making reconfiguration idempotent; unexpected source fails configuration instead of applying an unverified edit. The patch writes explicit LF line endings on every platform; Windows text-mode conversion must not alter the pinned result. Original SHA-256: `28f900c36a68f768706bad72ef7d65cac7ff2adc313f7aa0c2b047cab398696e`; patched SHA-256: `04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32`. Dependency updates require reviewing this patch again.

The dependency retains its own notices and license. Preview packages include the modified Tracktion source as well as Halftrack's patch script, so the shipped source corresponds to the built application. MIDI-device changes are deliberately deferred during a take; audio-device disconnection and other graph rebuild paths require separate validation.

## Tracktion clip-launcher patch

`cmake/PatchClipLauncher.cmake`, introduced in `8606b05`, now modifies four files in the same pinned Tracktion revision. Native launcher playback could allow arrangement audio to return before the message-thread timer published launcher ownership. The patch claims ownership at the scheduled audio boundary, retains it when a Session clip stops, and releases it only through an explicit Return to Arrangement path. A generation counter prevents a stale audio-thread claim from undoing a concurrent Return. It also handles a queued start already reached by the current processing block. A per-track native `LaunchHandle` schedules silent takeover for empty slots, including arrangement-only and source-free tracks; the graph-builder hunk retains a switching node while that ownership or scheduling state is needed. This creates no hidden clip or persistent performance edit.

The switching node now releases held arrangement MIDI notes even when the source has no audio channels. Merged MIDI events are sorted by timestamp, with note-offs before other messages at equal timestamps and stable ordering within each class. This prevents an old arrangement note-off from immediately silencing a new same-pitch Session note. It uses the pinned dependency's non-allocating stable-sort helper; this is not a whole-host real-time safety claim. See [the scoped audit](realtime-safety.md#native-launcher-ownership-and-midi-handoff).

Paths below are relative to `modules/tracktion_engine/`:

| Patched file | Original SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| `model/tracks/tracktion_AudioTrack.h` | `8f7ae3dd2f45939efb36fd15f5fd1c64f82d0ae6d543dab7e003a9419b54b8c5` | `933f35baa4ded602872777489b1661ca15a668de49f901fb0dc1701e94528ad7` |
| `model/tracks/tracktion_AudioTrack.cpp` | `de63d10123c0f286e8811a8f8a8d166b373a1be741cde8a0a34599e707d9c861` | `b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8` |
| `playback/graph/tracktion_ArrangerLauncherSwitchingNode.cpp` | `3afeace9d9eff030b8cb83b70850c6cc9c8c575f2c07c05115807f47407231fa` | `85f01686fff5abfb6a7e54fe0961c50d8ae362617007eda4a9dba7f7c93ac2eb` |
| `playback/graph/tracktion_EditNodeBuilder.cpp` | `6bc90a7dae60e9a0ab3d01fd77b0f90377b8933a9fe11bccbb38b31f471f060d` | `31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074` |

CMake applies this separately from the recording patch, after dependency population for both downloaded and local-source overrides. All four files are preflighted before mutation and must match an exact upstream, supported prior launcher-patch, or current hash. Reconfiguration is idempotent, writes use explicit LF line endings, and unknown input or output hashes fail configuration. Disposable checks passed for upstream/prior/current/mixed inputs, repeat application, unknown fourth-file rejection without mutation, and CRLF helper encoding producing the exact LF source hashes. Dependency updates require reviewing all patches. Packaging this build requires the modified corresponding Tracktion source and all patch scripts with all dependency notices retained; older preview archives predate the launcher patch.

`tests/LauncherTests.cpp` covers ownership before message-thread publication, stopped-clip silence, graph rebuilds, stale claims after Return, and different/same-pitch MIDI handoff through generated audio. Empty-slot cases also cover arrangement-only and source-free audio/MIDI tracks, adding sources and deleting scenes while silence owns a track, cancellation, undo and portable project preservation. The passing local subset and its timing tolerances are recorded in [verification](preview-verification.md#session-extension-local-verification). Physical-device deadlines and the complete dependency callback graph remain unverified.

## Tracktion offline-render latency patch

`cmake/PatchRenderLatency.cmake`, committed in `195bcd0`, modifies only `modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp` in the pinned revision. The offline renderer dropped leading latency frames by slicing an audio view, but its conversion to a JUCE buffer ignored the slice offset. A second calculation inferred the number of dropped frames from the full buffer capacity; that was incorrect for a final render block shorter than the processing buffer. The patch passes the view's actual offset to the JUCE buffer constructor and uses the actual `numToDrop` when advancing the partial block.

Original SHA-256: `40f8e18c146c05e93dc40a54602a6af674ca81a6253120e4782e9a59ae5af6a2`; earlier latency-only output: `cde4f3dbc50f0f1378e6f7b5f638422ba985333bb8abb3e8a6a4d060320a10e5`. The current patch additionally corrects forced-mono output to average all graph channels instead of selecting channel zero; final SHA-256: `57e58a225e0f433fb96920f03731ac37abe805205420e49e5c4ef7f0276186a4`. It accepts the pristine, earlier published or final exact state, validates transformation before writing, uses LF line endings, and is idempotent. Generated preflight fixtures cover fresh/upgrade/repeat and rejection of unknown source without mutation. It is separate from the recording and launcher patches and must be included with corresponding modified source when packaging this build.

The initial full-edge VST3 comparison failed in 8.79 seconds. Correcting the view offset made the tested 48000-frame exports at 127 through 635 samples of graph latency match their dry reference exactly; a separate 64-frame export then failed in 14.14 seconds and exposed the partial-block defect. The combined correction passed `latency-acceptance` in **17.40 seconds**: ten boundary exports cover 48000 and 64 frames at delays of 127, 254, 381, 508 and 635 samples, with exact length, finite values, and zero error against the dry graph reference. Four disposable CMake checks also passed: exact apply, repeat apply, unknown-source rejection without mutation, and a CRLF-encoded helper producing the exact LF source hash. These are generated offline export tests; this patch changes no real-time audio callback and does not establish live compensation, external hardware alignment, or arbitrary plug-in behavior.

## Tracktion scanner isolation patch

`cmake/PatchPluginScanner.cmake` fixes the pinned scanner's reply-queue race, connection flags, teardown order, and fallback into the host after a child launch failure. Reply consumers use the producer's mutex; connection flags are atomic; the derived scanner stops IPC before destroying its queue and mutex. If its executable cannot start, discovery now fails with a retry message instead of loading the plug-in inside the application. The engine's new `getPluginScanExecutable()` hook defaults to the current application; the test overrides it with a nonexistent private path to reproduce that failure deterministically.

Paths are relative to `modules/tracktion_engine/`:

| Patched file | Original SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| `plugins/tracktion_PluginScanHelpers.h` | `94eb54614af016e38e67af2e27f870e0ad14aa6bf83b42638d7b36c8291f8d8c` | `f26479c74d2e829a26997f46f6225a8ea2c3887738e560392f06ff48373c464f` |
| `utilities/tracktion_EngineBehaviour.h` | `71025051424caea898d1f07a8a6a67b4d7500b4d1c3991a734bcad0188c6ee18` | `09b64a7d5a5e58b45370424262f61a562f620affdb9e2b92829666d0a024347d` |

Both inputs and transformed outputs are checked before either file is written. Exact original/current inputs and the previous scanner result `521a1479001bb633c73f5a4780fe85418128728e1dab2f3c0abe43d84c4b0895` are accepted; unexpected content fails configuration. Explicit LF output keeps Windows and Unix hashes identical. Disposable checks covered original/current/mixed/repeated application, either unknown input without mutation, and CRLF helper encoding.

The regression first observed the fixture constructor running inside the parent after launch failure. After correction, `plugin-scan-acceptance` passed in 4.44 seconds: 64 actual IPC scans, two deliberate child exits, cancellation while a constructor stalled, and recovery all passed with zero parent constructions. The test removes cached metadata only from a private copy of Halftrack's original test plug-in, forcing actual loading. This verifies those failure paths; it is neither a sandbox for untrusted plug-ins nor a whole-program race proof. These synchronization changes concern the scanner, not the audio callback. Packaging includes the modified dependency source and this helper with its original notices.

A follow-up retry regression exposed an incorrect classification: returning a crash result for child-startup failure caused JUCE to blacklist a plug-in it never loaded. The same-list retry failed in 0.42 seconds. Startup failure now reports an empty, non-crash discovery result while keeping the visible warning; actual child crashes still return the crash result and remain blacklisted. The new helper accepts the preceding exact scanner patch as an upgrade input. The updated focused test passes in 4.08 seconds, including same-list retry and actual-crash blacklisting; commit `de9a0d0` contains this follow-up.

## Tracktion asynchronous plug-in lifecycle patch

`cmake/PatchAsyncPlugin.cmake` changes only the pinned `modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp`. Failed creation now clears the loading flag. A completion arriving after removal discards its processor without initializing detached state through the edit's UndoManager; otherwise that metadata initialization can erase Redo. The existing track-reattachment hook restarts only a cached plug-in marked as needing initialization, allowing undo/redo to restore it normally.

Original SHA-256: `b13b051c3c7b2c098a43aef53d0607be26dbfab0b948884b11d638518ab14476`; patched SHA-256: `9e489383996f1e5da43529f3bf2ce4f12818bef01e17378a014b07144688b01c`. The previous exact result `f01dd4643715e585904b1adc15b38334644ca82bb4567e2d6623e0b3478c92e7` is also accepted as an upgrade input. Exact apply, reapply, rejection of unknown input without mutation, and CRLF helper normalization all passed on disposable sources. The corresponding modified dependency and helper retain original notices in packages.

The separate Halftrack window fix in `6f4b63c` rejects editors while processors are unavailable, hosts an actual custom or generic editor after readiness, and calls `editorBeingDeleted` before deleting it. Previously the stale active-editor pointer made a restored processor fall back to the wrong editor. Nine original-fixture lifecycle scenarios pass locally in 3.50 seconds, including custom/generic content, close/reopen, removal/undo/redo, track deletion, new/load replacement, detached completion and unrelated redo preservation. The original nine scenarios complete on the message thread. Follow-up `6235185` adds four worker-delivery scenarios and queues completion before reading the weak target or editing project state. That regression failed in 4.55 seconds before correction; the expanded editor suite passes in 4.42 seconds. It proves queued success/failure, undo before delivery, and destruction before delivery, including processor disposal on the message thread. Five disposable source-pin cases and reapplication pass. Neither these synthetic formats nor the AUv2 fixture prove AUv3 or arbitrary vendor compatibility.

## Tracktion structural-sort transaction hooks

`cmake/PatchEditSort.cmake` exposes the native pending TrackList and ClipList sort delivery to the message-thread Session facade. A send gesture followed by export reproduced an extra unnamed undo transaction: the renderer dispatched deferred structural ordering after the gesture closed. Session now drains only that ordering before a new transaction, after mutations and before gesture completion. Native sort actions remain undoable; there is no general message-loop pumping or audio-thread work. Runtime integration is recorded in progress.

All three files are checked before any write; original and already-patched states are accepted, other content aborts configuration. Writes explicitly retain LF newlines. Disposable checks pass initial application, reapplication, each of three independent unknown-source failures without mutation, and CRLF helper compatibility.

| File under `modules/tracktion_engine` | Original SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| `model/tracks/tracktion_TrackUtils.h` | `fcad3688f77edb794f819be0e8a070d249a1396bc32278b979a4095a68a541e3` | `7eaef085fa9693bcb82435b0965ad6b87f0cecb9251517bffe05c30a5c8aacf3` |
| `model/clips/tracktion_ClipOwner.h` | `cc332f9847996d75c8af0f20c04557692a22bd1c341afb94ac5aacabaed549db` | `35fbc1ab47d65d5551f67ce283c6ebb70f6388acf9682bc8b1e7939b82218d90` |
| `model/clips/tracktion_ClipOwner.cpp` | `b40eeba8bf5d812cad05ce4485bab191ce39b612d6131b5367ef4b4bf868cb11` | `2fbdcae339880e99bb1a83005f7063d896f263026604b3e7b9656a5ac3d972c9` |

## Windows confirmed-exit scanner recovery

`cmake/PatchChildProcess.cmake` adds a Windows-only protected coordinator query using JUCE's retained child process handle. The pinned Windows `isRunning()` reports false only for a signaled handle; missing ownership or a failed query is not treated as exit. The existing scanner worker polls this query in its reply loop and uses the existing crash/retry/blacklist path. A complete reply already retrieved by that iteration takes precedence. This does not establish ordering for a reply concurrently arriving after the queue read.

Coordinator teardown skips the kill-message write only when the same retained handle already confirms exit; it still disconnects, cancels pipe I/O and releases ownership. Healthy workers and workers that are still alive retain their original timeouts. No generic pipe code, polling frequency, audio callback or background-thread ownership is changed. A child can still exit immediately after the query, so this is not a universal immediate-recovery guarantee.

CMake populates JUCE without adding its subdirectory, applies the patch, then adds it to the build. This covers downloaded archives and source overrides before JUCE's configure-time helper is built, using the documented [FetchContent SOURCE_SUBDIR behavior](https://cmake.org/cmake/help/latest/module/FetchContent.html). Both original inputs and transformed outputs are verified before any write. The pinned JUCE files contain CRLF; transformed output deliberately uses LF. Packaging retains this helper and the complete modified JUCE source with its existing notices.

| File under `modules/juce_events/interprocess` | Original SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| `juce_ConnectedChildProcess.h` | `697e20d898c9bee6d8f2e87bd2763fe086118c8dd207b36e981d342289911933` | `870a7ac9264e8d53dba2a08ff56e20a8afd760e9764c0311588842408f1fde32` |
| `juce_ConnectedChildProcess.cpp` | `a8e7e6de59390d48c4b44a8240804e82d4276f57d8aefe51199464c0ea4e53c1` | `9563a5c0cb0bfbf90c307742e1a2afbff820d5daca0cbfaff5955e0c699cebef` |

The scanner helper additionally accepts its preceding exact `a4498068deaeec17081d992825d1df2faf49a1e6c289b91d0fc9a8a54aa7071c` result as an upgrade input. Fourteen disposable application, reapplication, upgrade, unknown-source rejection and CRLF-helper checks pass. Windows-only test observations are static and compile out when `HALFTRACK_TESTS_ENABLED` is false. The ordinary crash test remains notification-enabled; a second test suppresses only the competing notification classification so actual process exit must exercise the new branch. Runtime measurements are recorded separately in progress.

## Tracktion clip-startup gain patch

`cmake/PatchClipGain.cmake` initializes the resampler level before the stretch-reader constructor primes buffered source audio. Without it, a saved attenuated Session clip could briefly begin at unity gain. The existing per-block gain and playback pipeline remain in place. Exact pinned source checks, generated playback regression and limitations are documented in the [audio-editing contract](research/audio-clip-editing-contract.md#startup-gain-regression). The dependency retains its license and notices, and source-inclusive packages contain the modified source.


## Tracktion reverse-channel startup patch

`cmake/PatchReverseChannels.cmake` supplies immutable source channel metadata while a reversed playback file is still being rendered. This prevents an immediate export or redo from constructing a zero-channel graph and silently writing zeros. Original input SHA256: `e87f781fd11f8f90236b416bd762577105439ef11eb13f63f1d9770b7ac49826`; patched output: `1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57`. The patch validates exact inputs/outputs, accepts repeat application, rejects unknown sources, and preserves dependency notices. Source-inclusive distributions contain the actual patched file. Immediate reverse/undo/redo full-buffer exports and an odd-frame source spanning native reverse-worker blocks are covered by `audio-clips-acceptance`.


## Tracktion tempo range and precision patch

`cmake/PatchTempoRange.cmake` aligns the pinned engine with Halftrack's 20–999 BPM project range and preserves beat positions for small tempo edits. The native 1 ms clip-remapping guard skipped a 300-to-300.01 BPM update, shifting a nominal beat-1.5 clip to beat 1.50005. The corrected guard is 1e-12 seconds. Conversion, absolute-clip exclusions and native undo remain unchanged. Both files validate before any writes, accept repeat application, and retain upstream notices. Generated fixtures cover fresh/repeated application, an already-patched header with original sequence file, and unknown changes to either file without partial mutation.

| File under `modules/tracktion_engine/model/edit` | Original SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| `tracktion_TempoSetting.h` | `5a3e45f0fb9f53846edde6aae349ba38544e0618e45008e4994e2543148dcb56` | `b5efb441105e6edf8e216fea7c039a7b47331030019de10dad6db9aa3185c30d` |
| `tracktion_TempoSequence.cpp` | `9bad3c89735d810e4fc41d7f6ac77fd422249287d96306525ae79458839fd6ab` | `c24f67e52af42fa46a551d585825063dde663eb17abe0a2e3236bcebf9037f99` |


## Tracktion MIDI recording and device policy patches

`cmake/PatchMidiReleaseVelocity.cmake` preserves matched note-off velocity when native recording imports a MIDI sequence. The generated controller fixture failed at the note-off byte while seven preceding channel events passed; the patch assigns the native note's existing release property with the same undo manager. `midi/tracktion_MidiList.cpp` changes from SHA256 `161898bf64101cb385b7ef5a99d18bedd8ad5909fa0d422a4ae734ec70fe1a79` to `93b05169b4008e66831d4ee3a9d42d065b32b011cd5f27c1dd04fd0a0854dcbd`.

`cmake/PatchMidiDevices.cmake` adds host policy for physical MIDI discovery/default input enablement, plus synchronous completion of an explicitly selected MIDI device change. Default policy for other hosts retains native behavior. Halftrack excludes hardware discovery in generated-device tests and starts newly discovered production inputs disabled. Native saved preferences still apply. The synchronous rescan uses the existing recording-defer guard. All four inputs are validated before any of these four files is written, and output uses LF on every platform.

| File under `modules/tracktion_engine` | Input SHA256 | Patched SHA256 |
| --- | --- | --- |
| `utilities/tracktion_EngineBehaviour.h` | `09b64a7d5a5e58b45370424262f61a562f620affdb9e2b92829666d0a024347d` | `fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf` |
| `playback/tracktion_DeviceManager.h` | `7ab4590fa518e44b977617dc003597adf84ade0021e6ce01bcf4ebcb6937af50` | `eaabb54af6e469690fb667efe36b6c88d0f9eefd42c7e8d34ac280271b4f5441` |
| `playback/tracktion_DeviceManager.cpp` | `04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32` | `49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498` |
| `playback/devices/tracktion_PhysicalMidiInputDevice.cpp` | `e72a87226f1448aa73fe4eca1cb1cc1ad8245bcccc38f8dd1f362b669ad467d1` | `c4221217f5d2c1dfa57a02268d6a762f6c733ea95a17b9c53371a3c035a5a617` |

The behavior header and manager input hashes already include the scanner and recording-safety patches respectively. Those earlier patch helpers also accept these exact combined outputs for repeated configuration. Fresh/repeated application and each unknown-input no-mutation case pass against isolated pinned sources; unknown release-import source is also rejected without mutation. The GPL notices and full patched sources remain included in packages. No global callback-safety claim follows: the existing native MIDI monitor/capture graph uses locks and dynamic storage.


## Tracktion input take rescan patch

`cmake/PatchInputTakeRescan.cmake` extends the recording-safety deferral in `playback/tracktion_DeviceManager.cpp`. MIDI discovery now also waits while any input of an active context is recording, even when the transport is not recording. This covers Session slot takes, which record through `InputDeviceInstance::startRecording` with the transport playing. Without it, a MIDI device appearing during a take cleared the playback contexts and discarded the take. The check runs on the message thread under the existing shared context lock, reads each context's input list and calls `InputDeviceInstance::isRecording`. It keeps the 100 ms retry and adds no audio-callback work.

The input is the result of the recording-safety and MIDI device patches: SHA256 `49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498`. The patched SHA256 is `896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36`. `PatchTracktion.cmake` and `PatchMidiDevices.cmake` accept that output for repeated configuration. Isolated pinned sources pass three checks. A fresh chain from the original reaches the patched hash. A repeated run leaves it unchanged. An unknown input fails configuration without mutation. Existing checkouts whose shared dependency copy already carries the two earlier patches are upgraded in place on their next configure. A branch without this patch then rejects that upgraded copy, so keep separate dependency copies per branch while both exist.

## Count-in timing and input-state patches

`cmake/PatchNegativeTime.cmake` patches `modules/tracktion_graph/tracktion_graph/tracktion_Utility.h` so graph seconds-to-samples conversion uses signed rounding, matching the core time type. Previously adding +0.5 for negative times moved the first count-in click one sample early. Original SHA256 `7a646528d5b260e2d681205f2b6220e2632798578b72c3d6c6d393618c71e237`; patched SHA256 `e711c54e67db07b1f4347986e3c4f062a6851453d0aa0a371448a393abd9482f`. The click waveform renderer itself is unchanged.

`cmake/PatchInputRecordingState.cmake` adds `InputDeviceInstance::flushRecordingStateUpdates()` to `modules/tracktion_engine/playback/devices/tracktion_InputDevice.h`. Session calls it on the message thread before starting capture, preventing a queued arm/destination update from restarting that take at the pre-roll location. Original SHA256 `9e3041acdd4712ce40f4b0701b74e43ed83f091d4fc2435422b5eed73e795ad7`; patched SHA256 `18cfec9e9fd6c2af4ea15af41e023494d442cf2e586f671d84a0b8d93ebbc7e3`.

Both patches validate exact input/output, accept repeated application, reject unknown source without mutation and preserve dependency notices. Fresh/repeat/unknown-source fixtures pass. Source-inclusive packages carry the actual patched dependency. Generated metronome/MIDI-input tests exercise immediate Arm → Record and negative-time conversion; these tests do not certify physical device timing or whole-engine callback safety.

`cmake/PatchWaveInputStart.cmake` patches `modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp` to defer the first recorded block while a queued transport position is still pending. Input consumption precedes graph seek application; without the guard, capture can accept an old-position block, prematurely clear its first-callback adjustment, and retain extra samples through count-in. Original SHA256 `d08f647d63c6d478dff1bbe93e26477a5e807e47ccc4590a9433dc08f61d10c2`; patched SHA256 `3547f2134733cde3032237fbac4852e217067b4dbb9642235128843738f34ac1`. Exact fresh/repeated application and unknown-source no-mutation checks pass. It adds an atomic pending-position read and branch, with no new callback allocations, locks or I/O.


## Clip activation playback patch

`cmake/PatchClipActivation.cmake` follows the existing startup-gain patch and validates all four pinned inputs/transforms before writing. Wave readers previously reused audio prepared at the old mute level. Including mute in their preparation hash prevents this on graph replacement. The seconds-based MIDI node now preserves its MIDI source ID and atomic mute-transition flag across replacement and resets that flag on reactivation. The looping MIDI node sends its existing note-offs when inherited notes remain active, even if its constructor already sees mute enabled. This keeps the existing per-clip MIDI source and native tail handling; it adds no global processor reset.

| Native source | Input SHA256 | Output SHA256 |
| --- | --- | --- |
| `tracktion_WaveNode.cpp` (after startup gain) | `b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76` | `a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2` |
| `tracktion_LoopingMidiNode.cpp` | `5bfeb512499f922cbe1acb464366c37a55faec511b4b5ce959b6fd4e8ce17a95` | `e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820` |
| `tracktion_MidiNode.h` | `1be178ac571b28ba4c85f15db946c37cfb16209a05c0efcc1061e790fc94aa09` | `0e3af9e8d1c1ba7aca706181876b8f6ccce5d31910f8d9048f63aaaae7a417e4` |
| `tracktion_MidiNode.cpp` | `161f6183ef28aa1c64f6862d7be299b8ce17b413a7b67c759f9112abeb16b5e8` | `208a3f2a6fd88470544538a01fc03a0b447296ce55022be1b0ece89ca4244e98` |

Source-inclusive packages retain all modified dependency sources and GPL notices. Playback/undo and full-render comparisons are exercised in `clip-activation-acceptance`. The extra active-note check is bounded and MIDI transition state is atomic on supported targets; this does not certify existing native MIDI buffer allocation, graph synchronization, plug-ins or hardware deadlines.


## Tracktion MIDI input activation and interruption patch

`cmake/PatchMidiInputActivation.cmake` maintains the current input consumer across native graph replacement, initializes first-input ownership before the first callback, and records queue receipt age in fixed 256-entry storage. After an established callback interruption, it suppresses old nonzero-velocity note attacks and system messages while retaining old non-attack channel state at block start. The first fresh event establishes the timing origin independently of retained old releases. Six audible release regressions and native byte/order/timing tests cover this behavior.

Receipt uses the accurate millisecond clock only for a construction-time non-track/non-hosted classification. The audited current ingress is non-audio; direct fallback of track/hosted devices through the generic node retains its cached clock. Processing still uses the existing cached clock. See [the audited thread boundary and limitations](realtime-safety.md#midi-receipt-clock-boundary); this patch does not certify native locks or arbitrary future callers. A regression observes an actual frozen cache before sending a fresh key, rather than assuming a sleep froze it.

| File under `modules/tracktion_engine/playback/graph` | Pristine SHA-256 | Current SHA-256 |
| --- | --- | --- |
| `tracktion_MidiInputDeviceNode.cpp` | `f893b43a88aa1897164b1f6fe45516b4e5e7e5db435afd09292da34870f91b16` | `b0e8e73a5ab9f5dd1d13e96c2adc84313ed3a579ddb8ca7d80b6564a2c497000` |
| `tracktion_MidiInputDeviceNode.h` | `976acfb6df698e01935bdb3cfad7761da884e07b41875edb325b2ee7a10879a9` | `6103673d444ed07506ab92f96fbfb835639ececeb834c5b936d6bd0838d146ef` |

The helper also accepts exact earlier activation, receipt-age, consumer-lifetime and channel-state outputs, including cpp hashes `b417dbcd6f6dafba3078eaa408b0074d0cd6ba50e7e43f123d5a4f283b4af4a2`, `a1660920bf4c22133e1a5c33264c478ee466cedfccca26edf4be79248aa81c2f`, `a3e623634363acecd4096d29498b8237af3a468015aeccdf48716d4dab115866`, `335da793205ef43c19384f666e0c13bb3d7117595d12b60676f66e5af2027dec`, and header `82f9fae59e49a0293f5aa0e0ba8926faa359df5d33fafc92226480083fb049e5`. Both inputs and both transformed hashes validate before either file is written. Repeat application is a no-op; unknown source fails without mutation. Full modified source and upstream GPL notices remain included in packages. The historical [consumer-lifetime report](research/midi-consumer-lifetime.md) describes its earlier checkpoint, not the current output hashes.

## Tracktion time-stretch alignment patch

`cmake/PatchStretchAlignment.cmake` patches five pinned files (`tracktion_TimeStretch.h/.cpp`, `tracktion_ReadAheadTimeStretcher.h/.cpp`, `tracktion_WaveNode.cpp`) after the clip-gain and clip-activation WaveNode patches, which now accept its result on reconfiguration. It fixes two defects measured with warped click tracks:

- Both stretch readers primed the stretcher in their constructors at the default 1.0 ratio before the clip's real ratio arrived, and discarded `backup + latency` *output* frames although both terms are input frames. The `outputLatency` samples synthesized at 1:1 then played out, shifting warped audio by `outputLatency × (1 − stretch)` (about 90 ms early at 2× stretch and 45 ms late at 0.5× with Signalsmith's 48 kHz default preset). Priming now discards `(backup + inputLatency) / speed + outputLatency` output frames, and a reader re-primes once when the first real speed arrives before any output is read. `TimeStretcher::getOutputLatencySamples()` exposes the output-side part (Signalsmith `outputLatency()`; zero for other stretchers).
- The Signalsmith adapter rounded input frames per 256-frame output chunk, so ratios such as 1.5 ran at 1.497 and drifted about 1.45 ms per beat at 80 BPM. It now carries the fractional input frame between chunks.

Exact input/output SHA-256 values are in the module; it validates every file before writing any and appends the trailing newline its LF write produces. The `warp-acceptance` suite fails at the original source (onsets up to 95 ms off) and passes with every onset within about 1 ms at 60–240 BPM.

## Tracktion launch repeat patch

`cmake/PatchLaunchRepeat.cmake` patches the pinned `tracktion_LaunchHandle.cpp` (original SHA-256 `060884330b8e511f0d207d7b0dc2e8f506ef846b1eff19ca795422f0cf805a24`, patched `1cd20c6ace77ed96f2cc99ef41754bfc066d2745c9e91e4f36d14c2ca7009693`). The handle's repeat loop (`setLooping`) computed the split from the block end with `fmod`. A loop point exactly on a block start therefore produced an empty first section, and `SlotControlNode::process` skips a block whose first section is empty. That retrigger lost its first block. Rounding could also place the point just past the block and miss it. The handle now locates the loop point itself. A point on the block start, within 1e-9 beats, restarts the whole block; points inside the block split as before. `launch-modes-acceptance` fails at the original source (the block-aligned retrigger is 149 frames late) and passes with every retrigger matching the first within 1e-4. The module validates the source hash before writing and retains upstream notices.

## Tracktion follow-action no-action patch

`cmake/PatchFollowNoAction.cmake` patches the pinned `tracktion_FollowActions.cpp` (original SHA-256 `02b464d6810afd73e644fddce42c3b5a9ee63850bb7b2c56427b50337ea8c846`, patched `e09f9b565661defe66fea39d770f53b9efc8f74f6a78c8d5a91babc51d08d286`). The engine stops a clip at its follow-action time and then runs the weighted choice. When the choice was "no action", the clip stayed stopped. Weighted containers now record whether their action is `none`, and that choice continues the clip in phase through `LaunchHandle::playSynced` from its own state, matching the reference workflow where No Action leaves the clip playing. Actions whose engine function is empty for other reasons (Stop) still stop. `follow-actions-acceptance` reaches both outcomes of a 50% stop/no-action pair only with the patch (all 24 trials stopped without it).

## Tracktion warp-marker timing patch

`cmake/PatchWarpTiming.cmake` patches the pinned `tracktion_WaveNode.cpp` after the clip-gain, clip-activation and stretch-alignment patches (input SHA-256 `2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0`, patched `04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269`). The earlier patches accept the new hash on reconfigure. Two bugs in `WarpReader` placed warped audio out of time:

- **The first segment.** It set each block's stretch ratio from `warpTime()`, which gives a time on the first marker the ratio 1. Playback from a clip's start therefore primed the stretcher and ran its first segment at the wrong ratio. That is a constant error of the stretcher's input latency (60 ms at 48 kHz) times (1 − 1/ratio): about 5 ms for a 5% nudge and 50 ms for a 2x squeeze.
- **After a marker.** A ratio set now is first heard after the stretcher's output latency (90 ms), so the change came late.

The reader now takes the ratio of the segment holding the read position plus the output latency, with a marker starting its own segment, when it primes, seeks and reads. Measured by the warp suite: clicks follow a 5% nudge within 0.35 ms and a 2x squeeze within 3.3 ms, before and after the marker.

## Tracktion render sends-only patch

`cmake/PatchRenderSendsOnly.cmake` patches the pinned `tracktion_EditNodeBuilder.cpp` after the clip-launcher patch (input SHA-256 `31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074`, patched `51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48`). The clip-launcher patch accepts the new hash on reconfigure. Live playback treats a track whose output goes to no device or track as a bus that is not heard, so a Sends Only track is heard only through its sends. The render graph, `createNodeForEdit`, summed every track without a destination track into the mix, so such a track was heard in exports. It now wraps those tracks in the same `SinkNode` playback uses. `routing-acceptance` renders a Sends Only track as silent without sends and at its send level with one.

## Tracktion track-inputs patch

`cmake/PatchRenderTrackInputs.cmake` patches the pinned `tracktion_EditNodeBuilder.cpp` after the render sends-only patch (input SHA-256 `51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48`, patched `06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8`). The clip-launcher and render sends-only patches accept the new hash on reconfigure. It makes two changes.

- **Taps.** A track whose audio another track takes as its input sends two more buses from its plug-in chain. The Pre FX bus follows its last instrument, or comes before its first plug-in, and the Post FX bus comes before its own volume plug-in. Both precede the track's muting. The destination's track input reads the bus its input destination's `halftrackInputTap` property names (0 Pre FX, 1 Post FX, 2 Post Mixer, the engine's own bus and the default).
- **Renders.** Playback carries routed audio through a send on the source and a track input node on the destination. Renders built neither, and a render frees the playback context holding the input instances, so exports lost the routed audio. Renders now send every enabled track device's buses. Each track then reads the routes targeting it from the edit's `INPUTDEVICES` state, and returns the tapped bus while the source device monitors In, or Auto with the destination armed. Hardware inputs are still not rendered.

`audio-from-acceptance` checks each tap's level in playback, the Post FX tap in an export, and the solo behaviour.
