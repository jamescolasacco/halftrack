# Dependency inventory

The first preview uses the exact compatible revisions recorded here. New upstream releases do not automatically change the build.

| Component | Revision | License route | Purpose |
|---|---|---|---|
| JUCE | `37c894f83d379179b2070d437ccd0f1cd9af9576` | AGPLv3 | Shared native UI, CoreAudio/ASIO/WASAPI, AU/VST3 hosting, audio formats |
| Tracktion Engine | `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f` | GPLv3-or-later | Streaming, arrangement, graph, recording, automation infrastructure, rendering |
| VST3 SDK bundled by JUCE | See pinned JUCE source | MIT | VST3 hosting and deterministic test plug-in |
| ASIO SDK bundled by JUCE | See pinned JUCE source | GPLv3 option | Windows device backend |
| Signalsmith Stretch | Bundled by pinned Tracktion revision | MIT | Selected stretching backend; editing and fidelity tests pending |

JUCE and Tracktion have additional bundled dependencies (including codecs, font rendering, resampling, queues, and allocators). Preserve their source notices. See [pinned JUCE license inventory](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/LICENSE.md) and [pinned Tracktion license](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/LICENSE.md). CMake records archive SHA-256 values and validates downloads. The Tracktion revision additionally receives the narrow source patch documented below.

Original Halftrack code and SVG marks are AGPL-3.0-only. No commercial JUCE or Tracktion license is selected. Their commercial terms apply to different licensing choices and are not a substitute for preserving open-source obligations. Preview packaging includes complete corresponding source archives with embedded third-party notices. Signed public releases still require distribution review. No third-party plug-in binaries or vendor sample libraries are redistributed.

VST2 support is not enabled: the historical SDK is not fetched. CLAP support needs its own host adapter. No promise of Intel-only plug-in bridging is made in the Apple Silicon preview.

## Tracktion recording-safety patch

`cmake/PatchTracktion.cmake` modifies only the pinned engine's `playback/tracktion_DeviceManager.cpp`. MIDI-device discovery used to clear playback contexts during an active recording, discarding the take. The patch defers automatic and explicit MIDI discovery while any engine context is recording, retries after 100 ms, and resumes discovery after recording stops. It runs on the message thread and preserves the user's scan-interval setting; it does not change the audio callback.

CMake applies this after dependency population for both downloaded archives and `FETCHCONTENT_SOURCE_DIR_TRACKTION` overrides. The script accepts only the exact original or already-patched file, making reconfiguration idempotent; unexpected source fails configuration instead of applying an unverified edit. Original SHA-256: `28f900c36a68f768706bad72ef7d65cac7ff2adc313f7aa0c2b047cab398696e`; patched SHA-256: `04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32`. Dependency updates require reviewing this patch again.

The dependency retains its own notices and license. Preview packages include the modified Tracktion source as well as Halftrack's patch script, so the shipped source corresponds to the built application. MIDI-device changes are deliberately deferred during a take; audio-device disconnection and other graph rebuild paths require separate validation.
