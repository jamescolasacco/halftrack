# Halftrack feature-parity ledger

Capability planning inventory, revised 2026-10-13. These are proposed behavior tests, not completed parity claims. See [specification policy](research/workflows.md).

**This is a planned capability inventory, not a claim that every reference DAW function has been examined.** Entries began as **NOT IMPLEMENTED / UNVERIFIED**. The preview and Session rows below now record narrowly scoped evidence; unverified rows retain their initial status. No completion is inferred from a screenshot, a control, a class name, a build, or another contributor's in-progress code.

## Scope and progression

The first audio/MIDI preview has been handed off. The latest owner instructions authorize renewed research, local binary analysis and implementation; the former preview approval gate is superseded. Windows shares the engine/model/UI and requires its own verification. Halftrack retains its own project format and original identity. Existing proprietary project/patch compatibility, a programmable patch environment, branded hardware integration and cloud features remain outside the current implementation.

The target is the closest measurable behavior, including exact-output comparisons for well-defined cases. No whole-product zero-difference claim is justified. The rows are a starting backlog, with broad groups still requiring parameter, gesture, shortcut, error-state and interaction cases. Consult [current progress](progress.md) for the latest verified results; older preview rows below retain their original observation scope.

### Status rules

- **NOT IMPLEMENTED / UNVERIFIED**: the initial state; no verified Halftrack implementation evidence is recorded.
- **PARTIAL**: identify exactly which subcases work and link evidence; missing cases stay explicit.
- **IMPLEMENTED / UNVERIFIED**: code exists but the stated acceptance checks have not passed.
- **VERIFIED**: record the commit, fixture, environment, command or manual procedure, measured result, and platform. Do not infer the other platform.
- Excluded/version-review rows remain outside acceptance unless a later user decision changes the scope.

A verification record should name the row ID, reference version, Halftrack commit, platform/architecture, generated or permitted fixture, inputs/actions, expected result, actual result, tolerance, and known gaps. Keep personal device names, plug-in paths, screenshots, private presets, and copyrighted reference media out of public evidence.

There are **170 behavior/scope rows** and **105 device/content rows** in this initial ledger. These are backlog entries, not a completed-feature count.

## Current Session subset

Engine `8606b05`, persistence/test integration `0f9e217`, and UI `16cb96b` pass the local Apple Silicon macOS launcher, UI-handler and session subsets. See [measured cases and limits](preview-verification.md#session-extension-local-verification). This is not complete Session parity or a current full-suite/CI pass. The new offline-latency edge correction passed its focused suite in 17.40 seconds; the expanded empty-slot takeover suite also passes in 3.75 seconds, while complete integration remains pending. Native Session manual checks UI-21 through UI-33 and physical hardware checks remain NOT RUN. Earlier platform passes apply only to their named source revisions.

## Original preview acceptance record

The original audio-preview subset passed locally in **12.66 seconds** at `a6f7e2e`. Browser repair and native save/reopen/export were subsequently observed, and the earlier preview later passed three-platform CI at `1294f5b`; details are in the [verification record](preview-verification.md). Those results do not certify the continued Session candidate. **PARTIAL** never implies the whole row or another platform has passed.

| ID | Capability / target | Acceptance evidence and remaining gap | Status |
| --- | --- | --- | --- |
| PRE-001 | Launch and honest status | Native Mac launch was observed after the browser stopped scanning Music at startup; chosen-directory scanning runs asynchronously. Current Session platform/CI results remain pending. | PARTIAL — Mac launch observed |
| PRE-002 | Device selection | Earlier GUI reported CoreAudio built-in output, 96 kHz / 512 samples, no input. No-device guards and virtual-device inputs passed. Hardware switching, permissions, hot-plug, and latency remain pending. | PARTIAL — observed output configuration |
| PRE-003 | Record a take | Generated input passed the real recording graph; stop created a WAV clip with expected rate, duration bounds, and signal. Export and save passed. Physical/acoustic capture and alignment remain pending. | PARTIAL — Mac synthetic capture |
| PRE-004 | Import and arrange audio | Earlier GUI imported four generated stems, showed waveforms, fitted the timeline, and played/stopped with advancing transport. API clip gesture undoes once. GUI clip placement/movement and seek checks remain pending. | PARTIAL — Mac import/playback observed |
| PRE-005 | Basic mix | Two-track summing, gain, pan, mute, solo, and grouped gain/pan undo passed API checks. GUI track/master meters were active. GUI mixer gestures and Windows remain pending. | PARTIAL — automated mix; meters observed |
| PRE-006 | Plug-in audition | Controlled VST3 discovery, instantiation, parameter change, processing, state round trip, bypass, and removal passed. Editor interactions, AU, arbitrary plug-ins, and Windows remain pending. | PARTIAL — controlled Mac VST3 |
| PRE-007 | Export | 48 kHz WAV PCM and duration passed; a two-second extension produces 144,000 frames from a one-second dry source. Worker cancellation dispatches messages and preserves existing output bytes. Native Mac export was decoded independently; the rate/depth matrix, actual effect tails, and current candidate platform results remain pending. | PARTIAL — Mac API export/cancel |
| PRE-008 | Save and reopen | Portable project/media, track name, tempo, loop, rendered audio, and VST3 state passed; invalid load retained the edit. Clean/dirty initialization regressions passed. Native Mac audio-demo save/reopen was observed; complete state coverage remains pending. | PARTIAL — tested API cases |
| PRE-009 | Basic stability | Original preview workflows and scoped continued Session tests passed; the browser stall was repaired. Repeated device changes, arbitrary plug-in editor lifetime, and long-session checks remain pending. Full integration after the latest empty-slot takeover change remains pending. | PARTIAL — bounded automated/GUI evidence |
| PRE-010 | Preview handoff | Runnable preview delivered; latest owner instructions authorize further research and implementation. | HANDOFF COMPLETE — former gate superseded |

## Audio backend, device lifecycle, and recording alignment


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| AUD-001 | CoreAudio backend | Exercise output-only, input-only where useful, duplex, invalid device, and restart paths. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-002 | Windows ASIO backend | Build on Windows and repeat duplex capture/playback with an actual ASIO interface; do not equate compilation with driver verification. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-003 | Windows system-audio fallback | Specify and test available fallback modes and their limitations separately from ASIO; verify the selected implementation rather than imply reference DAW equivalence. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-004 | Separate and aggregate devices | Test supported macOS combinations, clock drift, changing device IDs, and unavailability on reopen; document the Windows counterpart. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-005 | Channel enablement | Enable mono/stereo inputs and outputs, rename aliases, and verify physical-to-track routing with distinct channel impulses. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-006 | Sample rate and buffer controls | Test negotiated rates and buffers, unsupported values, device-owned configuration panels, and changes during transport. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-007 | Hot unplug and recovery | Disconnect/reconnect a test interface; verify bounded silence, useful error state, preserved session, and intentional recovery. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-008 | Input permission failure | Deny microphone access and retry; explain the condition without hanging or manufacturing an input meter. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-009 | Driver latency and compensation | Measure loopback latency and recorded placement; distinguish reported latency, measured offset, manual compensation, and monitor delay. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-010 | Monitoring modes | Test track monitoring with arm/disarm and playback overlap; prove when input, recorded clip, or silence reaches each destination. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-011 | Audio engine off/on | Toggle processing and change backend state without stuck notes, stale meters, or unexpected recording loss. | NOT IMPLEMENTED / UNVERIFIED |
| AUD-012 | Device settings persistence | Reopen with the original device present and absent; preserve meaningful choices without persisting personal identifiers in public fixtures. | NOT IMPLEMENTED / UNVERIFIED |

## Transport, time, recording, and takes


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| REC-001 | Musical and sample clocks | Define conversion between beats and sample frames; test long sessions and exact loop boundaries. | NOT IMPLEMENTED / UNVERIFIED |
| REC-002 | Play, stop, continue, locate | Test start from selection, stop/resume, rewind, scrub, and jumps with sounding notes and delayed effects. | NOT IMPLEMENTED / UNVERIFIED |
| REC-003 | Tempo and signatures | Test manual tempo, tap tempo, nudge, signature changes, tempo ramps, and transitions across clips. | NOT IMPLEMENTED / UNVERIFIED |
| REC-004 | Loop and punch | Record through repeated loop boundaries and punch regions; inspect placement and partial-buffer boundaries. | NOT IMPLEMENTED / UNVERIFIED |
| REC-005 | Metronome and count-in | Verify beats, accents, routing, count-in, odd signatures, and start-on-grid behavior against recorded impulses. | NOT IMPLEMENTED / UNVERIFIED |
| REC-006 | Simultaneous multitrack capture | Record distinct signals to several tracks; verify alignment, channel isolation, file closure, and stop behavior. | NOT IMPLEMENTED / UNVERIFIED |
| REC-007 | Recording formats and disk failure | Verify supported bit depths and sample rates; simulate storage failure without corrupting previously committed takes. | NOT IMPLEMENTED / UNVERIFIED |
| REC-008 | MIDI recording and overdub | Record note, controller, sustain, and expression streams; verify overdub policy and quantization choices. | NOT IMPLEMENTED / UNVERIFIED |
| REC-009 | MIDI capture and step input | Specify capture-history boundaries and step-entry operations; compare reference workflows using generated MIDI. | NOT IMPLEMENTED / UNVERIFIED |
| REC-010 | Take lanes and comping | Record several takes, audition lanes, construct a comp, edit transitions, and undo/reopen the result. | NOT IMPLEMENTED / UNVERIFIED |
| REC-011 | Linked-track edits | Edit grouped microphone takes together; check phase alignment and per-track exceptions. | NOT IMPLEMENTED / UNVERIFIED |
| REC-012 | External synchronization | Exercise MIDI clock in/out, transport messages, clock loss/rejoin, sync offset, Link tempo/phase, and tempo-following behavior. | NOT IMPLEMENTED / UNVERIFIED |

## Arrangement and clip editing


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| ARR-001 | Timeline navigation | Test horizontal/vertical zoom, fit, follow, selection, scroll, and navigation while playback continues. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-002 | Tracks and groups | Create, rename, color, reorder, duplicate, delete, nest, and ungroup tracks with stable references. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-003 | Move, trim, loop, slip | Exercise clip edges, source offsets, negative/late starts where supported, and repetition without destructive media changes. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-004 | Selection and grid | Test single/multiple clips, time ranges, fixed/adaptive/triplet grids, temporary snap override, and keyboard equivalents. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-005 | Cut, copy, paste, duplicate | Verify cross-track/time moves, copy modifiers, clipboard scope, undo, and automation ownership. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-006 | Split and consolidate | Test edits at start/end/loop boundaries; compare reconstructed audio and handle tails or selection gaps explicitly. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-007 | Time insertion/removal | Insert, delete, duplicate, and paste time across tracks with clips, tempo, markers, and automation. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-008 | Fades and crossfades | Edit overlapping clips and edge fades; test click avoidance, curve state, persistence, and render agreement. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-009 | Locators and loop regions | Create/name/jump/delete locators and edit loop endpoints; test meter changes and playback jumps. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-010 | Freeze and bounce | Specify 12.2.1 freeze/bounce behavior, retained source state, tails, sidechains, output placement, and undo. | NOT IMPLEMENTED / UNVERIFIED |
| ARR-011 | Session/Arrangement ownership | Native generated output verifies boundary takeover, stop retaining Session silence, delayed UI publication, graph rebuild and explicit Return; handler tests prove view switches do not change ownership. Stale-generation and same-pitch MIDI handoff cases pass. Native manual delivery and hardware timing remain unverified. | PARTIAL — Mac Session subset |
| ARR-012 | Clip properties | Persist activation, name/color, source bounds, loop bounds, gain, tuning, RAM/interpolation options, and batch edits. | NOT IMPLEMENTED / UNVERIFIED |

## Session performance and launching


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| SES-001 | Clip grid and scenes | Create/import/clear slots; add/rename/delete scenes; undo, stable IDs, exclusive playback and empty-slot stop/silent arrangement takeover pass engine/handler cases. Slot MIDI editing and occupied-destination rejection pass. Clip/scene move/copy and complete native interaction remain missing or unverified. | PARTIAL — Mac Session subset |
| SES-002 | Quantized launch and stop | None/Beat/Bar launch/stop, tempo-aware scheduling, loop wrap and stopped transport pass generated 48 kHz/128-frame cases; onset tolerance is 256 frames and tested scene-channel alignment is within one sample. Scene commands are serialized in the fixture; concurrent device callbacks can split native per-track enqueue across blocks. Exact request-edge timing and hardware jitter remain unverified. | PARTIAL — Mac generated timing |
| SES-003 | Launch modes and legato | Specify gate, trigger, toggle, repeat, velocity response, and phase-preserving transitions. | NOT IMPLEMENTED / UNVERIFIED |
| SES-004 | Follow actions | Exercise clip and scene actions, probabilities, repetition, timing, and global disable with seeded test fixtures. | NOT IMPLEMENTED / UNVERIFIED |
| SES-005 | Scene tempo/signature | Launch scenes that change tempo/meter; verify transport continuity and existing clip phase. | NOT IMPLEMENTED / UNVERIFIED |
| SES-006 | Session recording | Record audio/MIDI into slots and overdub; test arm routing, quantization, and loop length. | NOT IMPLEMENTED / UNVERIFIED |
| SES-007 | Performance capture | Record a launch performance into the Arrangement and replay equivalent timing. | NOT IMPLEMENTED / UNVERIFIED |
| SES-008 | Controller feedback | Clip queued/playing/stopped and track ownership display actual engine state; Enter/Shift+Enter, scene/slot selection and stop/return handlers pass. External controller feedback, recording state and native OS key delivery remain unverified. | PARTIAL — native UI handler subset |

## Audio clip processing and timing


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| WRP-001 | Unwarped audio | Verify sample-rate conversion policy, start offset, pitch, reverse, looping, gain, and end-of-file handling. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-002 | Transient and tempo analysis | Use original percussive and sustained fixtures; record detection accuracy and provide editable results. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-003 | Warp-marker editing | Add/move/remove/reset markers; test segment continuity, undo, and save/reopen. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-004 | Rhythmic stretching | Compare timing and transient preservation for a Beats-like mode over several tempo ratios. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-005 | Tonal and textured stretching | Compare sustained-tone and granular-texture modes; document artifacts and deterministic settings. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-006 | Repitch behavior | Verify coupled pitch/speed changes and tempo automation using known-frequency fixtures. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-007 | Complex stretching | Assess polyphonic and formant-aware processing on permitted material; record subjective and numeric tolerances. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-008 | Leader/follower tempo | Test clips that define versus follow tempo, conflicts between leaders, and automation ownership. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-009 | Groove pool | Extract, apply, edit, commit, and remove timing/velocity grooves without losing original note/marker data. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-010 | Slicing and conversion | Test transient slicing and separate melody, harmony, and drum transcription fixtures; report errors rather than imply perfect inference. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-011 | Shared media edits | Verify that duplicate clips and alternate sessions do not unexpectedly mutate each other's source files. | NOT IMPLEMENTED / UNVERIFIED |

## MIDI editing, tools, MPE, and tuning


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| MID-001 | Piano-roll editing | Production handlers create/select/move/resize/delete and undo notes in arrangement and Session clips. Selected empty Session slots do not open another clip. Split, duplicate, deactivate and complete native input delivery remain unverified or missing. | PARTIAL — Mac editor handlers |
| MID-002 | Pitch/time operations | Verify transpose, scale-aware movement, reverse/invert, stretch, legato, fixed lengths, and quantize without stuck notes. | NOT IMPLEMENTED / UNVERIFIED |
| MID-003 | Velocity, release, probability | Persist expressive values and ranges; use deterministic seeds to test chance and repeated-note behavior. | NOT IMPLEMENTED / UNVERIFIED |
| MID-004 | Multi-clip editing | Display/edit several MIDI clips; verify focus, selection boundaries, and notes at overlapping loop positions. | NOT IMPLEMENTED / UNVERIFIED |
| MID-005 | Controller/program events | Edit and replay CCs, pitch bend, sustain, channel pressure, banks, and program changes in order. | NOT IMPLEMENTED / UNVERIFIED |
| MID-006 | Note lifecycle | Generated Session MIDI loops stop without stuck voices; held arrangement notes release on takeover while same-pitch and different-pitch new notes survive. Instrument replacement, panic, device loss and general overlapping-note semantics remain unverified. | PARTIAL — Mac generated handoff |
| MID-007 | Transformation tools | Inventory Arpeggiate, Chop, Connect, Glissando, LFO, Ornament, Quantize, Recombine, Span, Strum, Time Warp, and Velocity Shaper; give each a reference fixture. | NOT IMPLEMENTED / UNVERIFIED |
| MID-008 | Generative tools | Inventory Rhythm, Seed, Shape, Stacks, and Euclidean; verify ranges, deterministic regeneration, and undo. | NOT IMPLEMENTED / UNVERIFIED |
| MID-009 | MPE recording/editing | Record and edit per-note pitch, pressure, slide, and release data; preserve association after trimming and copying. | NOT IMPLEMENTED / UNVERIFIED |
| MID-010 | MPE routing | Verify member-channel allocation, zones, pitch range, note stealing, and plug-in delivery. | NOT IMPLEMENTED / UNVERIFIED |
| MID-011 | Scales and alternate tunings | Test scale display, note quantization, tuning import/mapping, bypass, and compatible instrument/plug-in behavior. | NOT IMPLEMENTED / UNVERIFIED |
| MID-012 | MIDI file import/export | Round-trip supported event types, tempo and meter maps, track/channel assignments, and fractional timing. | NOT IMPLEMENTED / UNVERIFIED |
| MID-013 | MIDI ports and mappings | Exercise input/output filtering, virtual ports, remote mappings, learn, pickup/scaling, relative controls, and persistence. | NOT IMPLEMENTED / UNVERIFIED |

## Mixer, routing, racks, and automation


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| MIX-001 | Gain, pan, meters | Measure channel gain law, pan/balance modes, peak/RMS behavior, overload indication, and display ballistics. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-002 | Mute/solo/arm rules | Specify exclusive and additive behavior across groups, returns, cue, and armed tracks. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-003 | Returns and sends | Verify pre/post sends, nested groups, send automation, tail handling, and feedback restrictions. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-004 | Internal routing taps | Route pre/post processing and post-mixer signals; verify multichannel sources and feedback graph policy. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-005 | Cue and crossfade | Test independent cue output, preview level, crossfader assignment/curves, and master/cue separation. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-006 | Resampling and external inserts | Capture internal buses; loop external hardware with measured compensation and explicit render constraints. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-007 | Delay compensation | Controlled offline VST3 serial/parallel, bypass and removal cases pass. Two renderer defects are corrected; ten edge exports at 48000/64 frames and 127 through 635 samples of graph latency match their dry reference exactly with finite values and exact length. Live compensation, returns, sidechains, dynamic latency and monitoring exceptions remain unverified. | PARTIAL — Mac controlled offline graph |
| MIX-008 | Track delay | Test positive/negative offsets and compensation while maintaining project-time alignment. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-009 | Serial and parallel devices | Add/reorder/bypass/remove devices and rack chains while maintaining valid graph/state. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-010 | Drum racks | Test pads, choke behavior, chain routing, independent outputs, and extraction without stuck voices. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-011 | Key/velocity/selector zones | Test overlaps, boundaries, fades, distribution, and modulation of chain selection. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-012 | Macros and variations | Map ranges, invert mappings, store/recall variations, randomize, automate, and reopen. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-013 | Automation editing | Create, type, draw, curve, scale, simplify, copy, and delete envelopes with meaningful undo units. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-014 | Automation capture/override | Record from UI/controller, override playback, re-enable it, and resolve Session versus Arrangement ownership. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-015 | Clip modulation | Test linked/unlinked envelope loops, parameter scaling, automation interaction, and device replacement. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-016 | Tempo automation | Render tempo steps and curves; verify audio/MIDI scheduling and time-to-sample conversion. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-017 | Parameter identity | Maintain stable target IDs across reorder, duplicate, undo, reopen, unavailable devices, and both operating systems. | NOT IMPLEMENTED / UNVERIFIED |

## Plug-in host contract


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| PLG-001 | VST3 | Verify actual effect and instrument instances, factory metadata, state, automation, editor lifecycle, and render behavior on macOS and Windows. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-002 | VST2 | Resolve SDK/distribution feasibility before claiming support; test actual permitted legacy plug-ins on each supported architecture. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-003 | AUv2 | Test macOS discovery, effects/instruments, channel layouts, editor, parameters, state, and offline rendering. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-004 | AUv3 | Verify extension discovery and lifecycle on supported macOS; do not treat AUv2 success as AUv3 evidence. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-005 | Architecture policy | Document native CPU targets and any explicit bridge; verify unsupported binaries produce an actionable error. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-006 | Scanning and cache | Test standard/custom paths, rescan, duplicate IDs, removed files, changed versions, and interrupted scan. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-007 | Scan isolation | Use a deliberately failing test plug-in to prove the scanner cannot terminate the main session. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-008 | Device classification | Distinguish instrument/effect/MIDI capability, creator, format, and bus support without relying only on file extension. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-009 | Audio bus layouts | Test mono/stereo/multichannel, auxiliary outputs, sidechains, unsupported layouts, and layout changes. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-010 | Events and timing | Verify MIDI/MPE timestamps, transport/tempo information, looping, offline mode, and block-size changes. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-011 | Parameters and automation | Test normalized values, units/text, gestures, host learn, parameter count changes, and sample/block timing. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-012 | State and missing plug-ins | Reopen plug-in state and preserve unresolved state without silently discarding it. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-013 | Editor windows | Test focus, resize, scaling, keyboard shortcuts, multi-display movement, close/reopen, and app shutdown. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-014 | Latency and tails | Verify reported latency, changed latency, silence flags, tail rendering, bypass, and compensation. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-015 | Preset and bank handling | Specify supported formats and round-trip state using redistributable fixtures, not private presets. | NOT IMPLEMENTED / UNVERIFIED |
| PLG-016 | Runtime failure handling | Define crash/hang containment, restore options, and unavailable-device placeholders; test using a fault-injection fixture. | NOT IMPLEMENTED / UNVERIFIED |

## Projects, browser, assets, export, and recovery


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| PRJ-001 | Halftrack project schema | Specify IDs, versioning, migrations, unknown fields, deterministic storage, and atomic save behavior. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-002 | Save/reopen and Save As | Generated Session cases preserve scenes/slots, MIDI notes, quantization, names and stable IDs; invalid launcher shape preserves the current edit and undo/redo. General routing/automation and full native-dialog coverage remain unverified. | PARTIAL — Mac project subset |
| PRJ-003 | Undo/redo history | Test transactions spanning several objects, invalidated references, saved-state markers, and history limits. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-004 | Autosave and recovery | Interrupt during save/record/render; recover the last consistent state without overwriting the original project. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-005 | Media collection and relocation | Generated slot audio is collected and reopens after moving project plus Assets and removing the original. Missing media rejects replacement. Cross-system, case and arbitrary media-library cases remain unverified. | PARTIAL — Mac relocation subset |
| PRJ-006 | Asset formats | Enumerate actual decoder/encoder support; test WAV/AIFF/FLAC and supported compressed formats with invalid/truncated inputs. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-007 | Browser indexing | Scan large permitted libraries asynchronously; handle duplicate names, moved files, cancellation, and unavailable paths. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-008 | Search, tags, collections | Specify search/filter semantics, user tags, favorites, and 12.2-era browser interactions. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-009 | Preview and hot swap | Audition files without altering the mix; swap compatible sources/devices while retaining intended mappings. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-010 | Similarity search | Evaluate a Halftrack-owned similarity model against a known library; report relevance and compute cost. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-011 | Templates and presets | Save/open reusable track, device, rack, clip, and session defaults in documented Halftrack formats. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-012 | Stereo mix render | Verify range, tail, rate, channels, gain, normalization, bit depth, dither, and cancellation. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-013 | Stems and selected tracks | Verify render routing and sends for each export mode; prevent filename collision and accidental overwrite. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-014 | Realtime/offline agreement | Compare permitted plug-ins and native devices; document nondeterministic or realtime-only cases. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-015 | Video workflow | Define supported import/playback/export formats, frame-rate/timecode behavior, synchronization, and platform differences. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-016 | Packs and content management | Specify an original content package format, dependency versions, uninstall behavior, and user-generated content handling. | NOT IMPLEMENTED / UNVERIFIED |

## Halftrack's own extensible device environment


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| EXT-001 | Patch document and editor | Define nodes, typed ports, connections, subpatches, edit transactions, and documented file versions. | OUT OF SCOPE |
| EXT-002 | Audio DSP graph | Build a small instrument/effect in the environment; verify realtime execution, feedback policy, allocation boundaries, and sample-rate changes. | OUT OF SCOPE |
| EXT-003 | MIDI/event graph | Build a note transform and generator; verify scheduling, ordering, loop behavior, and panic. | OUT OF SCOPE |
| EXT-004 | Parameter/UI exposure | Expose controls with units/ranges/defaults; map, automate, modulate, undo, and save them. | OUT OF SCOPE |
| EXT-005 | Host object API | Specify stable access to tracks, clips, notes, devices, routing, transport, and observers with bounded updates. | OUT OF SCOPE |
| EXT-006 | Modulation semantics | Define absolute versus relative modulation, multiple sources, mapping conflicts, and automation interaction. | OUT OF SCOPE |
| EXT-007 | Patch state and dependencies | Package resources and subpatches; detect missing versions and preserve unresolved state. | OUT OF SCOPE |
| EXT-008 | Safe editing while playing | Test graph replacement and code errors without uncontrolled output or audio-thread stalls. | OUT OF SCOPE |
| EXT-009 | MIDI tools integration | Host original transforms/generators in the note editor with preview, apply, seed control, and undo. | OUT OF SCOPE |
| EXT-010 | Extension API compatibility | Publish a versioned API contract and migration policy before third-party developers depend on internals. | OUT OF SCOPE |

## Controllers, accessibility, and product shell


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| UX-001 | Keyboard workflow | Session Enter/Shift+Enter, arrows, delete/undo and menu/view commands pass production handler cases. A complete shortcut map, OS dispatch, text-entry/focus and platform modifiers require further verification. | PARTIAL — Mac handler subset |
| UX-002 | Resizable workspace | Actual original Session and piano snapshots generated at 1100x720 and 1440x900; Session 1100x720 and piano 1440x900 inspected as readable without clipping. Multi-window, high-DPI/native interaction and other-platform checks remain unverified. | PARTIAL — two Mac rendered layouts |
| UX-003 | Screen reader and focus | Expose names, roles, states, values, and focus order; verify real VoiceOver and Windows screen-reader workflows. | NOT IMPLEMENTED / UNVERIFIED |
| UX-004 | Numeric precision controls | Test typed values, fine/coarse drag, reset, units, modifiers, and accessible equivalents. | NOT IMPLEMENTED / UNVERIFIED |
| UX-005 | Themes and contrast | Provide Halftrack-owned visual assets and readable contrast; verify selection, clipping, disabled states, and scaling. | NOT IMPLEMENTED / UNVERIFIED |
| UX-006 | Generic controller integration | Test MIDI learn, relative encoders, pickup, LED feedback, reconnect, and shared mappings. | NOT IMPLEMENTED / UNVERIFIED |
| UX-007 | Branded hardware integration | Separate each generation and controller mode; verify documented communication and real hardware before making a support claim. | OUT OF SCOPE |
| UX-008 | Other control surfaces | Maintain a public supported-model matrix using approved test fixtures; do not copy bundled proprietary scripts by assumption. | NOT IMPLEMENTED / UNVERIFIED |
| UX-009 | Help and discoverability | Provide original contextual help and explanations for implemented controls, especially disabled or unavailable actions. | NOT IMPLEMENTED / UNVERIFIED |
| UX-010 | Settings and migration | Persist preference versions safely and test defaults, reset, corrupt settings, and cross-platform migration. | NOT IMPLEMENTED / UNVERIFIED |
| UX-011 | Packaging and updates | Test macOS bundle and Windows package, clean install/uninstall, version reporting, and future signing/update policy. | NOT IMPLEMENTED / UNVERIFIED |

## Reliability, performance, and shared-platform verification


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| QA-001 | Realtime thread discipline | Instrument callbacks for allocation, locking, I/O, unbounded work, and missed deadlines under representative load. | NOT IMPLEMENTED / UNVERIFIED |
| QA-002 | Long-running transport | Run an extended scripted session with edits/recording; measure dropouts, drift, memory growth, and file-handle leaks. | NOT IMPLEMENTED / UNVERIFIED |
| QA-003 | Deterministic engine fixtures | Render generated impulses, sine waves, noise, and MIDI patterns; compare declared numerical tolerances across platforms. | NOT IMPLEMENTED / UNVERIFIED |
| QA-004 | Independent decoder validation | Read exported files independently; verify metadata, sample count, channels, peak level, and absence of unintended silence. | NOT IMPLEMENTED / UNVERIFIED |
| QA-005 | Audio-thread/UI separation | Stress browser search, repaint, window resize, and file dialogs while recording without losing callback deadlines. | NOT IMPLEMENTED / UNVERIFIED |
| QA-006 | Large sessions | Scale tracks, clips, automation, and media deliberately; record load/save/render times and peak memory. | NOT IMPLEMENTED / UNVERIFIED |
| QA-007 | MIDI jitter and stuck-note tests | Measure scheduled versus delivered events through loop/seek/tempo transitions, with clear device versus host limitations. | NOT IMPLEMENTED / UNVERIFIED |
| QA-008 | Graph edits during playback | Repeatedly change routing and devices; check races, object lifetime, tails, and glitches. | NOT IMPLEMENTED / UNVERIFIED |
| QA-009 | Malformed input and recovery | Fuzz project/media/plug-in metadata parsing with bounded resources; verify no silent project data loss. | NOT IMPLEMENTED / UNVERIFIED |
| QA-010 | Mac/Windows CI | Build and run the same engine/model tests for supported architectures on every relevant change. | NOT IMPLEMENTED / UNVERIFIED |
| QA-011 | Native device and driver checks | Complement CI with real device, editor-window, permission, and installer tests; retain explicit untested cells. | NOT IMPLEMENTED / UNVERIFIED |
| QA-012 | Dependency and license manifest | Record pinned dependencies, source licenses, notices, reproducible acquisition, and copyleft distribution requirements. | NOT IMPLEMENTED / UNVERIFIED |
| QA-013 | Regression evidence | Tie each verified capability to a commit, fixture, command, environment, expected result, and actual result. | NOT IMPLEMENTED / UNVERIFIED |
| QA-014 | No false controls | Audit visible actions: each must work, be clearly disabled, or explain an unavailable capability; prevent preview mockups being mistaken for features. | NOT IMPLEMENTED / UNVERIFIED |

## Explicit exclusions and later-version watchlist


Stage: **Excluded / version review**. All checks below are proposed, not performed.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| OUT-001 | ALS import/export or round-trip | Out of agreed scope. Do not advertise reference DAW Set compatibility or silently add a compatibility commitment. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-002 | Existing AMXD/Max runtime compatibility | Out of agreed scope. The target is an independent extensible environment with its own contract. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-003 | Bit-identical proprietary device DSP | Out of agreed scope. Define measured and listening-based closeness criteria for independent implementations. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-004 | Redistributed proprietary content | Out of scope. Use original or appropriately licensed distributable media and presets. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-005 | 12.3 stem separation and Splice | Later-version features; exclude from the frozen 12.2.1 baseline unless the user later requests them. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-006 | 12.4 Link Audio and Learn View | Later-version features; ordinary Link synchronization remains a separate baseline item. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-007 | Later native-device revisions | Do not substitute current Auto Pan/Erosion/Delay/Chorus behavior for a 12.2.1 comparison without a recorded decision. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-008 | Unverified operating systems and architectures | Do not advertise Windows, Intel macOS, or additional targets until their build and behavior evidence exists. | NOT IMPLEMENTED / UNVERIFIED |

## Individual device comparison targets

The names below identify reference behaviors. They are not Halftrack product names, redistributed vendor content, or a promise to run vendor device binaries. The native list is anchored to the dated pre-12.2 manual and reconciled with the 12.2/12.2.1 release notes; current online descriptions must be checked for later revisions.

Every device needs the BASE checks and its indicated test profile. Profiles are Halftrack test-design proposals; they do not claim to document every parameter in the corresponding reference DAW device. Implementation of one general filter, sampler, or patcher does not complete the devices that can be built with it.

### Shared device test profiles

| Profile | Proposed test approach |
| --- | --- |
| BASE | Inventory the 12.2.1 controls/defaults/ranges/modes; document the independent Halftrack equivalents. Test bypass, parameter changes, automation/modulation, state, undo/reopen, rate/block changes, invalid values, real-time/offline rendering, and agreed listening/numeric comparisons. |
| FILTER | Measure response over frequency, gain, resonance, and slope; test stability and modulation sweeps without uncontrolled output. |
| NONLINEAR | Measure level-dependent transfer, spectra, aliasing, oversampling, DC offset, and gain staging using generated signals. |
| DYNAMICS | Measure threshold/ratio/knee, attack/release, lookahead, gain reduction, channel linking, sidechain paths, and latency. |
| MODULATION | Measure rate/phase/depth/stereo behavior, tempo sync, continuity at parameter changes, and saved phase policy. |
| TIME | Use impulses to measure delays, feedback, tempo changes, tails, modulation, and buffer-boundary behavior. |
| PITCH | Test known pitches, transitions, tracking error, formant behavior where relevant, latency, and silence/noise cases. |
| REVERB | Use original impulse responses and impulses to assess decay, early/late structure, convolution, tails, and CPU scaling. |
| RESONANCE | Excite models with impulses/noise; assess pitch, decay, damping, modulation, stability, and external control. |
| SPECTRAL | Test analysis window/latency, frequency/time behavior, freeze/shift settings, transients, and rate changes. |
| METER | Feed known tones/levels; check scale, precision, update timing, peak hold, and no unintended effect on the audio path. |
| ROUTING | Use distinct channel signals to verify gain, polarity, mono/stereo behavior, panning, and multichannel mapping. |
| EXTERNAL | Use loopback and a permitted external device; measure MIDI/audio paths, monitoring, compensation, and offline-render limits. |
| LOOPER | Record/overdub/undo/clear/reverse/resize loops while running; verify boundaries, state, and exported audio. |
| VOCODER | Use generated carrier/modulator fixtures; assess envelope/band behavior, routing, intelligibility, and latency. |
| MIDI | Compare timestamped input/output event logs, sustained notes, duplicates, channel/expression data, bounds, and stop/panic. |
| SYNTH | Test pitch, oscillator/filter/envelope/modulation behavior, voice allocation, retrigger, expression, and sustained performance. |
| PHYSICAL | Test excitation, pitch, decay, damping, expression, extreme settings, and stable finite output. |
| FM | Measure pitch/ratio and modulation relationships, envelopes, algorithms, voice behavior, and aliasing. |
| SAMPLER | Use original samples; test bounds, interpolation, pitch, loops, envelopes, slices, filters, and expression. |
| MULTISAMPLE | Add key/velocity zones, overlap/crossfade, release behavior, resource streaming, and supported import formats. |
| WAVETABLE | Use original tables; test interpolation/morphing, unison, modulation, expression, and aliasing. |
| DRUM | Compare transient, body, decay, tuning, expression, repeated triggering, and choking using generated patterns. |
| CONTROL | Measure modulation shape/range/rate/polarity, mapping ownership, automation interaction, reset, and persistence. |
| TOOL | Test note-selection boundaries, deterministic seeds, preview/apply, undo, repeated application, and range constraints. |
| RACK | Test chain order, parallel mixing, zones, macros, nested state, latency, and controller exposure. |
| GRANULAR | Use original sources; test grain timing/position/size, pitch, density, capture, expression, and bounded resource use. |
| MEASUREMENT | Recover a known synthetic response and validate measurement/export using independently analyzed signals. |
| CV | Use suitable test hardware; verify calibration, polarity, range, clock/gate events, and safe explicit output setup. |
| TUNING | Check known frequency ratios, mapping boundaries, note expression, bypass, and cross-device consistency. |
| PACK | First pin the actual reference revision and enumerate devices/capabilities; then split into device rows with suitable profiles. No bundled content is copied. |
| CONTENT | Implement the engine/browser capabilities using original or licensed fixtures; sample-library reproduction is not an acceptance target. |
| LEGACY | Enumerate accessible legacy device capabilities in installed 12.2.1 and record a scope decision; ALS/preset compatibility is not implied. |

### Native audio effects


Stage: **After preview approval**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| AFX-001 | Amp | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-002 | Auto Filter (12.2 revision) | FILTER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-003 | Auto Pan (12.2.1 behavior) | MODULATION | NOT IMPLEMENTED / UNVERIFIED |
| AFX-004 | Auto Shift | PITCH | NOT IMPLEMENTED / UNVERIFIED |
| AFX-005 | Beat Repeat | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-006 | Cabinet | FILTER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-007 | Channel EQ | FILTER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-008 | Chorus-Ensemble (12.2.1 behavior) | MODULATION | NOT IMPLEMENTED / UNVERIFIED |
| AFX-009 | Compressor | DYNAMICS | NOT IMPLEMENTED / UNVERIFIED |
| AFX-010 | Corpus | RESONANCE | NOT IMPLEMENTED / UNVERIFIED |
| AFX-011 | Delay (12.2.1 behavior) | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-012 | Drum Buss | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-013 | Dynamic Tube | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-014 | Echo | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-015 | EQ Eight | FILTER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-016 | EQ Three | FILTER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-017 | Erosion (12.2.1 behavior) | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-018 | External Audio Effect | EXTERNAL | NOT IMPLEMENTED / UNVERIFIED |
| AFX-019 | Filter Delay | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-020 | Gate | DYNAMICS | NOT IMPLEMENTED / UNVERIFIED |
| AFX-021 | Glue Compressor | DYNAMICS | NOT IMPLEMENTED / UNVERIFIED |
| AFX-022 | Grain Delay | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-023 | Hybrid Reverb | REVERB | NOT IMPLEMENTED / UNVERIFIED |
| AFX-024 | Limiter | DYNAMICS | NOT IMPLEMENTED / UNVERIFIED |
| AFX-025 | Looper | LOOPER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-026 | Multiband Dynamics | DYNAMICS | NOT IMPLEMENTED / UNVERIFIED |
| AFX-027 | Overdrive | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-028 | Pedal | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-029 | Phaser-Flanger | MODULATION | NOT IMPLEMENTED / UNVERIFIED |
| AFX-030 | Redux | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-031 | Resonators | RESONANCE | NOT IMPLEMENTED / UNVERIFIED |
| AFX-032 | Reverb | REVERB | NOT IMPLEMENTED / UNVERIFIED |
| AFX-033 | Roar | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-034 | Saturator | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-035 | Shifter | PITCH | NOT IMPLEMENTED / UNVERIFIED |
| AFX-036 | Spectral Resonator | SPECTRAL | NOT IMPLEMENTED / UNVERIFIED |
| AFX-037 | Spectral Time | SPECTRAL | NOT IMPLEMENTED / UNVERIFIED |
| AFX-038 | Spectrum | METER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-039 | Tuner | METER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-040 | Utility | ROUTING | NOT IMPLEMENTED / UNVERIFIED |
| AFX-041 | Vinyl Distortion | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-042 | Vocoder | VOCODER | NOT IMPLEMENTED / UNVERIFIED |

### Native MIDI effects


Stage: **After preview approval**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| MFX-001 | Arpeggiator | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-002 | CC Control | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-003 | Chord | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-004 | Note Length | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-005 | Pitch | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-006 | Random | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-007 | Scale | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-008 | Velocity | MIDI | NOT IMPLEMENTED / UNVERIFIED |

### Native instruments


Stage: **After preview approval**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| INS-001 | Analog | SYNTH | NOT IMPLEMENTED / UNVERIFIED |
| INS-002 | Collision | PHYSICAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-003 | Drift | SYNTH | NOT IMPLEMENTED / UNVERIFIED |
| INS-004 | Drum Sampler | SAMPLER | NOT IMPLEMENTED / UNVERIFIED |
| INS-005 | Electric | PHYSICAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-006 | External Instrument | EXTERNAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-007 | Impulse | SAMPLER | NOT IMPLEMENTED / UNVERIFIED |
| INS-008 | Meld | SYNTH | NOT IMPLEMENTED / UNVERIFIED |
| INS-009 | Operator | FM | NOT IMPLEMENTED / UNVERIFIED |
| INS-010 | Sampler | MULTISAMPLE | NOT IMPLEMENTED / UNVERIFIED |
| INS-011 | Simpler | SAMPLER | NOT IMPLEMENTED / UNVERIFIED |
| INS-012 | Tension | PHYSICAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-013 | Wavetable | WAVETABLE | NOT IMPLEMENTED / UNVERIFIED |

### Rack infrastructure


Stage: **After preview approval**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| RCK-001 | Instrument Rack | RACK | NOT IMPLEMENTED / UNVERIFIED |
| RCK-002 | Drum Rack | RACK | NOT IMPLEMENTED / UNVERIFIED |
| RCK-003 | Audio Effect Rack | RACK | NOT IMPLEMENTED / UNVERIFIED |
| RCK-004 | MIDI Effect Rack | RACK | NOT IMPLEMENTED / UNVERIFIED |

### Built-in Max-based reference devices


Stage: **After preview approval**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| MXL-001 | DS Clang | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-002 | DS Clap | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-003 | DS Cymbal | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-004 | DS FM | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-005 | DS HH | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-006 | DS Kick | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-007 | DS Sampler | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-008 | DS Snare | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-009 | DS Tom | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-010 | Align Delay | TIME | NOT IMPLEMENTED / UNVERIFIED |
| MXL-011 | Envelope Follower | CONTROL | NOT IMPLEMENTED / UNVERIFIED |
| MXL-012 | LFO | CONTROL | NOT IMPLEMENTED / UNVERIFIED |
| MXL-013 | Shaper | CONTROL | NOT IMPLEMENTED / UNVERIFIED |
| MXL-014 | Envelope MIDI | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-015 | Expression Control | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-016 | Expression Control Legacy | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-017 | MIDI Monitor | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-018 | MPE Control | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-019 | Note Echo | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-020 | Shaper MIDI | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MXL-021 | Velocity Shaper (MIDI Tool) | TOOL | NOT IMPLEMENTED / UNVERIFIED |
| MXL-022 | Euclidean (MIDI Tool) | TOOL | NOT IMPLEMENTED / UNVERIFIED |

### Additional content/Pack capability candidates


Stage: **After preview approval**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| PAK-001 | Granulator III | GRANULAR | NOT IMPLEMENTED / UNVERIFIED |
| PAK-002 | Expressive Chords (12.2-era revision) | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| PAK-003 | Convolution Reverb | REVERB | NOT IMPLEMENTED / UNVERIFIED |
| PAK-004 | Convolution Reverb Pro | REVERB | NOT IMPLEMENTED / UNVERIFIED |
| PAK-005 | IR Measurement Tool | MEASUREMENT | NOT IMPLEMENTED / UNVERIFIED |
| PAK-006 | PitchLoop89 | PITCH | NOT IMPLEMENTED / UNVERIFIED |
| PAK-007 | CV Tools | CV | NOT IMPLEMENTED / UNVERIFIED |
| PAK-008 | Microtuner | TUNING | NOT IMPLEMENTED / UNVERIFIED |
| PAK-009 | Creative Extensions | PACK | NOT IMPLEMENTED / UNVERIFIED |
| PAK-010 | Inspired by Nature | PACK | NOT IMPLEMENTED / UNVERIFIED |
| PAK-011 | Surround Panner | ROUTING | NOT IMPLEMENTED / UNVERIFIED |
| PAK-012 | Performance Pack | PACK | NOT IMPLEMENTED / UNVERIFIED |
| PAK-013 | Original multisample library capabilities | CONTENT | NOT IMPLEMENTED / UNVERIFIED |
| PAK-014 | Original drum/content library capabilities | CONTENT | NOT IMPLEMENTED / UNVERIFIED |
| PAK-015 | Other version-pinned Suite Packs | PACK | NOT IMPLEMENTED / UNVERIFIED |
| PAK-016 | Legacy native devices exposed by 12.2.1 | LEGACY | NOT IMPLEMENTED / UNVERIFIED |

## How to use this ledger after preview approval

Start from a complete song-making workflow and expand coverage while keeping the native app runnable. For each row, create the smallest meaningful reference fixture, then test its interactions with transport, save/reopen, undo, routing, automation, and rendering. Reuse general engine fixtures without pretending they verify every device's sound.

For a platform matrix, keep separate results for macOS Apple Silicon, any supported Intel macOS build, and each supported Windows architecture/backend. Native AU-only behavior should be explicitly platform-specific; shared VST/session behavior should use equivalent fixtures. “Windows build passed” and “Windows audio driver tested” are different evidence.

Device profiles need per-parameter cases before a device can be marked VERIFIED. Pack families need versioned device manifests before their scope can be called complete. Physical controllers and interface behaviors require real hardware evidence in addition to simulation.

The first preview review is a product gate, not a shortcut to full parity. Deliver it with its verified capabilities and remaining gaps, obtain the user's explicit approval, and only then execute this broader backlog.
