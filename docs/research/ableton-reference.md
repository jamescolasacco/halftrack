# Ableton reference baseline

Research snapshot: 2026-10-13. Baseline: Ableton Live 12 Suite, **12.2.1**, macOS Apple Silicon. This is a planning reference, not a claim of completed feature parity.

## Agreed Halftrack scope

The user wants an independently implemented, open-source DAW with familiar Live workflows, an analog studio identity, and one shared product for macOS and Windows. The accepted decisions are:

- Build **one runnable basic preview** for recording, mixing, plug-in use, and export. Wait for the user's explicit approval of that preview before beginning the full implementation sweep.
- Use a Halftrack project format. Reading, writing, or round-tripping Ableton `.als` files is **not a target**.
- Build Halftrack's own environment for user-created instruments, effects, MIDI tools, and modulation. Existing Max patches, Max binaries, and `.amxd` compatibility are **not implied** by “equivalent.”
- Aim for closely matched sound and behavior where appropriate; do not promise bit-identical recreation of proprietary DSP.
- Share the engine, model, editing behavior, and UI across macOS and Windows, with platform-specific audio, plug-in, and packaging adapters.
- Use a copyleft open-source licensing direction. Exact dependency and distribution compatibility must be checked against the licenses of the selected components.
- Keep the temporary Halftrack logo replaceable. Do not commit Ableton artwork, screenshots, bundled samples, presets, manuals, binaries, or personal device/plug-in inventories.

These are project decisions from the interview, not conclusions inferred from Ableton's documentation.

## Evidence and version boundaries

| Evidence | Result | Limit |
| --- | --- | --- |
| Local reference identity provided by the application audit | Live 12 Suite 12.2.1, build `2025-06-05_c0d59beb04`, Apple Silicon | The research agent did not operate the app. Build date is distinct from release date. |
| Official release notes | 12.2 released June 11, 2025; 12.2.1 released June 19, 2025 | The page is cumulative and now contains later versions. Read the relevant release headings. |
| Official dated PDF | June 2, 2025 URL downloaded and parsed successfully; 913 pages; 76,333,838 bytes | It is a **pre-12.2 document**, not an exact frozen 12.2.1 specification. |
| Current online manual | Useful topic index and supporting reference | It changes within major version 12 and includes features added after 12.2.1. |
| Current edition comparison and Pack catalog | Useful to identify content families | Neither establishes the exact Suite entitlement or installed content at the 12.2.1 baseline. |
| This initial parity ledger | All entries start NOT IMPLEMENTED / UNVERIFIED | Implementation must be demonstrated independently; the ledger does not inspect or infer application code. |

Sources: [release notes](https://www.ableton.com/en/release-notes/live-12/), [dated PDF](https://cdn-resources.ableton.com/resources/pdfs/live-manual/12/2025-06-02/live12-manual-en.pdf), [current manual](https://www.ableton.com/en/live-manual/12/welcome-to-live/), [edition comparison](https://www.ableton.com/en/live/compare-editions/).

The verified PDF's SHA-256 is:

```text
42fbd3794df0d69aaf522aea589bf08dd3dc8db3eb0e135cab041093875db976
```

Only the hash, source URL, and research observations belong in this repository. The downloaded PDF remains a temporary research input.

The dated PDF's contents name the earlier Auto Filter, and its device description on page 455 describes that earlier filter. Its contents also retain the older freeze/flatten organization. This proves that the timestamped PDF must be supplemented with the 12.2 release changes. It must not silently override observations from installed 12.2.1.

For baseline reconciliation, prioritize: (1) repeatable observations in the installed 12.2.1 app, (2) official 12.2 and 12.2.1 release notes, (3) dated earlier documentation for unchanged topics, and (4) current documentation with a recorded version check. Disagreements remain UNVERIFIED until resolved.

## The 12.2 overlay and later-version exclusions

The important 12.2 overlays include revised Auto Filter, bounce commands, browser tagging/navigation changes, keyboard automation editing, device updates, scene follow-action changes, and Max 9.0.7 integration. 12.2.1 includes controller support/mapping changes. Expressive Chords belongs to the Max/content side of the inventory, not a newly inferred native synthesis engine. Sources: [release notes](https://www.ableton.com/en/release-notes/live-12/), [12.2 announcement](https://www.ableton.com/en/blog/live-12-2/).

Do not backport modern documentation into the target unnoticed. Stem separation and Splice belong to the later 12.3 release; Link Audio and Learn View belong to 12.4. Updated Erosion, Delay, Chorus-Ensemble, and renamed/revised Auto Pan behavior also require explicit version review. They are not automatic 12.2.1 requirements. Source: [versioned release headings](https://www.ableton.com/en/release-notes/live-12/).

## What “Suite” covers

Keep four inventories instead of treating every browser result as an engine feature:

1. **Core application behavior:** editing, playback, recording, routing, automation, file management, controllers, and UI.
2. **Native device capabilities:** instruments, audio/MIDI effects, and rack infrastructure.
3. **Extensible devices:** Max-based instruments, effects, modulators, MIDI tools, and host APIs. Halftrack will provide its own system.
4. **Content:** samples, multisamples, presets, grooves, lesson/demo content, and optional Packs.

The Core Library combines several of these types. Packs have their own version and license dependencies and can add devices as well as sound content. Marketing device totals therefore do not define a reproducible compatibility manifest. Sources: [Core Library](https://help.ableton.com/hc/en-us/articles/360000776490-The-Core-Library), [Pack FAQ](https://help.ableton.com/hc/en-us/articles/209072029-Ableton-Live-Pack-FAQ), [Packs release history](https://www.ableton.com/en/release-notes/packs-release-notes/).

The device tables in [feature-parity.md](../feature-parity.md) use reference device names only to identify comparison targets. Halftrack implementations, names, visual assets, and distributable content must be its own. No imported vendor presets or sample libraries are required to implement a capability test.

## Plug-ins and cross-platform behavior

The baseline plug-in families are VST2, VST3, AUv2, and AUv3. AU is macOS-only. Windows supports VST2/VST3; unsupported Live formats such as AAX and RTAS do not become parity requirements merely because “full plug-in support” was requested. Source: [supported formats](https://help.ableton.com/hc/en-us/articles/5937501570460-Supported-Plug-in-Formats).

Native Apple Silicon Live requires native Apple Silicon VST2/VST3 builds. Some Intel Audio Units can run using Apple's compatibility service. That is a platform behavior to investigate, not proof that arbitrary architecture bridging is free or already supported. Source: [Apple Silicon plug-ins](https://help.ableton.com/hc/en-us/articles/4410323149074-Plug-ins-on-Mac-in-Live-11-1-and-later).

Define plug-in support as a tested contract: discovery, classification, loading, bus layout, MIDI/MPE delivery, parameters, automation, sidechains, latency reporting, editor windows, state persistence, offline rendering, removal, and failure handling. Compare the same plug-in build and format on both operating systems where a vendor supplies them. An AU project cannot be assumed portable to Windows simply because a similarly named VST exists. Sources: [plug-in manual](https://www.ableton.com/en/live-manual/12/using-plug-ins/), [macOS plug-ins](https://help.ableton.com/hc/en-us/articles/209068929-Using-AU-and-VST-plug-ins-on-macOS), [project transfer](https://help.ableton.com/hc/en-us/articles/209071909-Transferring-Projects-to-another-computer).

For the preview, publish exactly which formats and paths work. VST2 licensing/access and legacy binary support remain separately unverified; they must not be hidden behind a generic “all plugins” claim. This is an implementation dependency question, not an assertion that a chosen SDK grants those rights.

## Audio system and performance

Live's documented setup uses CoreAudio on macOS and recommends ASIO on Windows. The macOS UI can choose separate input and output devices. Windows ASIO generally uses one device for both; some settings are controlled through the interface's own panel. Sources: [audio interface setup](https://help.ableton.com/hc/en-us/articles/211476789-Setting-up-an-Audio-Interface), [ASIO](https://help.ableton.com/hc/en-us/articles/360003149240-Using-an-ASIO-audio-driver-on-Windows), [latency](https://help.ableton.com/hc/en-us/articles/209072289-How-to-reduce-latency).

Consequently, shared source code should preserve musical behavior while allowing honest differences in available device controls. Shared C++ or another portable engine is a proposed Halftrack architecture choice; this research does not establish Live's entire internal language or implementation.

Separate these measurements:

- Engine processing time versus callback deadline.
- Hardware/input/output latency versus latency introduced by devices.
- Recorded audio alignment versus monitor-path delay.
- Offline-render throughput versus real-time reliability.
- Peak CPU pressure versus average load.
- Dropouts, memory growth, and disk starvation during a sustained session.

These are proposed engineering measurements, not measured Halftrack results. “Optimized” requires a reproducible fixture, sample rate, buffer size, architecture, interface class, and test duration. A successful build alone verifies none of them.

Ableton's Audio Fact Sheet distinguishes neutral operations from sound-changing ones and describes cancellation testing. Use that distinction when designing reference fixtures: identity playback can demand strict numerical checks; time stretching and nonlinear devices need their own evaluated tolerances. Source: [Audio Fact Sheet](https://www.ableton.com/en/live-manual/12/audio-fact-sheet/).

## Project format and extensibility

Ableton Sets reference external media, plug-ins, and Max devices, and save-version compatibility has limits. Halftrack's own format should explicitly version its schema, track media references, retain unavailable-device state, and save atomically. Those are design recommendations; `.als` compatibility is excluded. Sources for the reference behavior: [project transfer](https://help.ableton.com/hc/en-us/articles/209071909-Transferring-Projects-to-another-computer), [backward compatibility](https://help.ableton.com/hc/en-us/articles/360000841004-Backward-Compatibility).

Ableton's developer page describes a restricted Live Set Export library, currently described there as iOS-only, and an open-source cross-platform Link library. Neither is a general promise of arbitrary Set import or access to Live's source. [Official developer libraries](https://ableton.github.io/).

Max for Live includes editable instrument/effect/MIDI-tool patches and APIs for interacting with the host. A different Max installation can be selected, so the app version alone does not fully specify that runtime. Halftrack's equivalent needs a separate specification for its patch model, DSP/event timing, state, parameter exposure, host API, distribution format, and compatibility policy. Source: [Max for Live manual](https://www.ableton.com/en/live-manual/12/max-for-live/).

## Firsthand UI observation log

The following observations were supplied by the main application's CUA audit on 2026-10-13. They document visible UI/help only. They are **not** sound, recording, timing, plug-in compatibility, or performance tests. No reference settings were changed for these observations.

| Area | Observed in installed 12.2.1 | Verification limit |
| --- | --- | --- |
| Arrangement workspace | Browser on the left, timeline in the center, track headers/mixer on the right, selected-track devices below, transport above | Layout observation; interaction parity still unverified. |
| Audio preferences | CoreAudio and distinct input/output selection controls; reference engine inactive because no devices were selected | No device names or inventory retained; no audio behavior tested. |
| Plug-in preferences | Separate AUv2/AUv3 options; VST2/VST3 system and custom sources; rescan controls | Presence of controls does not prove any plug-in loads. |
| Rescan and windows help | Option-modified full rescan versus changed-only rescan; multiple, automatic-hide, and automatic-open plug-in-window preferences | Help text observed, behaviors not empirically exercised. |
| Transport help | Space play/stop; Shift+Space resume; double Stop returns to 1.1.1; F9 Arrangement recording | Record-triggered playback depends on the Start Transport With Record preference. |

## Research-to-test procedure

After preview approval, each baseline item should gain a small reproducible reference case:

1. Create a new disposable reference session from generated or expressly permitted media.
2. Record the app version, starting state, exact gesture or command, and observable result.
3. Record boundary cases, undo/redo, save/reopen, automation, and interaction with routing.
4. Implement an independent Halftrack capability and add a meaningful regression test.
5. Compare on macOS; repeat shared behavior and rendering fixtures on Windows.
6. Save numeric results, relevant logs, and an original written observation. Keep personal paths, device identities, copyrighted source content, and screenshots out of public evidence.
7. Promote an item only when evidence exists. A visible control, mock UI, or class name is not implementation evidence.

This is a proposed protocol. No exhaustive firsthand sweep, cross-platform certification, device sound comparison, or latency benchmark was completed by this research pass.

## Remaining scope questions to resolve after preview review

- Which reproducible behavior defines each device's “close enough” sound, and who accepts it?
- Which plug-in format/architecture matrix is actually supported by the selected host SDK and licensing?
- Which controller models and Push generations can be tested on real hardware?
- Which versioned Packs are capability references versus content outside the implementation target?
- What minimum OS/CPU versions will the shared codebase support?
- What initial public API will Halftrack's own patch environment promise?
- Which accessibility/navigation differences are acceptable where platform conventions differ?

These do not block building the agreed preview. They block claims of complete parity. See [the initial parity ledger](../feature-parity.md) for the implementation and verification backlog.
