# Dependency inventory

The build uses the exact compatible revisions recorded here. New upstream releases do not automatically change the build.

| Component | Revision | License route | Purpose |
|---|---|---|---|
| JUCE | `37c894f83d379179b2070d437ccd0f1cd9af9576` | AGPLv3 | Shared native UI, CoreAudio/ASIO/WASAPI, AU/VST3 hosting, audio formats |
| Tracktion Engine | `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f` | GPLv3-or-later | Streaming, arrangement, native Session launcher, graph, recording, automation infrastructure, rendering |
| VST3 SDK bundled by JUCE | See pinned JUCE source | MIT | VST3 hosting and deterministic test plug-in |
| ASIO SDK bundled by JUCE | See pinned JUCE source | GPLv3 option | Windows device backend |
| Signalsmith Stretch | Bundled by pinned Tracktion revision | MIT | Selected stretching backend; editing and fidelity tests pending |

JUCE and Tracktion have additional bundled dependencies (including codecs, font rendering, resampling, queues, and allocators). Preserve their source notices. See [pinned JUCE license inventory](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/LICENSE.md) and [pinned Tracktion license](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/LICENSE.md). CMake records archive SHA-256 values and validates downloads. The Tracktion revision additionally receives the recording-safety, clip-launcher, and offline-render patches documented below.

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

Original SHA-256: `40f8e18c146c05e93dc40a54602a6af674ca81a6253120e4782e9a59ae5af6a2`; patched SHA-256: `cde4f3dbc50f0f1378e6f7b5f638422ba985333bb8abb3e8a6a4d060320a10e5`. The script accepts only these exact input states, verifies its output, writes LF line endings, and is idempotent. It is separate from the recording and launcher patches and must be included with corresponding modified source when packaging this build.

The initial full-edge VST3 comparison failed in 8.79 seconds. Correcting the view offset made the tested 48000-frame exports at 127 through 635 samples of graph latency match their dry reference exactly; a separate 64-frame export then failed in 14.14 seconds and exposed the partial-block defect. The combined correction passed `latency-acceptance` in **17.40 seconds**: ten boundary exports cover 48000 and 64 frames at delays of 127, 254, 381, 508 and 635 samples, with exact length, finite values, and zero error against the dry graph reference. Four disposable CMake checks also passed: exact apply, repeat apply, unknown-source rejection without mutation, and a CRLF-encoded helper producing the exact LF source hash. These are generated offline export tests; this patch changes no real-time audio callback and does not establish live compensation, external hardware alignment, or arbitrary plug-in behavior.
