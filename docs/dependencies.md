# Dependency inventory

The build uses the exact compatible revisions recorded here. New upstream releases do not automatically change the build.

| Component | Revision | License route | Purpose |
|---|---|---|---|
| JUCE | `37c894f83d379179b2070d437ccd0f1cd9af9576` | AGPLv3 | Shared native UI, CoreAudio/ASIO/WASAPI, AU/VST3 hosting, audio formats, the Video window's player (`juce_video`, macOS, camera support off) |
| Tracktion Engine | `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f` | GPLv3-or-later | Streaming, arrangement, native Session launcher, graph, recording, automation infrastructure, rendering |
| VST3 SDK bundled by JUCE | See pinned JUCE source | MIT | VST3 hosting and deterministic test plug-in |
| ASIO SDK bundled by JUCE | See pinned JUCE source | GPLv3 option | Windows device backend |
| Signalsmith Stretch | Bundled by pinned Tracktion revision | MIT | Selected stretching backend; editing and fidelity tests pending |

JUCE and Tracktion have additional bundled dependencies (including codecs, font rendering, resampling, queues, and allocators). Preserve their source notices. See [pinned JUCE license inventory](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/LICENSE.md) and [pinned Tracktion license](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/LICENSE.md). CMake records archive SHA-256 values and validates downloads. JUCE receives the Windows confirmed-exit coordinator patch described below. The Tracktion revision additionally receives the recording-safety, clip-launcher, offline-render, scanner, asynchronous plug-in, structural-sort and clip-startup-gain patches documented below.

Original Halftrack code and SVG marks are AGPL-3.0-only. No commercial JUCE or Tracktion license is selected. Their commercial terms apply to different licensing choices and are not a substitute for preserving open-source obligations. Preview packaging includes complete corresponding source archives with embedded third-party notices. Signed public releases still require distribution review. No third-party plug-in binaries or vendor sample libraries are redistributed.

Movies are read, written and muxed through Apple's AVFoundation, CoreMedia and CoreVideo system frameworks on macOS (`src/VideoMedia_mac.mm`); no codec is bundled. VST2 support is not enabled: the historical SDK is not fetched. CLAP support needs its own host adapter. No promise of Intel-only plug-in bridging is made in the Apple Silicon preview.

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

## Tracktion MIDI variations patch

`cmake/PatchMidiVariations.cmake` patches three pinned files, after the plug-in-scanner and MIDI-device patches (`tracktion_EngineBehaviour.h`, input `fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf`), the track-inputs patch (`tracktion_EditNodeBuilder.cpp`, input `06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8`) and the clip-activation patch (`tracktion_LoopingMidiNode.cpp`, input `e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820`). The earlier patches accept its outputs on reconfigure. It makes three changes:

- **Hooks.** `EngineBehaviour` gains `getNumPlaybackMidiSequenceVariations` and `createPlaybackMidiSequenceVariation`. The node builder makes a MIDI clip's sequences from them when there is more than one, as step clips make probability sequences.
- **One sequence per pass.** `CachingMidiEventGenerator::cacheSequence` moved to the next sequence on every call, but the looping generator calls it twice a block once a pass's events are done. It now takes the loop index and plays sequence `loopIndex mod n`.
- **Empty passes.** An empty sequence is exhausted at once, and only `advance()`, which an exhausted generator never reaches, moved on to the next loop, so playback stopped for good. The looping generator now skips up to 64 empty loops when it seeks and advances.

`note-chance-acceptance` renders the per-pass outcomes.

## Tracktion Re-Pitch patch

`cmake/PatchRepitch.cmake` patches the pinned `tracktion_AudioClipBase.cpp` after the reverse-channels patch (input SHA-256 `1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57`, patched `63336ba34baf1de28fce2e8bc0aff7b06d015ba45f9776cc62b0e7716449c455`). The reverse-channels patch accepts the new hash on reconfigure. `getActualTimeStretchMode` gave any clip that follows the tempo the default time stretcher when its own mode was disabled. That kept its pitch, so no clip could follow the tempo by resampling. A clip with the `halftrackRepitch` property now keeps the stretcher disabled, and the wave node's resampler then plays it at the tempo ratio. `repitch-acceptance` measures the rendered pitch.

## Tracktion clip envelope patch

`cmake/PatchClipEnvelopes.cmake` patches the pinned `tracktion_AutomatableParameter.cpp` (input SHA-256 `bd3609d9b42fa1312e775c48488307639894a79528c1d84b1909c050d1ae9aad`, patched `fd6b4d431d6d47a1478b972e419d8a0f10c06be9ca4e7695385dde9420767de1`). It makes three changes:

- **Held values.** A curve of one point produced no automation stream. Halftrack holds such a curve's value on the message thread, but once a clip curve modulated the parameter, the audio thread computed the base from the manual value. The track curve source now stores a one-point curve's value in atomics for the audio thread.
- **Deleted curves.** A clip curve's source on a parameter held a plain reference to the clip's curve modifier. A plug-in kept by the plug-in cache for undo could outlive the deleted modifier and read it from a timer, which crashed after undoing a track duplication. The source now detaches on the modifier's deletion notice: it stops its timer, drops its streams and ignores later calls.
- **Missing destinations.** A curve whose destination parameter is missing, as a malformed project can name, was dereferenced when its source was made. Such a curve now gets no source. Fuzzing found this (see the [robustness contract](research/robustness-contract.md)).

The engine's clip curve sources take a critical section around their streams on the audio thread. That is unchanged upstream behaviour and is not claimed lock-free. `clip-envelopes-acceptance` covers both changes.

## Tracktion warp modes patch

`cmake/PatchWarpModes.cmake` patches the pinned time stretcher after the stretch alignment patch. The alignment patch accepts the new hashes on reconfigure.

| File | Input SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| `tracktion_TimeStretch.h` | `823e52adaf06223d36c886c754c0b464f9d0f93ba28d9bbb9d379ac67b345a30` | `27e50e271e9b07c9f30c50951d387b462dbdb0684bfb50c46d81e454ff4dc3f2` |
| `tracktion_TimeStretch.cpp` | `51775bb94b0c46021476489a9d4bbbe4a004c029f1652dec53407d09e0f1c530` | `9058c56542eacba92ee5890eb63ffdb059faac60baf7e6b7998101ff30e8b9f4` |

The patch makes these changes:

- **New modes.** It adds four modes for Halftrack's Beats, Tones, Texture and Complex Pro.
- **A public interface.** It makes the stretcher interface public, so Halftrack's stretchers can implement it.
- **Stretcher creation.** `initialise` creates those modes' stretchers through a factory Halftrack registers.
- **Mode settings.** The stretch options carry four Halftrack values. They are saved as a version 2 string only when set, so other clips' options strings are unchanged.

`warp-modes-acceptance` renders each mode.

## Tracktion MPE timing patch

`cmake/PatchMpeTiming.cmake` patches the pinned `tracktion_MidiList.cpp` after the MIDI release velocity patch (input SHA-256 `93b05169b4008e66831d4ee3a9d42d065b32b011cd5f27c1dd04fd0a0854dcbd`, patched `ed53b9158c0162bef0c3c041c473bd4e6e740bad5eee65c61e4f3e5775436949`). The release velocity patch accepts the new hash on reconfigure.

MIDI clips build their playback sequences in raw beats. Expressive notes and their expression events were placed in seconds there, which the sequence then played as beats. Raw-beat sequences now place them in beats, as other notes are. `mpe-acceptance` measures the bends' timing.

## Tracktion recording errors patch

`cmake/PatchRecordingErrors.cmake` patches the pinned `tracktion_WaveInputDevice.cpp` after the wave input start patch (input SHA-256 `3547f2134733cde3032237fbac4852e217067b4dbb9642235128843738f34ac1`, patched `76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7`). The wave input start patch accepts the new hash on reconfigure.

The take file lookup returns `tl::expected<juce::File, juce::String>`, but its three failures (a folder it cannot create, a read-only folder, a file it cannot replace) returned the message string. A `juce::File` is constructed from a string, so each failure became a file named after the message, relative to the working directory. One local run left an empty folder tree called "The directory…" in the repository. The failures are now returned as errors. Halftrack's own take folder check refuses such recordings first in the recording format suite, so the suite does not reach this path, and it now also checks that no such folder appears.

## Tracktion sidechain taps patch

`cmake/PatchSidechainTaps.cmake` patches the pinned `tracktion_EditNodeBuilder.cpp` after the MIDI variations patch (input SHA-256 `bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766`, patched `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647`). The launcher, render sends-only, render track-inputs and MIDI variations patches accept the new hash on reconfigure.

The engine sent a sidechain source's audio to a bus of its own, after the source's devices and mixer and before its muting. Plug-ins could hear only that point. Now:

- **Sends.** A sidechain source sends the three taps that track inputs read. Pre FX and Post FX are sent from its device chain; Post Mixer is sent after its muting, including on a frozen track.
- **Taps.** Each plug-in reads the tap its `halftrackSidechainTap` property names: 0 Pre FX, 1 Post FX (the default) or 2 Post Mixer.
- **Switch.** A plug-in whose `halftrackSidechainOff` property is set reads no sidechain, and its source sends nothing for it.

The engine's channel wires and delay compensation are unchanged. Halftrack restarts playback when either property changes, undo and redo included. `sidechain-acceptance` measures each tap in renders and live playback.

## Tracktion MIDI ports patch

`cmake/PatchMidiPorts.cmake` patches four pinned files. Each input SHA-256 is the output of the earlier patches:

- `tracktion_EngineBehaviour.h`, input `5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa` (after MIDI variations);
- `tracktion_MidiOutputDevice.cpp`, input `330630e1e82ae881bf7c65cce26bebe7961a26531174d1ca53b45021b7658b82` (upstream);
- `tracktion_DeviceManager.cpp`, input `896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36` (after input take rescan);
- `tracktion_EditNodeBuilder.cpp`, input `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` (after sidechain taps).

The earlier patches on those files accept its outputs on reconfigure. It makes three changes:

- **Port filter.** The engine behaviour can leave a physical MIDI port out (`shouldUsePhysicalMidiDevice`), and the device scan honours it. Halftrack's test Sessions use it to see only the virtual ports they make.
- **Closed by default.** A newly found MIDI output opens only when the behaviour says so (`shouldOpenMidiOutputByDefault`). Halftrack keeps outputs closed until their Track or Sync switch is on, as in the reference.
- **Channel node.** A track playing to a MIDI device with `halftrackMidiOutChannel` set (1-16) gets a node at its end. The node rewrites each channel message to that channel and allocates nothing.

`midi-ports-acceptance` measures notes and MIDI Clock through a virtual port.

## Patch and archive hash index

## Tracktion input placement patch

`cmake/PatchInputPlacement.cmake` patches the pinned `tracktion_WaveInputDevice.cpp` after the recording errors patch (input SHA-256 `76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7`, patched `b7ea47bf3f5680c433197eaa3426c459ffbdf58ae6f1a513708af52d1b8feb2d`). The wave input start and recording errors patches accept the new hash on reconfigure.

Before each callback, the device manager resyncs a playback context's reference stream to [stream time, stream time + one block]. The next block then advances it by a block. So the output rendered in a callback plays from the edit time of the device's stream time plus a block, while that callback's input was stamped with the stream time itself. Every take therefore landed one block before what was heard: through a simulated loopback whose latency was reported truthfully, a recorded click landed 256 samples early at 256-sample blocks. Each input block is now stamped at the stream time plus its own length, the edit time of the output rendered with it, and the click lands exactly (`tests/RecordingLatencyTests.cpp`).

The wave input start patch dropped a take's first block while the transport's queued start was pending, since that block would map through the old playhead position. With correct stamping, that block is what plays from the queued start, so it is now placed there instead: the take begins with the input heard as the recording's first block plays. The metronome suite's generated takes match their input from that sample exactly. The retrospective record buffer, which the engine already offsets by a block, is unchanged.

## Tracktion loop recording length patch

`cmake/PatchLoopRecordLength.cmake` patches the pinned `tracktion_TransportControl.cpp` (input SHA-256 `07f46651b67377e32bd962204c27e3f1fe57e819f21cde4e65e1f884d35eb6c0`, patched `5e43a391154ab64092d83eeeb8dddfc57718a11be23e753e4a1fdb0ec83c931d`). The later playback continuation, count-in start and loop playback patches build on its result, which this patch then accepts as already applied.

- **Before.** The engine refused to start recording round a loop shorter than two seconds, presumably for its own cutting of loop recordings into takes: a one-bar loop at 140 BPM could not be recorded.
- **After.** Halftrack ends a loop recording unlooped and splits it into passes itself, so the minimum is lowered to 10 ms, refusing only a degenerate loop.
- **Evidence.** `take-lanes-acceptance` records 3.5 passes round a one-second loop.

## Tracktion clip fades patch

`cmake/PatchClipFades.cmake` runs after every other patch. It changes three pinned files:

  - `tracktion_AudioFadeCurve.h`: input `a0c004dd6f3196d3df4808f891878b8816df98269d065bbecd8e6e16eba6a522`, patched `a66bb52e2ca925acac9168aaed6f14eb52d7ff3e0058b9e660fa79cb20e09dbd`;
  - `tracktion_EngineBehaviour.h`: input `546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3`, patched `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95`;
  - `tracktion_EditNodeBuilder.cpp`: input `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a`, patched `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c`.

- **Before.**
  - The engine's fade curves were linear, convex, concave and its own S-curve.
  - A clip's node played from the clip's start with the fades stored on it.
  - The engine's own automatic edge fades were fixed 3 ms fades written into new clips.
- **After.**
  - The curves gain the smoothstep 3x² − 2x³, the shape of the reference's default fades.
  - The engine behaviour gains two hooks: one gives an arranger audio clip's derived fades, the other how long before its start the clip plays.
  - The node builder asks the host for these when it builds a clip's node, so nothing derived is stored. It reads each kind of clip's source, and runs the clip's node in the track's combining node, from that much earlier.
  - Halftrack answers the hooks with 4 ms default edge fades and crossfades between touching clips ([contract](research/clip-fades-contract.md)).
- **Earlier patches.** The earlier patches touching the behaviour header and the node builder (plug-in scanner, MIDI devices, MIDI variations, clip launcher, render sends-only, render track-inputs, sidechain taps, MIDI ports) accept the new hashes as downstream states.
- **Evidence.** `clip-fades-acceptance` renders exact smoothstep edges, and a split that crossfades back into the unsplit sine within 1e-7.

## Tracktion count-in patch

`cmake/PatchCountIn.cmake` patches two pinned files no other patch touches:

- `tracktion_Edit.h`: input `48dfd7ac69e95e5257734c24783c9c867c9c499d8af4e396cbafb826d84a5a33`, patched `d391c913e0804f6b0d02ff78202f519a20a5c7c4c6ff1a58681a2773bd7fd23d`;
- `tracktion_Edit.cpp`: input `ecaf4380982c22801e1e7a7e0b6d0e76965f36cba091605b32039f6093215c7c`, patched `385e1d6ad1551a3b38c7d8860c50cd9b364c46f40aecd3693f441b7fa7ebb57a`.

- **Before.**
  - The count-in modes were none, one or two bars, or one or two beats.
  - A bar counted as the first time signature's numerator in beats. With Halftrack's crotchet beats, one bar of 6/8 counted in six beats, two bars.
- **After.**
  - A four-bar mode is added, as the reference's Count-In menu offers None, 1 Bar, 2 Bars and 4 Bars.
  - A bar is numerator × 4 / denominator crotchets when beats are crotchets. The length becomes a double, so 7/8 counts in three and a half beats.
- **Evidence.** `metronome-acceptance` records after a four-bar count-in, and after one-bar count-ins in 6/8 (1.5 s at 120 BPM) and 7/8 (1.75 s).

## Tracktion metronome rhythm patch

`cmake/PatchMetronomeRhythm.cmake` patches two pinned files; the later bars patch changes `tracktion_ClickNode.cpp` again, and this patch accepts its result:

- `tracktion_ClickNode.h`: input `cfca43d540e922c63be15bddb94fc1f9db1cc5c9f199bd3bc96dde86ae030613`, patched `dd21794c667566ec8d0c8f04c16858770f882e9156f410d91889b3361240814a`;
- `tracktion_ClickNode.cpp`: input `487e4ef7c67ab8ee3dd5d6dcd27266087306bd4c5adf735b74cceab3e08a7921`, patched `89c978d830b759cf006bd56c2a125d144479bb77b390f0eb64d2be2d74373941`.

- **Before.** The click generator ticked every crotchet and accented the beats the engine counted as a bar's first. That count treats a bar of 6/8 as six crotchets.
- **After.**
  - It reads the Edit's `halftrackMetronomeTick` when it is prepared, as the reference's rhythm: 0 Auto, −1 a bar, or a division in crotchets.
  - It resolves that for the time signature as the reference does: Auto on the denominator, a division only if it divides the bar.
  - It ticks on that grid, accenting the first tick of each bar of numerator × 4 / denominator crotchets.
- **Evidence.** `metronome-acceptance` compares clicks sample by sample at several rhythms in 4/4, 6/8 and 7/8.

## Tracktion playback continuation patch

`cmake/PatchPlaybackContinues.cmake` patches three pinned files:

- `tracktion_EditPlaybackContext.h`, which no other patch touches: input `d172d8de5ee35392557d3e0e363893283de2d9d0392d7e58114b3c8c3ee09b4b`, patched `5b4993f48aa8700aaa3f7c4e27790c41042536951e20d4d650951b73095ae327`;
- `tracktion_EditPlaybackContext.cpp`, which no other patch touches: input `1742b29b2e581de7583892454d08e988dab60598254d5669164acbd9918bb019`, patched `7d28b4424f5c1bb4140631529a219e9c4f60e58ee1c293a2faf9eb10a890ad70`;
- `tracktion_TransportControl.cpp`, after the loop recording length patch: input `5e43a391154ab64092d83eeeb8dddfc57718a11be23e753e4a1fdb0ec83c931d`, patched `d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f`. The earlier patch accepts this result, and this one accepts the later count-in start and loop playback patches'.

It makes two changes.

**Playing through device rebuilds**

- **Before.** Rebuilding a playback context's devices, as the engine does whenever a MIDI device appears or goes, cleared its nodes and stopped its play head. The new nodes left it stopped, so the transport stopped itself a moment later. Plugging in a controller stopped playback. On a slow CI machine, the engine's periodic MIDI rescan finding the tests' own ports did the same.
- **After.** When the context releases its device list, it remembers whether the play head ran. When the list is rebuilt while the transport still plays, it resumes the play head where it stopped at once, before the transport's timer can find it standing still; the graph may be rebuilt only later. Only device-list rebuilds do this; the transport's own clearing of nodes is left as it was. Recordings are unaffected: the earlier input take rescan patch already defers MIDI discovery while anything records.

**Stop withdraws a pending start**

- **Before.** Recording from a stopped transport posts a play for the play head's next block. A stop that came before that block stopped the play head but left the posted play. The play head then started on that block, and the transport's timer, finding it playing, started playback to follow. Recording and stopping at once started playback a moment later, from where the transport last was. The resume above then kept that stray playback going through a MIDI rescan, where before it had ended it, and the punch suite's MIDI pass, recorded while it played, started late.
- **After.** Stopping the play head withdraws a posted play first.

**Evidence**

- `capture-midi-acceptance` adds a MIDI device while playing and finds playback still running and advancing.
- The UI suite's lane-click check passes with message dispatch slowed to 15 ms a block, where it failed before.
- `transport-acceptance` records and stops at once, then pumps the device for a while with the clock running, and finds the transport stopped throughout. It failed three times in three without the second change.
- `punch-acceptance` passes eight of eight runs in parallel, where it failed six of eight before the second change.

## Tracktion cue patch

`cmake/PatchCue.cmake` patches two pinned files after every earlier patch on them:

- `tracktion_EngineBehaviour.h`: input `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` (after the clip fades patch), patched `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6`;
- `tracktion_EditNodeBuilder.cpp`: input `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` (after the clip fades patch), patched `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf`.

Every earlier patch on those files accepts these results as already applied.

- **Before.** The engine had no cue: the click played on its own output at its own level, and nothing heard a track before its fader except the Audio From and sidechain taps.
- **After.**
  - The engine behaviour gains three hooks, all defaulting to no cue: `isTrackCued`, `getCueOutputDeviceID` (asked when the graph is built) and `getCueGain` (asked on the audio thread, so it must be lock-free).
  - A cued track keeps its Pre and Post FX taps, as a track Audio From takes does.
  - On the cue's device, the node builder sums each cued track's Post FX tap and the click, each through a new `CueGainNode`. That node ramps the cue gain across the block, plays a mono tap on every channel and averages stereo for a mono device. The sum goes after the master's devices and meter; a MIDI click is left as it was.
- **Evidence.** `output-routing-acceptance` measures each output of a four-output synthetic device: a cued track's pre-fader, unpanned level on Cue Out with its master outputs unchanged, with its activator off, at the cue volume, and silent in Solo mode; and the click and a preview on Cue Out. `metronome-acceptance` compares the click at −6 dB sample by sample.

## Tracktion count-in start patch

`cmake/PatchCountInStart.cmake` patches the pinned `tracktion_TransportControl.cpp` after the loop recording length and playback continuation patches: input `d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f`, patched `cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be`. Both earlier patches accept this result, and this one accepts the later loop playback patch's.

- **Before.** A count-in began its bars before the recording's start, plus half a beat of silence before its first click, wherever the start fell.
- **After.** As the reference's count-in does (static reading of its code), it starts on the bar line nearest to its bars before the start, rounding a half away from zero, after first rounding to 1/3843840 beat as the reference does. Its first click is at its start, and it clicks until the recording's start.
- **Evidence.** `metronome-acceptance` compares count-in clicks sample by sample from their start, checks the count-in's length from bar lines and from mid-bar, and checks a take's placement and samples after it. `midi-input-acceptance` trims a note crossing the count-in's end.

## Tracktion loop playback patch

`cmake/PatchLoopPlayback.cmake` patches three pinned files:

- `tracktion_PlayHead.h`, which no other patch touches: input `e592ff87c34b02bc3934fb39d470a9e12bf168601c4d7e3d9bf950b2050e04d5`, patched `29b0133c12b035f159b8464765e6751f57e0a7ac75c8b75a5bfce4403cbe06ee`;
- `tracktion_TransportControl.cpp`, after the count-in start patch: input `cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be`, patched `9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4`. The loop recording length, playback continuation and count-in start patches accept this result.
- `tracktion_EditUtilities.cpp`, which no other patch touches: input `cbc08effa9de1b1143f3e39931ae5c7fe6a2e07a27b23037d9cfc2378dfc3918`, patched `8214ed9e16d54647e3d6a6e5085234915286e7ee3b1b806a509e2fc864885aad`.

- **Before.** With the Arrangement loop on, Play and Record from outside the loop jumped to its start. A jump while playing was clamped into the loop, and a loop recording started at the loop's start whatever the play position.
- **After.** As the reference's transport does (static reading of its code), playback and recording start where the transport is:
  - from before the loop, the play head rolls into it and loops once inside;
  - from at or past its end, it plays on and never loops;
  - a jump goes where it is asked, rolling in or playing on in the same way;
  - a loop recording starts at the play position, so its first pass includes the roll-in.
- **Evidence.**
  - `transport-acceptance` plays from before the loop, past its end and after jumps both ways, measuring the position.
  - `take-lanes-acceptance` records from a second before a loop and finds a two-second first pass, then one round the loop.
  - `punch-acceptance` records round the loop with Punch-In and Punch-Out from before it, and finds three passes in take lanes inside the brace, without the pre-roll.

## Tracktion remote MIDI patch

`cmake/PatchRemoteMidi.cmake` patches three pinned files:

- `tracktion_MidiInputDevice.h`, which no other patch touches: input `e99fb2a7f68fb187d50e7d7f72fab502b4b03ca838eb82d560e6e101905c7101`, patched `4caa2c5b2dfa88a8ff9f0d2ca15830e83c432ac27e3122de275c75875d1fe54e`;
- `tracktion_MidiInputDevice.cpp`, which no other patch touches: input `c1c06cdd51d2e5bc7f7fae0696f9d0752757740cb33ca41b7d562acab68ce6c9`, patched `5f31534136e50526dd3c30aa82a679e8f6a87a90ec1b5aaca4fbf4ac07bb8fba`;
- `tracktion_PhysicalMidiInputDevice.cpp`, after the MIDI devices patch, which accepts this result: input `c4221217f5d2c1dfa57a02268d6a762f6c733ea95a17b9c53371a3c035a5a617`, patched `baa5513d1ecb6d3aad29b74c85ef3393b118892654502a49903330b48877a2f6`.

- **Before.** Every message from a MIDI input reached its instances: a note mapped to a clip slot also played and recorded on any track taking that input, directly or through All MIDI Ins.
- **After.** A MIDI input device has an optional remote control (`MidiInputDevice::RemoteControl`, `setRemoteControl`), offered each message before the device's instances under a spin lock. A message it takes reaches no instance, so no track plays, monitors, records or captures it; a note-off goes where its note-on went. `sendMessageToInstances` returns whether the remote took the message, and a physical input then does not pass it on to the virtual inputs that gather it. As in the reference, mapped notes and controllers stop acting as track input.
- **Evidence.** `midi-mapping-acceptance` plays notes into a virtual controller feeding an armed MIDI track: a note played while learning, and the mapped note afterwards, switch the activator without reaching the track, an unmapped note still reaches it, and Capture MIDI keeps only the unmapped note.

## Tracktion launch velocity patch

`cmake/PatchLaunchVelocity.cmake` patches three pinned files, which no other patch touches:

- `tracktion_LaunchHandle.h`: input `da905fda9f13b54127f59441550d2ac0c2c2268336f05ec28d13bfb9093542fd`, patched `c76b3c71ed1e6c17c28962a4f6f5598b4a37b4af9141add8fd9bbe94d94480a6`;
- `tracktion_SlotControlNode.h`: input `9d5d84386b23635aa010b0bcfffd93cc22a0539a916eb1b71f7498429bc72c8f`, patched `288596a5359fad12c2cac73d2b37044a1e3b79b0f0142a678388a88125efcbfb`;
- `tracktion_SlotControlNode.cpp`: input `decfd887689b87caf5b5140ad5494309253573a66ed3640aba8562ae94d12c5e`, patched `dd5682389efa47a9245ae561605b209dcb3b2ef1e1743261337a979a8dd7a783`.

- **Before.** A Session clip always played at its own gain and note velocities, however it was launched.
- **After.** A launch handle carries a gain and a MIDI velocity scale (`LaunchHandle::setLaunchVelocity`), stored as atomics on the message thread before `play()`. The clip's `SlotControlNode` takes them up as that launch starts, or as it restarts a playing clip. It keeps them in the playback state it shares across graph rebuilds, so they last through loops until the next launch. It multiplies the clip's sound by the gain and scales its note-on velocities, never below 1, as the reference's launch velocity does (static reading of its code). No allocation, lock or system call is added to the audio thread.
- **Evidence.** `launch-modes-acceptance` renders a steady audio clip launched by a mapped note at velocities 64 and 100 with a Velocity amount of 100 % and 50 %. The levels match an independent model of the reference's clip gain scale within 2e-7. A mouse launch restores full volume. A MIDI Monitor sees a MIDI clip's velocity 100 become 50, 100 and 1.

## Tracktion MIDI input channel patch

`cmake/PatchMidiInputChannel.cmake` patches four pinned files after the earlier patches. Each of those accepts its result:

- `tracktion_MidiInputDevice.h`, after the remote MIDI patch: input `4caa2c5b2dfa88a8ff9f0d2ca15830e83c432ac27e3122de275c75875d1fe54e`, patched `b35855d978d2444c773834360c345c8df19d70451adc4f5c28a9ffd50c926dbe`;
- `tracktion_MidiInputDevice.cpp`, after the remote MIDI patch: input `5f31534136e50526dd3c30aa82a679e8f6a87a90ec1b5aaca4fbf4ac07bb8fba`, patched `c5181f604c9fd59d45712fd68c454645d36e3aae26f29d870038222fbb84eec4`;
- `tracktion_MidiInputDeviceNode.h`, after the MIDI input activation patch: input `6103673d444ed07506ab92f96fbfb835639ececeb834c5b936d6bd0838d146ef`, patched `90dec853c902a4349a64b450007f314c7380b5f414494d482385c5f6b33cacde`;
- `tracktion_MidiInputDeviceNode.cpp`, after the MIDI input activation patch: input `b0e8e73a5ab9f5dd1d13e96c2adc84313ed3a579ddb8ca7d80b6564a2c497000`, patched `7f8dd4ed48893d3ba46e4b09a44404dd9bcf334f772aa09be98a06573892cd74`.

- **Before.** Every channel of a MIDI input reached every track taking it.
- **After.** A track's `halftrackMidiInputChannel` (1 to 16) is read for a track or a clip slot's track. The track's MIDI input node keeps only that channel's messages, and its recordings keep only that channel, as the reference's MIDI From channel does. Messages without a channel pass. The filter adds a comparison on the MIDI thread and nothing else.
- **Evidence.** `midi-input-acceptance` monitors and records a generated controller on channels 3, 5 and 2 with the track set to channel 5 ([contract](research/track-inputs-contract.md)).

## Tracktion fader laws patch

`cmake/PatchFaderLaws.cmake` patches five pinned files that no earlier patch touches:

- `tracktion_AudioUtilities.h`: upstream `b772588adb0e179a9af328e89cbb11112e89b99f82a112a9cfe18e08a0b0dc05`, patched `9e525d79f453f03b3ebc497c8a944bef2f7a3cd0673fc75c749b9581b5997c35`;
- `tracktion_AudioUtilities.cpp`: upstream `5073e77b9eccebfbca7d7eb3e151744969305929ee07d7c080a9000031563372`, patched `a2f14631df98ccc833e221e57a6e8137ea3ad4282fe9ec18416ed842e22c8b9b`;
- `tracktion_AuxSend.cpp`: upstream `9d343474d1f22add0a39d159e9ec5fc163b6856d9cf89648fda03c9e687ba9c8`, patched `13d09a32bd3c5fda3e7488b5664415a316030c72440c0a581c119281faad1388`;
- `tracktion_AuxSend.h`: upstream `0f162e59bb4b5924cb19889364823050564bae9b2cc0b839f8907842569eb812`, patched `dc7238c35191465c52d0adc9cd304ed95490b44869bf69b10d369fe22b5995c2`;
- `tracktion_AuxSendNode.cpp`: upstream `58d79bb59e402f758244e9cba74e2ea49b10c293b830d5899f3842f9d2ed5919`, patched `d959b60b746ea5427d6e3eff75c7889198295be03d0ce6eeb19c5867c36dc301`.

- **Before.** Fader positions followed the engine's own law, dB = 20 ln(position) + 6, for volume and aux sends alike.
- **After.** The host can install a volume law and a send law, each a pair of plain function pointers from a position to a gain and back, held in atomics. The engine's volume conversions read the volume law, and aux sends' levels, text, gains and rendering read the send law. With no law installed, the engine's own law applies, and sends fall back to the volume law. Session installs the reference's scales: volume from −70 to +6 dB and sends from −70 to 0 dB, both silent at the bottom. The audio thread pays one relaxed atomic load and a table interpolation per conversion; the tables are built when Session installs the laws.
- **Evidence.** `fader-laws-acceptance` renders a fader and a post-fader send at their levels, checks the research model's levels and opens a generated legacy project with every level kept. `automation-acceptance` renders a volume ramp along the law ([contract](research/mixer-laws-contract.md)).

## Tracktion bars patch

`cmake/PatchMeterBars.cmake` patches three pinned files, after every earlier patch:

- `tracktion_Tempo.h`: input `a78f2961a868d3595c4a80599610c1fd1af1f29327722614e25ef53847b66664`, patched `93e8dbbd94f466189bda4aec01b5147424caa650c8680a7af09a5ba438ffcbb1`;
- `tracktion_LaunchQuantisation.cpp`: input `3becd7c4526ace7b2596389bdbb7e04361552ff5038779ddf2948c47731af177`, patched `9bc45be9a73cf706f6ca11f6c173c0143fb4dd4069e3cbd0b85b4e850cbd2b5b`;
- `tracktion_ClickNode.cpp`: input `89c978d830b759cf006bd56c2a125d144479bb77b390f0eb64d2be2d74373941` (the metronome rhythm patch's result), patched `8321bcd0ec19e6e80ed52ef5af28c6d168c13cacbc984784fc77eb7e0be45ad3`.

- **Before.**
  - With Halftrack's crotchet beats the tempo sequence counted a bar as the numerator in beats, so a bar of 6/8 was six quarter notes and one of 7/8 seven. Bar launch grids and the engine's bar positions followed that.
  - A launch grid finer than a bar was a fraction of that bar, so 1/4 was three quarters of a beat in 3/4.
  - Plug-ins were told four quarter notes per denominator note, half the quarter notes played in 6/8.
  - A time signature change inside a bar waited for that bar's end.
- **After.**
  - A bar is numerator × 4 / denominator beats, a double (7/8 is three and a half), and a beat is one quarter note to plug-ins.
  - As in the reference, a time signature starts a bar where it is, leaving the bar before it short (a fragmentary bar); tempo changes keep the bars going.
  - Launch grids finer than a bar are note values, so 1/4 is a quarter note in any meter, and end at the next bar.
  - The metronome counts its ticks from each bar's start, so the accent falls on each marker.
- **Evidence.** `time-signature-acceptance` launches clips on the bar grid in 7/8 (beat 3.5), on the 1/4 grid in 3/4 (one beat), and across a marker that cuts a bar short (the clip starts at the marker). `metronome-acceptance` matches every click and accent across a 3/4 marker inside a 4/4 bar, and `plugin-timing-acceptance` hears a probe plug-in count quarter notes in 6/8 ([contract](research/time-signature-contract.md)).

`cmake/PatchFreezeTails.cmake` patches four pinned files, after every earlier patch:

- `tracktion_Renderer.h`: input `305c2c08e9664861…` (upstream), patched `0511a660f341…`;
- `tracktion_NodeRenderContext.h`: input `265b6bee4a3fe7d0…` (upstream), patched `c6802d754da6…`;
- `tracktion_NodeRenderContext.cpp`: input `57e58a225e0f433f…` (the render latency patch's result), patched `9d09d2ac4b34…`;
- `tracktion_AudioTrack.cpp`: input `b70f5b63b7c58ca0…` (the clip launcher patch's result), patched `cf1765d05ea1…`.

The full hashes are in the index below. The render latency and clip launcher patches accept the new results.

- **Before.** In a render's end allowance, the first silent block stopped the render. A freeze's allowance comes from its devices' tails, but a Delay's echoes start after a gap of silence, so the freeze ended at the gap and the frozen track lost its echoes.
- **After.** `Renderer::Parameters::silenceBeforeStopping` asks for a stretch of silence before a render stops: silent blocks are held back, written when the tail sounds again, and dropped once the silence lasts. Freezes ask for three seconds; it is zero, the old behaviour, for every other render.
- **Evidence.** `freeze-acceptance` bounces a track whose Delay echoes a 0.2 s burst: the frozen render keeps 2.3 s of echoes after the burst, and plays as the live track does but for echoes under −70 dB ([contract](research/freeze-contract.md)).

`cmake/PatchGroupOutputs.cmake` patches three pinned files, after every earlier patch:

- `tracktion_Track.h`: input `c373a1d823da…` (upstream), patched `19d0e8ef0c8a…`;
- `tracktion_Track.cpp`: input `9350634aa81f…` (upstream), patched `b1623886a9bd…`;
- `tracktion_EditNodeBuilder.cpp`: input `bbc12a579f50…` (the cue patch's result), patched `05e73d01d1cc…`.

The full hashes are in the index below. The clip launcher, render sends-only, render track-inputs, MIDI variations, sidechain taps, MIDI ports, clip fades and cue patches accept the new result.

- **Before.** A track inside a submix folder was always summed into the folder, whatever its own output. The reference lets a grouped track's Audio To leave its group, for Main, Sends Only, another track or an external output, and lets a group leave the group around it.
- **After.** `Track::leavesGroup()` reads Halftrack's `halftrackOutsideGroup` property. A track or folder that leaves its group is in no submix (`Track::isPartOfSubmix()`), so it plays where its own output goes, and its folder leaves it out of its sum. Solo through a destination and the tracks feeding a track follow from `isPartOfSubmix()` as for any ungrouped track. A parent group's mute and solo still reach it, as before.
- **Evidence.** `routing-acceptance` renders a track grouped at −6 dB playing into its group, then to Main past the group's fader, Sends Only, into another track, and back into the group; a group playing into a track; and the routes reopening ([contract](research/routing-contract.md)).

`cmake/PatchStretchPriming.cmake` patches one pinned file, after every earlier patch:

- `tracktion_WaveNode.cpp`: input `04ccc34aa76c…` (the warp timing patch's result), patched `2e4354f76d96…`.

The full hashes are in the index below. The clip gain, clip activation, stretch alignment and warp timing patches accept the new result.

- **Before.** A stretched clip's readers prime their stretcher with the clip's first audio as the playback graph builds them, on the message thread, but read the file cache without waiting. A file region not yet cached primed them with silence, and they kept that state until repositioned, so the first play of a warped clip after a graph build lost its first stretcher latency: about 150 ms in Complex, half a second in Beats.
- **After.** While the graph builds a clip's readers, the cache reader waits for the audio, up to two seconds as an offline render's up to five, and once they are built it reads without waiting again, as real time needs.
- **Evidence.** `warp-modes-acceptance` plays a warped clip's first Arrangement play and first Session launch in Complex and Beats and finds each sounding as the next play does, near the unwarped clip; without the patch a Complex clip's first launch began 6946 samples late ([warp contract](research/warp-contract.md)).

## Tracktion render job lifetimes patch

`cmake/PatchRenderJobs.cmake` patches one pinned file, after every earlier patch:

- `tracktion_RenderManager.cpp`: input `0d8bac1242b5…` (upstream), patched `108919a7f97f…`.

No other patch touches the file. The full hashes are in the index below.

- **Before.** The background job pool does not own a RenderManager job. A job cancelled while it waited in the pool, for example a reversed clip's render when an undo deletes the clip a moment later, handled its completion message as finished and released itself while still queued. A job cancelled before its start message still joined the pool. The pool's next pick then read the deleted job. AddressSanitizer reports this as a heap use after free in `juce::ThreadPool::pickNextJobToRun`; in ordinary builds it was the intermittent crash in `juce::ThreadPool::runNextJob` that the UI suite hit after reversing and bouncing clips.
- **After.** A job cancelled before it starts no longer joins the pool. A finished or cancelled job leaves the pool's queue before it is released (`ThreadPool::removeJob` without waiting); one a pool thread has taken waits, as a running job already did, and is released after its thread lets it go.
- **Evidence.** `audio-clips-acceptance` imports and reverses sixteen new sources at a time, twice the pool's threads, and undoes it all at once, waiting 0, 2 and 5 ms, then renders a reversed clip. Under AddressSanitizer the pinned engine reports the use after free on the first such round; with the patch it runs clean.

## Tracktion clip automation bypass patch

`cmake/PatchClipAutomationBypass.cmake` patches two pinned files, after every earlier patch:

- `tracktion_AutomatableParameter.cpp`: input `fd6b4d431d6d…` (after the clip envelope patch, which accepts the new hash), patched `d2a1876cc068…`.
- `tracktion_AutomationCurveList.cpp`: input `1c2b5cd43383…` (upstream), patched `ac33fb50883e…`.

The full hashes are in the index below.

- **Before.** A clip's curve source on a parameter stopped only when all three of its curves (absolute, relative and scale) were bypassed. With one bypassed and the others merely empty, the bypassed curve still set the parameter on the audio thread, and the message thread's value queries ignored bypassing altogether.
- **After.** A bypassed curve leaves the parameter alone in both places, as a bypassed track curve does. Halftrack bypasses a Session clip's automation envelope to override it when its control is moved (see the [automation capture contract](research/automation-capture-contract.md)).
- **Evidence.** `automation-capture-acceptance` renders a tone through a Session clip whose volume automation is overridden, then relaunched.

<!-- dependency-index:start -->

Generated by `scripts/dependency_index.py` from the build; `tests/test_dependencies.py` checks it is current.

| Pinned archive | Revision | Archive SHA-256 |
| --- | --- | --- |
| juce | `37c894f83d379179b2070d437ccd0f1cd9af9576` | `42524a87f3569273ab7286545816ad1f0cc95240504a7d4464ef1e32c50692aa` |
| tracktion | `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f` | `d4f0af1514b55f147c13dd68c9ac0effc839a5270541653731e9ad3b23d9fd41` |

| Patch script | Hash | SHA-256 |
| --- | --- | --- |
| `cmake/PatchAsyncPlugin.cmake` | accepted input | `9e489383996f1e5da43529f3bf2ce4f12818bef01e17378a014b07144688b01c` |
| `cmake/PatchAsyncPlugin.cmake` | accepted input | `b13b051c3c7b2c098a43aef53d0607be26dbfab0b948884b11d638518ab14476` |
| `cmake/PatchAsyncPlugin.cmake` | accepted input | `f01dd4643715e585904b1adc15b38334644ca82bb4567e2d6623e0b3478c92e7` |
| `cmake/PatchChildProcess.cmake` | original_0 | `697e20d898c9bee6d8f2e87bd2763fe086118c8dd207b36e981d342289911933` |
| `cmake/PatchChildProcess.cmake` | patched_0 | `870a7ac9264e8d53dba2a08ff56e20a8afd760e9764c0311588842408f1fde32` |
| `cmake/PatchChildProcess.cmake` | original_1 | `a8e7e6de59390d48c4b44a8240804e82d4276f57d8aefe51199464c0ea4e53c1` |
| `cmake/PatchChildProcess.cmake` | patched_1 | `9563a5c0cb0bfbf90c307742e1a2afbff820d5daca0cbfaff5955e0c699cebef` |
| `cmake/PatchClipActivation.cmake` | original0 | `b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76` |
| `cmake/PatchClipActivation.cmake` | patched0 | `a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2` |
| `cmake/PatchClipActivation.cmake` | original1 | `5bfeb512499f922cbe1acb464366c37a55faec511b4b5ce959b6fd4e8ce17a95` |
| `cmake/PatchClipActivation.cmake` | patched1 | `e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820` |
| `cmake/PatchClipActivation.cmake` | original2 | `1be178ac571b28ba4c85f15db946c37cfb16209a05c0efcc1061e790fc94aa09` |
| `cmake/PatchClipActivation.cmake` | patched2 | `0e3af9e8d1c1ba7aca706181876b8f6ccce5d31910f8d9048f63aaaae7a417e4` |
| `cmake/PatchClipActivation.cmake` | original3 | `161f6183ef28aa1c64f6862d7be299b8ce17b413a7b67c759f9112abeb16b5e8` |
| `cmake/PatchClipActivation.cmake` | patched3 | `208a3f2a6fd88470544538a01fc03a0b447296ce55022be1b0ece89ca4244e98` |
| `cmake/PatchClipActivation.cmake` | downstream0 | `2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0` |
| `cmake/PatchClipActivation.cmake` | downstream0 | `04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269` |
| `cmake/PatchClipActivation.cmake` | downstream0 | `2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9` |
| `cmake/PatchClipActivation.cmake` | downstream1 | `012a4ac8ea6c65ffb647e8d0244ecf5c0ce62d6f1841e72009c14309cb3307e1` |
| `cmake/PatchClipAutomationBypass.cmake` | original0 | `fd6b4d431d6d47a1478b972e419d8a0f10c06be9ca4e7695385dde9420767de1` |
| `cmake/PatchClipAutomationBypass.cmake` | patched0 | `d2a1876cc068b09b26def43b0dce3cb3585afdafd2aa313920d74b2c20cf3164` |
| `cmake/PatchClipAutomationBypass.cmake` | original1 | `1c2b5cd4338376b342ef856e811762b396bf6fa40dd2cee57a8723a5a62f3987` |
| `cmake/PatchClipAutomationBypass.cmake` | patched1 | `ac33fb50883eb5a6c9115b001f7069272f8133abd1ac03abcbb1212376992434` |
| `cmake/PatchClipEnvelopes.cmake` | original0 | `bd3609d9b42fa1312e775c48488307639894a79528c1d84b1909c050d1ae9aad` |
| `cmake/PatchClipEnvelopes.cmake` | patched0 | `fd6b4d431d6d47a1478b972e419d8a0f10c06be9ca4e7695385dde9420767de1` |
| `cmake/PatchClipEnvelopes.cmake` | downstream0 | `d2a1876cc068b09b26def43b0dce3cb3585afdafd2aa313920d74b2c20cf3164` |
| `cmake/PatchClipFades.cmake` | original0 | `a0c004dd6f3196d3df4808f891878b8816df98269d065bbecd8e6e16eba6a522` |
| `cmake/PatchClipFades.cmake` | patched0 | `a66bb52e2ca925acac9168aaed6f14eb52d7ff3e0058b9e660fa79cb20e09dbd` |
| `cmake/PatchClipFades.cmake` | original1 | `546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3` |
| `cmake/PatchClipFades.cmake` | patched1 | `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` |
| `cmake/PatchClipFades.cmake` | original2 | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchClipFades.cmake` | patched2 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchClipFades.cmake` | downstream1 | `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6` |
| `cmake/PatchClipFades.cmake` | downstream2 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchClipFades.cmake` | downstream2 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchClipGain.cmake` | accepted input | `b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76` |
| `cmake/PatchClipGain.cmake` | accepted input | `a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2` |
| `cmake/PatchClipGain.cmake` | accepted input | `2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0` |
| `cmake/PatchClipGain.cmake` | accepted input | `04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269` |
| `cmake/PatchClipGain.cmake` | accepted input | `2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9` |
| `cmake/PatchClipGain.cmake` | accepted input | `1b92d35da33ca2495f3a2627f4a985c54c651285370e63f036491de953cdd980` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `8f7ae3dd2f45939efb36fd15f5fd1c64f82d0ae6d543dab7e003a9419b54b8c5` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `933f35baa4ded602872777489b1661ca15a668de49f901fb0dc1701e94528ad7` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `f02cd152ab14ced230c3102a5a8aff6c98615a0ecdf139c33db85dd1982398e2` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `b5565cf52845479493ece086382d21c678b2f17ae93526f6ebf756169e03cdcc` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `de63d10123c0f286e8811a8f8a8d166b373a1be741cde8a0a34599e707d9c861` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `cf1765d05ea1a9484450cce0045b77f1be3f26ecc816619ab675fc2e273022ea` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `3afeace9d9eff030b8cb83b70850c6cc9c8c575f2c07c05115807f47407231fa` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `565b43eb8aef36972755c7b509bc9e6419d04e19ba7071fbd0c62581879c55f1` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `85f01686fff5abfb6a7e54fe0961c50d8ae362617007eda4a9dba7f7c93ac2eb` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `6bc90a7dae60e9a0ab3d01fd77b0f90377b8933a9fe11bccbb38b31f471f060d` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchClipLauncher.cmake` | accepted input | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchCountIn.cmake` | original0 | `48dfd7ac69e95e5257734c24783c9c867c9c499d8af4e396cbafb826d84a5a33` |
| `cmake/PatchCountIn.cmake` | patched0 | `d391c913e0804f6b0d02ff78202f519a20a5c7c4c6ff1a58681a2773bd7fd23d` |
| `cmake/PatchCountIn.cmake` | original1 | `ecaf4380982c22801e1e7a7e0b6d0e76965f36cba091605b32039f6093215c7c` |
| `cmake/PatchCountIn.cmake` | patched1 | `385e1d6ad1551a3b38c7d8860c50cd9b364c46f40aecd3693f441b7fa7ebb57a` |
| `cmake/PatchCountInStart.cmake` | original0 | `d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f` |
| `cmake/PatchCountInStart.cmake` | patched0 | `cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be` |
| `cmake/PatchCountInStart.cmake` | downstream0 | `9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4` |
| `cmake/PatchCue.cmake` | original0 | `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` |
| `cmake/PatchCue.cmake` | patched0 | `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6` |
| `cmake/PatchCue.cmake` | original1 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchCue.cmake` | patched1 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchCue.cmake` | downstream1 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchEditSort.cmake` | accepted input | `fcad3688f77edb794f819be0e8a070d249a1396bc32278b979a4095a68a541e3` |
| `cmake/PatchEditSort.cmake` | accepted input | `7eaef085fa9693bcb82435b0965ad6b87f0cecb9251517bffe05c30a5c8aacf3` |
| `cmake/PatchEditSort.cmake` | accepted input | `cc332f9847996d75c8af0f20c04557692a22bd1c341afb94ac5aacabaed549db` |
| `cmake/PatchEditSort.cmake` | accepted input | `35fbc1ab47d65d5551f67ce283c6ebb70f6388acf9682bc8b1e7939b82218d90` |
| `cmake/PatchEditSort.cmake` | accepted input | `b40eeba8bf5d812cad05ce4485bab191ce39b612d6131b5367ef4b4bf868cb11` |
| `cmake/PatchEditSort.cmake` | accepted input | `2fbdcae339880e99bb1a83005f7063d896f263026604b3e7b9656a5ac3d972c9` |
| `cmake/PatchFaderLaws.cmake` | original0 | `b772588adb0e179a9af328e89cbb11112e89b99f82a112a9cfe18e08a0b0dc05` |
| `cmake/PatchFaderLaws.cmake` | patched0 | `9e525d79f453f03b3ebc497c8a944bef2f7a3cd0673fc75c749b9581b5997c35` |
| `cmake/PatchFaderLaws.cmake` | original1 | `5073e77b9eccebfbca7d7eb3e151744969305929ee07d7c080a9000031563372` |
| `cmake/PatchFaderLaws.cmake` | patched1 | `a2f14631df98ccc833e221e57a6e8137ea3ad4282fe9ec18416ed842e22c8b9b` |
| `cmake/PatchFaderLaws.cmake` | original2 | `9d343474d1f22add0a39d159e9ec5fc163b6856d9cf89648fda03c9e687ba9c8` |
| `cmake/PatchFaderLaws.cmake` | patched2 | `13d09a32bd3c5fda3e7488b5664415a316030c72440c0a581c119281faad1388` |
| `cmake/PatchFaderLaws.cmake` | original3 | `0f162e59bb4b5924cb19889364823050564bae9b2cc0b839f8907842569eb812` |
| `cmake/PatchFaderLaws.cmake` | patched3 | `dc7238c35191465c52d0adc9cd304ed95490b44869bf69b10d369fe22b5995c2` |
| `cmake/PatchFaderLaws.cmake` | original4 | `58d79bb59e402f758244e9cba74e2ea49b10c293b830d5899f3842f9d2ed5919` |
| `cmake/PatchFaderLaws.cmake` | patched4 | `d959b60b746ea5427d6e3eff75c7889198295be03d0ce6eeb19c5867c36dc301` |
| `cmake/PatchFollowNoAction.cmake` | original0 | `02b464d6810afd73e644fddce42c3b5a9ee63850bb7b2c56427b50337ea8c846` |
| `cmake/PatchFollowNoAction.cmake` | patched0 | `e09f9b565661defe66fea39d770f53b9efc8f74f6a78c8d5a91babc51d08d286` |
| `cmake/PatchFreezeTails.cmake` | original0 | `305c2c08e96648615f06700339ecbecba95da529fcd0a61812158d3941aff696` |
| `cmake/PatchFreezeTails.cmake` | patched0 | `0511a660f3411abc72afe1bcc6c8bf12e5e152f4d79174db70d021456197b985` |
| `cmake/PatchFreezeTails.cmake` | original1 | `265b6bee4a3fe7d043f7c591e056bed95b1d3f10d3a91356a70ba3c3713b294b` |
| `cmake/PatchFreezeTails.cmake` | patched1 | `c6802d754da6f0c6abde0fb290bb69a6136c3528472fad0992b3e0307d1b2a9a` |
| `cmake/PatchFreezeTails.cmake` | original2 | `57e58a225e0f433fb96920f03731ac37abe805205420e49e5c4ef7f0276186a4` |
| `cmake/PatchFreezeTails.cmake` | patched2 | `9d09d2ac4b345d6d59d9c9c9b1bf809badf5ce2c08e2bb6961794309350a8bae` |
| `cmake/PatchFreezeTails.cmake` | original3 | `b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8` |
| `cmake/PatchFreezeTails.cmake` | patched3 | `cf1765d05ea1a9484450cce0045b77f1be3f26ecc816619ab675fc2e273022ea` |
| `cmake/PatchGroupOutputs.cmake` | original0 | `c373a1d823daf114fbdeb9ed826ab5d5fc3c39915ca15a19f5c951649d96c142` |
| `cmake/PatchGroupOutputs.cmake` | patched0 | `19d0e8ef0c8a1ef0db3f5962ab0dfb4f35e3ff2ce10e4e9b24f959bcf3bc6815` |
| `cmake/PatchGroupOutputs.cmake` | original1 | `9350634aa81fce9c3269142a26bc6ecb2f56589a6e0ac5dcc27ce10e7d769a49` |
| `cmake/PatchGroupOutputs.cmake` | patched1 | `b1623886a9bdfa41a210a1bd698475185cb193d0b665a2219c2ccaaf037bb0cc` |
| `cmake/PatchGroupOutputs.cmake` | original2 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchGroupOutputs.cmake` | patched2 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchInputPlacement.cmake` | original | `76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7` |
| `cmake/PatchInputPlacement.cmake` | patched | `b7ea47bf3f5680c433197eaa3426c459ffbdf58ae6f1a513708af52d1b8feb2d` |
| `cmake/PatchInputRecordingState.cmake` | original | `9e3041acdd4712ce40f4b0701b74e43ed83f091d4fc2435422b5eed73e795ad7` |
| `cmake/PatchInputRecordingState.cmake` | patched | `18cfec9e9fd6c2af4ea15af41e023494d442cf2e586f671d84a0b8d93ebbc7e3` |
| `cmake/PatchInputTakeRescan.cmake` | original | `49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498` |
| `cmake/PatchInputTakeRescan.cmake` | patched | `896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36` |
| `cmake/PatchInputTakeRescan.cmake` | downstream | `e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91` |
| `cmake/PatchLaunchRepeat.cmake` | original0 | `060884330b8e511f0d207d7b0dc2e8f506ef846b1eff19ca795422f0cf805a24` |
| `cmake/PatchLaunchRepeat.cmake` | patched0 | `1cd20c6ace77ed96f2cc99ef41754bfc066d2745c9e91e4f36d14c2ca7009693` |
| `cmake/PatchLaunchVelocity.cmake` | original0 | `da905fda9f13b54127f59441550d2ac0c2c2268336f05ec28d13bfb9093542fd` |
| `cmake/PatchLaunchVelocity.cmake` | patched0 | `c76b3c71ed1e6c17c28962a4f6f5598b4a37b4af9141add8fd9bbe94d94480a6` |
| `cmake/PatchLaunchVelocity.cmake` | original1 | `9d5d84386b23635aa010b0bcfffd93cc22a0539a916eb1b71f7498429bc72c8f` |
| `cmake/PatchLaunchVelocity.cmake` | patched1 | `288596a5359fad12c2cac73d2b37044a1e3b79b0f0142a678388a88125efcbfb` |
| `cmake/PatchLaunchVelocity.cmake` | original2 | `decfd887689b87caf5b5140ad5494309253573a66ed3640aba8562ae94d12c5e` |
| `cmake/PatchLaunchVelocity.cmake` | patched2 | `dd5682389efa47a9245ae561605b209dcb3b2ef1e1743261337a979a8dd7a783` |
| `cmake/PatchLoopPlayback.cmake` | original0 | `e592ff87c34b02bc3934fb39d470a9e12bf168601c4d7e3d9bf950b2050e04d5` |
| `cmake/PatchLoopPlayback.cmake` | patched0 | `29b0133c12b035f159b8464765e6751f57e0a7ac75c8b75a5bfce4403cbe06ee` |
| `cmake/PatchLoopPlayback.cmake` | original1 | `cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be` |
| `cmake/PatchLoopPlayback.cmake` | patched1 | `9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4` |
| `cmake/PatchLoopPlayback.cmake` | original2 | `cbc08effa9de1b1143f3e39931ae5c7fe6a2e07a27b23037d9cfc2378dfc3918` |
| `cmake/PatchLoopPlayback.cmake` | patched2 | `8214ed9e16d54647e3d6a6e5085234915286e7ee3b1b806a509e2fc864885aad` |
| `cmake/PatchLoopRecordLength.cmake` | original | `07f46651b67377e32bd962204c27e3f1fe57e819f21cde4e65e1f884d35eb6c0` |
| `cmake/PatchLoopRecordLength.cmake` | patched | `5e43a391154ab64092d83eeeb8dddfc57718a11be23e753e4a1fdb0ec83c931d` |
| `cmake/PatchLoopRecordLength.cmake` | downstream | `d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f` |
| `cmake/PatchLoopRecordLength.cmake` | downstream | `cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be` |
| `cmake/PatchLoopRecordLength.cmake` | downstream | `9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4` |
| `cmake/PatchMeterBars.cmake` | original0 | `a78f2961a868d3595c4a80599610c1fd1af1f29327722614e25ef53847b66664` |
| `cmake/PatchMeterBars.cmake` | patched0 | `93e8dbbd94f466189bda4aec01b5147424caa650c8680a7af09a5ba438ffcbb1` |
| `cmake/PatchMeterBars.cmake` | original1 | `3becd7c4526ace7b2596389bdbb7e04361552ff5038779ddf2948c47731af177` |
| `cmake/PatchMeterBars.cmake` | patched1 | `9bc45be9a73cf706f6ca11f6c173c0143fb4dd4069e3cbd0b85b4e850cbd2b5b` |
| `cmake/PatchMeterBars.cmake` | original2 | `89c978d830b759cf006bd56c2a125d144479bb77b390f0eb64d2be2d74373941` |
| `cmake/PatchMeterBars.cmake` | patched2 | `8321bcd0ec19e6e80ed52ef5af28c6d168c13cacbc984784fc77eb7e0be45ad3` |
| `cmake/PatchMetronomeRhythm.cmake` | original0 | `cfca43d540e922c63be15bddb94fc1f9db1cc5c9f199bd3bc96dde86ae030613` |
| `cmake/PatchMetronomeRhythm.cmake` | patched0 | `dd21794c667566ec8d0c8f04c16858770f882e9156f410d91889b3361240814a` |
| `cmake/PatchMetronomeRhythm.cmake` | original1 | `487e4ef7c67ab8ee3dd5d6dcd27266087306bd4c5adf735b74cceab3e08a7921` |
| `cmake/PatchMetronomeRhythm.cmake` | patched1 | `89c978d830b759cf006bd56c2a125d144479bb77b390f0eb64d2be2d74373941` |
| `cmake/PatchMetronomeRhythm.cmake` | downstream1 | `8321bcd0ec19e6e80ed52ef5af28c6d168c13cacbc984784fc77eb7e0be45ad3` |
| `cmake/PatchMidiDevices.cmake` | original0 | `09b64a7d5a5e58b45370424262f61a562f620affdb9e2b92829666d0a024347d` |
| `cmake/PatchMidiDevices.cmake` | patched0 | `fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf` |
| `cmake/PatchMidiDevices.cmake` | original1 | `7ab4590fa518e44b977617dc003597adf84ade0021e6ce01bcf4ebcb6937af50` |
| `cmake/PatchMidiDevices.cmake` | patched1 | `eaabb54af6e469690fb667efe36b6c88d0f9eefd42c7e8d34ac280271b4f5441` |
| `cmake/PatchMidiDevices.cmake` | original2 | `04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32` |
| `cmake/PatchMidiDevices.cmake` | patched2 | `49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498` |
| `cmake/PatchMidiDevices.cmake` | original3 | `e72a87226f1448aa73fe4eca1cb1cc1ad8245bcccc38f8dd1f362b669ad467d1` |
| `cmake/PatchMidiDevices.cmake` | patched3 | `c4221217f5d2c1dfa57a02268d6a762f6c733ea95a17b9c53371a3c035a5a617` |
| `cmake/PatchMidiDevices.cmake` | downstream3 | `baa5513d1ecb6d3aad29b74c85ef3393b118892654502a49903330b48877a2f6` |
| `cmake/PatchMidiDevices.cmake` | downstream2 | `896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36` |
| `cmake/PatchMidiDevices.cmake` | downstream2 | `e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91` |
| `cmake/PatchMidiDevices.cmake` | downstream0 | `5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa` |
| `cmake/PatchMidiDevices.cmake` | downstream0 | `546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3` |
| `cmake/PatchMidiDevices.cmake` | downstream0 | `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` |
| `cmake/PatchMidiDevices.cmake` | downstream0 | `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6` |
| `cmake/PatchMidiInputActivation.cmake` | original_cpp | `f893b43a88aa1897164b1f6fe45516b4e5e7e5db435afd09292da34870f91b16` |
| `cmake/PatchMidiInputActivation.cmake` | patched_cpp | `a3e623634363acecd4096d29498b8237af3a468015aeccdf48716d4dab115866` |
| `cmake/PatchMidiInputActivation.cmake` | state_preserving_cpp | `335da793205ef43c19384f666e0c13bb3d7117595d12b60676f66e5af2027dec` |
| `cmake/PatchMidiInputActivation.cmake` | receipt_age_cpp | `a1660920bf4c22133e1a5c33264c478ee466cedfccca26edf4be79248aa81c2f` |
| `cmake/PatchMidiInputActivation.cmake` | original_h | `976acfb6df698e01935bdb3cfad7761da884e07b41875edb325b2ee7a10879a9` |
| `cmake/PatchMidiInputActivation.cmake` | patched_h | `82f9fae59e49a0293f5aa0e0ba8926faa359df5d33fafc92226480083fb049e5` |
| `cmake/PatchMidiInputActivation.cmake` | previous_cpp | `b417dbcd6f6dafba3078eaa408b0074d0cd6ba50e7e43f123d5a4f283b4af4a2` |
| `cmake/PatchMidiInputActivation.cmake` | accurate_receipt_cpp | `b0e8e73a5ab9f5dd1d13e96c2adc84313ed3a579ddb8ca7d80b6564a2c497000` |
| `cmake/PatchMidiInputActivation.cmake` | accurate_receipt_h | `6103673d444ed07506ab92f96fbfb835639ececeb834c5b936d6bd0838d146ef` |
| `cmake/PatchMidiInputActivation.cmake` | accepted input | `7f8dd4ed48893d3ba46e4b09a44404dd9bcf334f772aa09be98a06573892cd74` |
| `cmake/PatchMidiInputActivation.cmake` | accepted input | `90dec853c902a4349a64b450007f314c7380b5f414494d482385c5f6b33cacde` |
| `cmake/PatchMidiInputChannel.cmake` | original0 | `4caa2c5b2dfa88a8ff9f0d2ca15830e83c432ac27e3122de275c75875d1fe54e` |
| `cmake/PatchMidiInputChannel.cmake` | patched0 | `b35855d978d2444c773834360c345c8df19d70451adc4f5c28a9ffd50c926dbe` |
| `cmake/PatchMidiInputChannel.cmake` | original1 | `5f31534136e50526dd3c30aa82a679e8f6a87a90ec1b5aaca4fbf4ac07bb8fba` |
| `cmake/PatchMidiInputChannel.cmake` | patched1 | `c5181f604c9fd59d45712fd68c454645d36e3aae26f29d870038222fbb84eec4` |
| `cmake/PatchMidiInputChannel.cmake` | original2 | `6103673d444ed07506ab92f96fbfb835639ececeb834c5b936d6bd0838d146ef` |
| `cmake/PatchMidiInputChannel.cmake` | patched2 | `90dec853c902a4349a64b450007f314c7380b5f414494d482385c5f6b33cacde` |
| `cmake/PatchMidiInputChannel.cmake` | original3 | `b0e8e73a5ab9f5dd1d13e96c2adc84313ed3a579ddb8ca7d80b6564a2c497000` |
| `cmake/PatchMidiInputChannel.cmake` | patched3 | `7f8dd4ed48893d3ba46e4b09a44404dd9bcf334f772aa09be98a06573892cd74` |
| `cmake/PatchMidiPorts.cmake` | original0 | `5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa` |
| `cmake/PatchMidiPorts.cmake` | patched0 | `546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3` |
| `cmake/PatchMidiPorts.cmake` | downstream0 | `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` |
| `cmake/PatchMidiPorts.cmake` | downstream0 | `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6` |
| `cmake/PatchMidiPorts.cmake` | original1 | `330630e1e82ae881bf7c65cce26bebe7961a26531174d1ca53b45021b7658b82` |
| `cmake/PatchMidiPorts.cmake` | patched1 | `03dbf07bcf6a2483a744e2f1751f452dead34c6b7d89ba27d15db093790a2a64` |
| `cmake/PatchMidiPorts.cmake` | original2 | `896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36` |
| `cmake/PatchMidiPorts.cmake` | patched2 | `e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91` |
| `cmake/PatchMidiPorts.cmake` | original3 | `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` |
| `cmake/PatchMidiPorts.cmake` | patched3 | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchMidiPorts.cmake` | downstream3 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchMidiPorts.cmake` | downstream3 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchMidiPorts.cmake` | downstream3 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchMidiReleaseVelocity.cmake` | original | `161898bf64101cb385b7ef5a99d18bedd8ad5909fa0d422a4ae734ec70fe1a79` |
| `cmake/PatchMidiReleaseVelocity.cmake` | patched | `93b05169b4008e66831d4ee3a9d42d065b32b011cd5f27c1dd04fd0a0854dcbd` |
| `cmake/PatchMidiReleaseVelocity.cmake` | downstream | `ed53b9158c0162bef0c3c041c473bd4e6e740bad5eee65c61e4f3e5775436949` |
| `cmake/PatchMidiVariations.cmake` | original0 | `fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf` |
| `cmake/PatchMidiVariations.cmake` | patched0 | `5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa` |
| `cmake/PatchMidiVariations.cmake` | downstream0 | `546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3` |
| `cmake/PatchMidiVariations.cmake` | downstream0 | `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` |
| `cmake/PatchMidiVariations.cmake` | downstream0 | `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6` |
| `cmake/PatchMidiVariations.cmake` | original1 | `06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8` |
| `cmake/PatchMidiVariations.cmake` | patched1 | `bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766` |
| `cmake/PatchMidiVariations.cmake` | downstream1 | `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` |
| `cmake/PatchMidiVariations.cmake` | downstream1 | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchMidiVariations.cmake` | downstream1 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchMidiVariations.cmake` | downstream1 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchMidiVariations.cmake` | downstream1 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchMidiVariations.cmake` | original2 | `e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820` |
| `cmake/PatchMidiVariations.cmake` | patched2 | `012a4ac8ea6c65ffb647e8d0244ecf5c0ce62d6f1841e72009c14309cb3307e1` |
| `cmake/PatchMpeTiming.cmake` | original0 | `93b05169b4008e66831d4ee3a9d42d065b32b011cd5f27c1dd04fd0a0854dcbd` |
| `cmake/PatchMpeTiming.cmake` | patched0 | `ed53b9158c0162bef0c3c041c473bd4e6e740bad5eee65c61e4f3e5775436949` |
| `cmake/PatchNegativeTime.cmake` | original | `7a646528d5b260e2d681205f2b6220e2632798578b72c3d6c6d393618c71e237` |
| `cmake/PatchNegativeTime.cmake` | patched | `e711c54e67db07b1f4347986e3c4f062a6851453d0aa0a371448a393abd9482f` |
| `cmake/PatchPlaybackContinues.cmake` | original0 | `d172d8de5ee35392557d3e0e363893283de2d9d0392d7e58114b3c8c3ee09b4b` |
| `cmake/PatchPlaybackContinues.cmake` | patched0 | `5b4993f48aa8700aaa3f7c4e27790c41042536951e20d4d650951b73095ae327` |
| `cmake/PatchPlaybackContinues.cmake` | original1 | `1742b29b2e581de7583892454d08e988dab60598254d5669164acbd9918bb019` |
| `cmake/PatchPlaybackContinues.cmake` | patched1 | `7d28b4424f5c1bb4140631529a219e9c4f60e58ee1c293a2faf9eb10a890ad70` |
| `cmake/PatchPlaybackContinues.cmake` | original2 | `5e43a391154ab64092d83eeeb8dddfc57718a11be23e753e4a1fdb0ec83c931d` |
| `cmake/PatchPlaybackContinues.cmake` | patched2 | `d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f` |
| `cmake/PatchPlaybackContinues.cmake` | downstream2 | `cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be` |
| `cmake/PatchPlaybackContinues.cmake` | downstream2 | `9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4` |
| `cmake/PatchPluginScanner.cmake` | original_0 | `94eb54614af016e38e67af2e27f870e0ad14aa6bf83b42638d7b36c8291f8d8c` |
| `cmake/PatchPluginScanner.cmake` | previous_0 | `521a1479001bb633c73f5a4780fe85418128728e1dab2f3c0abe43d84c4b0895` |
| `cmake/PatchPluginScanner.cmake` | previous_current_0 | `a4498068deaeec17081d992825d1df2faf49a1e6c289b91d0fc9a8a54aa7071c` |
| `cmake/PatchPluginScanner.cmake` | patched_0 | `f26479c74d2e829a26997f46f6225a8ea2c3887738e560392f06ff48373c464f` |
| `cmake/PatchPluginScanner.cmake` | original_1 | `71025051424caea898d1f07a8a6a67b4d7500b4d1c3991a734bcad0188c6ee18` |
| `cmake/PatchPluginScanner.cmake` | patched_1 | `09b64a7d5a5e58b45370424262f61a562f620affdb9e2b92829666d0a024347d` |
| `cmake/PatchPluginScanner.cmake` | accepted input | `fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf` |
| `cmake/PatchPluginScanner.cmake` | accepted input | `5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa` |
| `cmake/PatchPluginScanner.cmake` | accepted input | `546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3` |
| `cmake/PatchPluginScanner.cmake` | accepted input | `d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95` |
| `cmake/PatchPluginScanner.cmake` | accepted input | `296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6` |
| `cmake/PatchRecordingErrors.cmake` | original | `3547f2134733cde3032237fbac4852e217067b4dbb9642235128843738f34ac1` |
| `cmake/PatchRecordingErrors.cmake` | patched | `76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7` |
| `cmake/PatchRecordingErrors.cmake` | downstream | `b7ea47bf3f5680c433197eaa3426c459ffbdf58ae6f1a513708af52d1b8feb2d` |
| `cmake/PatchRemoteMidi.cmake` | original0 | `e99fb2a7f68fb187d50e7d7f72fab502b4b03ca838eb82d560e6e101905c7101` |
| `cmake/PatchRemoteMidi.cmake` | patched0 | `4caa2c5b2dfa88a8ff9f0d2ca15830e83c432ac27e3122de275c75875d1fe54e` |
| `cmake/PatchRemoteMidi.cmake` | original1 | `c1c06cdd51d2e5bc7f7fae0696f9d0752757740cb33ca41b7d562acab68ce6c9` |
| `cmake/PatchRemoteMidi.cmake` | patched1 | `5f31534136e50526dd3c30aa82a679e8f6a87a90ec1b5aaca4fbf4ac07bb8fba` |
| `cmake/PatchRemoteMidi.cmake` | original2 | `c4221217f5d2c1dfa57a02268d6a762f6c733ea95a17b9c53371a3c035a5a617` |
| `cmake/PatchRemoteMidi.cmake` | patched2 | `baa5513d1ecb6d3aad29b74c85ef3393b118892654502a49903330b48877a2f6` |
| `cmake/PatchRemoteMidi.cmake` | downstream0 | `b35855d978d2444c773834360c345c8df19d70451adc4f5c28a9ffd50c926dbe` |
| `cmake/PatchRemoteMidi.cmake` | downstream1 | `c5181f604c9fd59d45712fd68c454645d36e3aae26f29d870038222fbb84eec4` |
| `cmake/PatchRenderJobs.cmake` | original0 | `0d8bac1242b5ebc27e835959b79dd0a60ee1ce4fefc9ddfc5a47e3784432de24` |
| `cmake/PatchRenderJobs.cmake` | patched0 | `108919a7f97f21e77dbb916b4e4648a952885430526d418f0454bc6c9d65a9d6` |
| `cmake/PatchRenderLatency.cmake` | original | `40f8e18c146c05e93dc40a54602a6af674ca81a6253120e4782e9a59ae5af6a2` |
| `cmake/PatchRenderLatency.cmake` | previous | `cde4f3dbc50f0f1378e6f7b5f638422ba985333bb8abb3e8a6a4d060320a10e5` |
| `cmake/PatchRenderLatency.cmake` | patched | `57e58a225e0f433fb96920f03731ac37abe805205420e49e5c4ef7f0276186a4` |
| `cmake/PatchRenderLatency.cmake` | downstream | `9d09d2ac4b345d6d59d9c9c9b1bf809badf5ce2c08e2bb6961794309350a8bae` |
| `cmake/PatchRenderSendsOnly.cmake` | original0 | `31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074` |
| `cmake/PatchRenderSendsOnly.cmake` | patched0 | `51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchRenderSendsOnly.cmake` | downstream0 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchRenderTrackInputs.cmake` | original0 | `51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48` |
| `cmake/PatchRenderTrackInputs.cmake` | patched0 | `06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8` |
| `cmake/PatchRenderTrackInputs.cmake` | downstream0 | `bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766` |
| `cmake/PatchRenderTrackInputs.cmake` | downstream0 | `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` |
| `cmake/PatchRenderTrackInputs.cmake` | downstream0 | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchRenderTrackInputs.cmake` | downstream0 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchRenderTrackInputs.cmake` | downstream0 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchRenderTrackInputs.cmake` | downstream0 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchRepitch.cmake` | original0 | `1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57` |
| `cmake/PatchRepitch.cmake` | patched0 | `63336ba34baf1de28fce2e8bc0aff7b06d015ba45f9776cc62b0e7716449c455` |
| `cmake/PatchReverseChannels.cmake` | accepted input | `1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57` |
| `cmake/PatchReverseChannels.cmake` | accepted input | `63336ba34baf1de28fce2e8bc0aff7b06d015ba45f9776cc62b0e7716449c455` |
| `cmake/PatchReverseChannels.cmake` | accepted input | `e87f781fd11f8f90236b416bd762577105439ef11eb13f63f1d9770b7ac49826` |
| `cmake/PatchSidechainTaps.cmake` | original0 | `bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766` |
| `cmake/PatchSidechainTaps.cmake` | patched0 | `d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647` |
| `cmake/PatchSidechainTaps.cmake` | downstream0 | `1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a` |
| `cmake/PatchSidechainTaps.cmake` | downstream0 | `9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c` |
| `cmake/PatchSidechainTaps.cmake` | downstream0 | `bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf` |
| `cmake/PatchSidechainTaps.cmake` | downstream0 | `05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc` |
| `cmake/PatchStretchAlignment.cmake` | original0 | `8714b1c76395b671f77cf8d91e3f8686bdc8ed3d1b1f38faf20e9f7c992328ee` |
| `cmake/PatchStretchAlignment.cmake` | patched0 | `823e52adaf06223d36c886c754c0b464f9d0f93ba28d9bbb9d379ac67b345a30` |
| `cmake/PatchStretchAlignment.cmake` | downstream0 | `27e50e271e9b07c9f30c50951d387b462dbdb0684bfb50c46d81e454ff4dc3f2` |
| `cmake/PatchStretchAlignment.cmake` | original1 | `b2a3ca555d7817dc6e3faf0c5e4daf28adf472288e22010ebf2ecace01e06aa9` |
| `cmake/PatchStretchAlignment.cmake` | patched1 | `51775bb94b0c46021476489a9d4bbbe4a004c029f1652dec53407d09e0f1c530` |
| `cmake/PatchStretchAlignment.cmake` | downstream1 | `9058c56542eacba92ee5890eb63ffdb059faac60baf7e6b7998101ff30e8b9f4` |
| `cmake/PatchStretchAlignment.cmake` | original2 | `4c3774f61fa82ffd7b716790802959b0ff6177e564034714db86d759a8fcd43b` |
| `cmake/PatchStretchAlignment.cmake` | patched2 | `9bba658a860b056e7f5a5d5d94d230662884711f7504b7f42dfaeee121714ffb` |
| `cmake/PatchStretchAlignment.cmake` | original3 | `bd6e380779ea12bae22351081adbbd9ad6bbc134b3088c5e001e1de8038eddb2` |
| `cmake/PatchStretchAlignment.cmake` | patched3 | `32525b4261b040e35ce4ed8644af8b710ca00c3509d91b5a4944b5f3824f09b9` |
| `cmake/PatchStretchAlignment.cmake` | original4 | `a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2` |
| `cmake/PatchStretchAlignment.cmake` | patched4 | `2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0` |
| `cmake/PatchStretchAlignment.cmake` | downstream4 | `04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269` |
| `cmake/PatchStretchAlignment.cmake` | downstream4 | `2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9` |
| `cmake/PatchStretchPriming.cmake` | original0 | `04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269` |
| `cmake/PatchStretchPriming.cmake` | patched0 | `2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9` |
| `cmake/PatchTempoRange.cmake` | original0 | `5a3e45f0fb9f53846edde6aae349ba38544e0618e45008e4994e2543148dcb56` |
| `cmake/PatchTempoRange.cmake` | patched0 | `b5efb441105e6edf8e216fea7c039a7b47331030019de10dad6db9aa3185c30d` |
| `cmake/PatchTempoRange.cmake` | original1 | `9bad3c89735d810e4fc41d7f6ac77fd422249287d96306525ae79458839fd6ab` |
| `cmake/PatchTempoRange.cmake` | patched1 | `c24f67e52af42fa46a551d585825063dde663eb17abe0a2e3236bcebf9037f99` |
| `cmake/PatchTracktion.cmake` | accepted input | `04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32` |
| `cmake/PatchTracktion.cmake` | accepted input | `49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498` |
| `cmake/PatchTracktion.cmake` | accepted input | `896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36` |
| `cmake/PatchTracktion.cmake` | accepted input | `e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91` |
| `cmake/PatchTracktion.cmake` | accepted input | `28f900c36a68f768706bad72ef7d65cac7ff2adc313f7aa0c2b047cab398696e` |
| `cmake/PatchWarpModes.cmake` | original0 | `823e52adaf06223d36c886c754c0b464f9d0f93ba28d9bbb9d379ac67b345a30` |
| `cmake/PatchWarpModes.cmake` | patched0 | `27e50e271e9b07c9f30c50951d387b462dbdb0684bfb50c46d81e454ff4dc3f2` |
| `cmake/PatchWarpModes.cmake` | original1 | `51775bb94b0c46021476489a9d4bbbe4a004c029f1652dec53407d09e0f1c530` |
| `cmake/PatchWarpModes.cmake` | patched1 | `9058c56542eacba92ee5890eb63ffdb059faac60baf7e6b7998101ff30e8b9f4` |
| `cmake/PatchWarpTiming.cmake` | original0 | `2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0` |
| `cmake/PatchWarpTiming.cmake` | patched0 | `04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269` |
| `cmake/PatchWarpTiming.cmake` | downstream0 | `2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9` |
| `cmake/PatchWaveInputStart.cmake` | original | `d08f647d63c6d478dff1bbe93e26477a5e807e47ccc4590a9433dc08f61d10c2` |
| `cmake/PatchWaveInputStart.cmake` | patched | `3547f2134733cde3032237fbac4852e217067b4dbb9642235128843738f34ac1` |
| `cmake/PatchWaveInputStart.cmake` | downstream | `76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7` |
| `cmake/PatchWaveInputStart.cmake` | downstream | `b7ea47bf3f5680c433197eaa3426c459ffbdf58ae6f1a513708af52d1b8feb2d` |

<!-- dependency-index:end -->
