# Dependency inventory

The first preview uses the exact compatible revisions recorded here. New upstream releases do not automatically change the build.

| Component | Revision | License route | Purpose |
|---|---|---|---|
| JUCE | `37c894f83d379179b2070d437ccd0f1cd9af9576` | AGPLv3 | Shared native UI, CoreAudio/ASIO/WASAPI, AU/VST3 hosting, audio formats |
| Tracktion Engine | `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f` | GPLv3-or-later | Streaming, arrangement, graph, recording, automation infrastructure, rendering |
| VST3 SDK bundled by JUCE | See pinned JUCE source | MIT | VST3 hosting and deterministic test plug-in |
| ASIO SDK bundled by JUCE | See pinned JUCE source | GPLv3 option | Windows device backend |
| SoundTouch bundled by Tracktion | See pinned Tracktion source | Upstream bundled license | Available engine stretch implementation; not Ableton Warp equivalence |

JUCE and Tracktion have additional bundled dependencies (including codecs, font rendering, resampling, queues, and allocators). Preserve their source notices. See [pinned JUCE license inventory](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/LICENSE.md) and [pinned Tracktion license](https://github.com/Tracktion/tracktion_engine/blob/07e2d674884293f1a6c8f35fb8d1b9f469ffa41f/LICENSE.md). CMake records archive SHA-256 values and validates downloads.

Original Halftrack code and SVG marks are AGPL-3.0-only. No commercial JUCE or Tracktion license is selected. Their commercial terms apply to different licensing choices and are not a substitute for preserving open-source obligations. A full dependency notice bundle and release distribution review remain required before a packaged public release. No third-party plug-in binaries or vendor sample libraries are redistributed.

VST2 support is not enabled: the historical SDK is not fetched. CLAP support needs its own host adapter. No promise of Intel-only plug-in bridging is made in the Apple Silicon preview.
