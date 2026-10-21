# Halftrack feature-parity ledger

Capability planning inventory, revised 2026-10-15. These are proposed behavior tests, not completed parity claims. See [specification policy](research/workflows.md).

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

## Native effects and note transformations

Source `6467066` passes all eleven local Mac suites in 168.69 seconds, including complete-file signal tests for EQ/compressor/delay/reverb, project-tempo divisions, atomic MIDI note transformations and native editor handlers. The extracted Mac package passes both new dedicated suites. Both the four-effect checkpoint `4ebb163` and combined source `6467066` pass macOS, Windows and Linux builds, native tests and source-inclusive packaging. [Verification and remaining limits](preview-verification.md#native-effects-note-tools-and-tempo-sync--2026-10-14) distinguish actual working subsets from full device parity.

## Channel utility subset

The new original Utility, refined using traced filter topology and a normalized balance law, passes all nine local Mac suites in 146.16 seconds. Its [contract](research/channel-utility-contract.md) records tested DSP, native controls, insertion/reordering, undo and saved state. The reference's filter orders and balance law are traced; exact output, approximation rounding, switching transients and smoothing remain unmeasured against this implementation; native input/hardware checks remain pending. Source `90c9eb2` passes [clean builds, acceptance and packaging on all three platforms](https://github.com/jamescolasacco/halftrack/actions/runs/38532090898).

## Current Session subset

Individual MIDI note activation is exposed through selected-note controls, stored native mute state and state-preserving clipboard operations. A reproduced held-note defect led to selecting the native beat sequencer for Arrangement as well as Session; generated tests compare a retained same-clip voice against an independent channel. MID-001 remains partial; controllers/MPE and complete editor parity are not established. See [the activation contract](architecture.md#midi-note-activation).

Scene duplication now has stable-ID UI/model integration with independent audio/MIDI copies, retained clip settings, undo/redo and generated native-driver playback tests. This deepens SES-001; it does not add a completed parity row. Scene follow actions and performance-to-Arrangement capture remain unfinished. See [architecture](architecture.md#session-scene-duplication).

The built-in instrument at `ea24869` adds sine, deterministic white noise and adjustable pulse width alongside the existing saw. Native controls, invalid-command preservation, project round trips, undo/redo and independent complete-buffer/harmonic tests pass all eight Mac suites in 133.15 seconds and [all three CI platforms](https://github.com/jamescolasacco/halftrack/actions/runs/38518528688). See the [oscillator contract](research/synth-oscillator-contract.md). Dual oscillators, full modulation, complete instrument-device reconstruction and measured reference sound matching remain absent or unverified.

Grouped piano-roll editing and the validating note-batch facade at `13406de` pass all eight local suites in 133.69 seconds. Shift selection/marquee, Select All/Escape, common group movement/resize, set-value velocity, group deletion, stable-ID pruning and one-step undo/redo have handler/model coverage, including 512 notes and rejection of infeasible loaded groups. This remains one-clip editing with Halftrack's current drawing convention, not full MIDI-editor parity; the published grouped-note checkpoint `9b59843` passes all three CI platforms.

The scanner follow-up `5580373` passes all three platforms in [CI 38499473562](https://github.com/jamescolasacco/halftrack/actions/runs/38499473562). Its Windows ordinary two-crash request takes 56.14 ms and the full scanner regression 1.08 s; see the [verification record](preview-verification.md) for the prior baseline and limits.

The current routing/plugin checkpoint `ce1128e` passes all eight local Mac suites in 133.65 seconds; published checkpoint `17a0575` now passes [clean builds, tests and packaging on all three platforms](https://github.com/jamescolasacco/halftrack/actions/runs/38496744486). The Session/export checkpoint `5961295` passes macOS, Windows and Linux CI. The newer plug-in checkpoint `727c36e` passes all seven local Mac suites in 88.10 seconds; its macOS/Linux CI passed and its Windows crash-test deadline failure is corrected and verified at `17a0575`. See [measured cases and limits](preview-verification.md#session-extension-local-verification). This is scoped implementation evidence, not complete Session parity. Manual checks UI-21 through UI-45 and physical hardware remain NOT RUN.

## Original preview acceptance record

The original audio-preview subset passed locally in **12.66 seconds** at `a6f7e2e`. Browser repair and native save/reopen/export were subsequently observed, and the earlier preview later passed three-platform CI at `1294f5b`; details are in the [verification record](preview-verification.md). Those results do not certify the continued Session candidate. **PARTIAL** never implies the whole row or another platform has passed.

| ID | Capability / target | Acceptance evidence and remaining gap | Status |
| --- | --- | --- | --- |
| PRE-001 | Launch and honest status | Native Mac launch was observed after the browser stopped scanning Music at startup; chosen-directory scanning runs asynchronously. Session/export builds and tests pass across all three CI platforms at `5961295`; physical GUI parity remains unverified. | PARTIAL — Mac launch observed |
| PRE-002 | Device selection | Earlier GUI reported CoreAudio built-in output, 96 kHz / 512 samples, no input. No-device guards and virtual-device inputs passed. Hardware switching, permissions, hot-plug, and latency remain pending. | PARTIAL — observed output configuration |
| PRE-003 | Record a take | Generated input passed the real recording graph; stop created a WAV clip with expected rate, duration bounds, and signal. Export and save passed. Physical/acoustic capture and alignment remain pending. | PARTIAL — Mac synthetic capture |
| PRE-004 | Import and arrange audio | Earlier GUI imported four generated stems, showed waveforms, fitted the timeline, and played/stopped with advancing transport. API clip gesture undoes once. GUI clip placement/movement and seek checks remain pending. | PARTIAL — Mac import/playback observed |
| PRE-005 | Basic mix | Two-track summing, gain, pan, mute, solo, and grouped gain/pan undo passed API checks. GUI track/master meters were active. GUI mixer gestures and Windows remain pending. | PARTIAL — automated mix; meters observed |
| PRE-006 | Plug-in audition | Controlled VST3 discovery, instantiation, parameter change, processing, state round trip, bypass, and removal passed. Controlled AUv2 processing/state and generated custom/generic editor lifecycle now pass locally at `727c36e`. Four deterministic worker-completion cases also pass locally at `6235185`; AUv3, arbitrary vendor plug-ins and native input delivery remain separate checks. | PARTIAL — controlled Mac VST3/AUv2 |
| PRE-007 | Export | 48 kHz WAV PCM and duration passed; a two-second extension produces 144,000 frames from a one-second dry source. Worker cancellation dispatches messages and preserves existing output bytes. Native Mac export was decoded independently. The current effects suite additionally compares complete files with one second of actual delay/reverb tail. The export-format suite adds WAV 16/24/32-bit and AIFF/FLAC 16/24-bit full-frame checks, 44.1 kHz resampling, cancellation and corrected-extension overwrite UI. The full rate matrix, arbitrary-plugin tails and hardware checks remain pending. | PARTIAL — tested export/cancel/tails |
| PRE-008 | Save and reopen | Portable project/media, track name, tempo, loop, rendered audio, and VST3 state passed; invalid load retained the edit. Clean/dirty initialization regressions passed. Native Mac audio-demo save/reopen was observed; complete state coverage remains pending. | PARTIAL — tested API cases |
| PRE-009 | Basic stability | Original preview workflows and scoped continued Session tests passed; the browser stall was repaired. Repeated device changes, arbitrary plug-in editor lifetime, and long-session checks remain pending. Full integration passes locally at `727c36e`; the subsequent integrated routing/plugin checkpoint `17a0575` passes macOS, Windows and Linux CI. | PARTIAL — bounded automated/GUI evidence |
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
| REC-003 | Tempo and signatures | Test manual tempo, tap tempo, nudge, signature changes, tempo ramps, and transitions across clips. Tap tempo now uses a bounded four-interval estimate with no-op/history/project and rendered timing tests; physical tapping and reference estimator matching remain unverified. | PARTIAL — constant 20–999 BPM, precise MIDI remapping, rendered onset/duration, synchronized delay, validated persistence and undo; tap tempo tested; one project time signature (1–99 over 1/2/4/8/16, quarter-note beats) with rendered beat timing, a 3/4 bar-quantized launch, MIDI file meta, undo, reopen and rejection tested; tempo maps with linear ramps (see MIX-016); nudge and signature changes missing |
| REC-004 | Loop and punch | Record through repeated loop boundaries and punch regions; inspect placement and partial-buffer boundaries. | NOT IMPLEMENTED / UNVERIFIED |
| REC-005 | Metronome and count-in | Verify beats, accents, routing, count-in, odd signatures, and start-on-grid behavior against recorded impulses. | PARTIAL — native click/accent/level/record-only mode with accents exact in 4/4, 3/4 and 5/4, zero/one/two-bar count-in, exact generated click/recorded PCM, MIDI boundaries, cancellation, undo/persistence and export exclusion; cue routing, eighth-note click subdivision and reference/physical timing unverified |
| REC-006 | Simultaneous multitrack capture | Record distinct signals to several tracks; verify alignment, channel isolation, file closure, and stop behavior. | NOT IMPLEMENTED / UNVERIFIED |
| REC-007 | Recording formats and disk failure | Verify supported bit depths and sample rates; simulate storage failure without corrupting previously committed takes. | NOT IMPLEMENTED / UNVERIFIED |
| REC-008 | MIDI recording and overdub | Record note, controller, sustain, and expression streams; verify overdub policy and quantization choices. | PARTIAL — native single-channel Arrangement capture/merge overdub, seven recording grids with bounded starts and retained durations/controller timing, release velocity/CC/bend/pressure, monitoring, one-take undo, editable identities and persistence tested with generated inputs; MPE, quantized loop/offset capture, take controls and physical timing missing/unverified |
| REC-009 | MIDI capture and step input | Specify capture-history boundaries and step-entry operations; compare reference workflows using generated MIDI. | NOT IMPLEMENTED / UNVERIFIED |
| REC-010 | Take lanes and comping | Record several takes, audition lanes, construct a comp, edit transitions, and undo/reopen the result. | NOT IMPLEMENTED / UNVERIFIED |
| REC-011 | Linked-track edits | Edit grouped microphone takes together; check phase alignment and per-track exceptions. | NOT IMPLEMENTED / UNVERIFIED |
| REC-012 | External synchronization | Exercise MIDI clock in/out, transport messages, clock loss/rejoin, sync offset, Link tempo/phase, and tempo-following behavior. | NOT IMPLEMENTED / UNVERIFIED |

## Arrangement and clip editing


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| ARR-001 | Timeline navigation | PARTIAL — paging Follow (button, Cmd/Ctrl+F, off on manual scroll) in the Arrangement and piano roll, +/− and Cmd-scroll zoom, Z/X zoom to and back from the selection; UI tests cover paging, manual-scroll cancel and exact zoom views. Continuous follow, vertical zoom and reference comparison missing | PARTIAL |
| ARR-002 | Tracks and groups | PARTIAL — rename, colour, reorder, duplicate and delete tracks; one-level submix groups with gain/pan/mute/solo/fold, ungroup, undo and persistence; Session group columns with group slot launch/stop and a group mixer strip; nesting missing | PARTIAL |
| ARR-003 | Move, trim, loop, slip | Exercise clip edges, source offsets, negative/late starts where supported, and repetition without destructive media changes. | PARTIAL — numeric and direct edge trim/source slip, bounds, snapping, undo, stable-ID audio/MIDI cross-track move; warp/loop edge editing missing |
| ARR-004 | Selection and grid | PARTIAL — drag-selected time across tracks (grid snap, Alt bypass, Shift extend) with Split, Delete, Duplicate, Loop Selection and Consolidate (MIDI join, sample-exact audio render), MIDI clip splitting and selection-based Insert/Delete Time; sample-exact renders, note layouts, undo and rejections are tested. Duplicate/Delete carry and clear the selected tracks' automation; ruler selection, range clipboard, adaptive/triplet grids and reference comparison missing | PARTIAL |
| ARR-005 | Cut, copy, paste, duplicate | Verify cross-track/time moves, copy modifiers, clipboard scope, undo, and automation ownership. | PARTIAL — audio and MIDI clip Cut/Copy/Paste across tracks/projects, beat-correct MIDI paste, fresh identities, duplicate/undo and portable media; multi-clip/time-range clipboard missing |
| ARR-006 | Split and consolidate | Test edits at start/end/loop boundaries; compare reconstructed audio and handle tails or selection gaps explicitly. | PARTIAL — interior split preserves complete rendered audio; consolidate missing |
| ARR-007 | Time insertion/removal | PARTIAL — Insert Silence and Delete Time across every track, splitting editable audio clips, opening or closing MIDI clips between their notes, and moving locators and all automation; one undo step each; a render resumes the split source exactly; warped crossing clips are rejected. Duplicate/paste time, tempo changes and a dedicated time selection missing (the loop range is used) | PARTIAL |
| ARR-008 | Fades and crossfades | Edit overlapping clips and edge fades; test click avoidance, curve state, persistence, and render agreement. | PARTIAL — four manual curves, timeline handles, overlap/render/state tests; linked/automatic fades missing |
| ARR-009 | Locators and loop regions | Create/name/jump/delete locators and edit loop endpoints; test meter changes and playback jumps. | PARTIAL — named absolute locators, seek/navigation, snapped/free drag, native undo/save/load and loop ruler; beat-locked markers, meter-change and reference playback parity unverified |
| ARR-010 | Freeze and bounce | PARTIAL — Freeze renders Arrangement clips through the instrument and devices and plays the render exactly (generated Sampler case), with locked devices/clips/notes and a live mixer; Flatten writes the render to project audio on the same track; undo, UI menu and frost display tested. Session-clip freeze, portable renders and partial freeze points missing | PARTIAL |
| ARR-011 | Session/Arrangement ownership | Native generated output verifies boundary takeover, stop retaining Session silence, delayed UI publication, graph rebuild and explicit Return; handler tests prove view switches do not change ownership. Stale-generation and same-pitch MIDI handoff cases pass. Native manual delivery and hardware timing remain unverified. | PARTIAL — Mac Session subset |
| ARR-012 | Clip properties | Persist activation, name/color, source bounds, loop bounds, gain, tuning, RAM/interpolation options, and batch edits. | PARTIAL — audio/MIDI display naming and activation with running playback, undo and persistence; gain, fades and source bounds persist; broader clip properties missing |

## Session performance and launching


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| SES-001 | Clip grid and scenes | Create/import/clear slots; add/rename/delete scenes; undo, stable IDs, exclusive playback and empty-slot stop/silent arrangement takeover pass engine/handler cases. Slot MIDI editing, clip activation and occupied-destination rejection pass. Clip/scene move/copy and complete native interaction remain missing or unverified. | PARTIAL — Mac Session subset |
| SES-002 | Quantized launch and stop | None/Beat/Bar launch/stop, tempo-aware scheduling, loop wrap and stopped transport pass generated 48 kHz/128-frame cases; onset tolerance is 256 frames and tested scene-channel alignment is within one sample. Scene commands are serialized in the fixture; concurrent device callbacks can split native per-track enqueue across blocks. Exact request-edge timing and hardware jitter remain unverified. | PARTIAL — Mac generated timing |
| SES-003 | Launch modes and legato | Trigger, Gate, Toggle and Repeat press/release semantics, per-clip launch quantization (Global, None, 8 Bars–1/32) and legato phase pass generated 48 kHz/128-frame cases (Repeat retriggers match within 1e-4, including one on a block boundary after an engine patch). Launch box UI and mouse/Enter press/release are checked. Triplet grids, launch velocity and reference timing comparison are missing. | PARTIAL — Mac generated timing |
| SES-004 | Follow actions | Exercise clip and scene actions, probabilities, repetition, timing, and global disable with seeded test fixtures. | PARTIAL — beat-timed audio/MIDI Stop/Replay/Previous/Next/First/Last/Cycle/Any/Other with an action B and chance of A (0%/100% deterministic, 50% reaching both outcomes, No action continuing play), sparse slots, running history, phase/tempo/queued-start tests, UI and persistence; loop-linked timing, group/scene actions and global disable missing |
| SES-005 | Scene tempo/signature | Launch scenes that change tempo/meter; verify transport continuity and existing clip phase. | NOT IMPLEMENTED / UNVERIFIED |
| SES-006 | Session recording | MIDI and audio record into empty slots of armed tracks from the next launch boundary to a grid stop, then loop and play in phase. Generated MIDI events land exactly at their offsets from the punch-in, an earlier key is excluded, and generated audio input fills a two-beat take; slot buttons, undo and transport stop are covered. Overdub, Session Record button, fixed length, count-in and reference timing missing. | PARTIAL — Mac generated cases |
| SES-007 | Performance capture | Record a launch performance into the Arrangement and replay equivalent timing. | NOT IMPLEMENTED / UNVERIFIED |
| SES-008 | Controller feedback | Clip queued/playing/stopped and track ownership display actual engine state; Enter/Shift+Enter, scene/slot selection and stop/return handlers pass. External controller feedback, recording state and native OS key delivery remain unverified. | PARTIAL — native UI handler subset |

## Audio clip processing and timing


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| WRP-001 | Unwarped audio | Verify sample-rate conversion policy, start offset, pitch, reverse, looping, gain, and end-of-file handling. | PARTIAL — gain/offset/duration/reversal, immediate export/undo/redo and full-buffer tests at 48 kHz; pitch, looping controls and broader conversion unverified |
| WRP-002 | Transient and tempo analysis | PARTIAL — bar-length tempo estimate only; no transient or content analysis | PARTIAL |
| WRP-003 | Warp-marker editing | Add/move/remove/reset markers; test segment continuity, undo, and save/reopen. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-004 | Rhythmic stretching | PARTIAL — warped clips follow tempo via Signalsmith stretch/efficient modes; onsets within ~1 ms of the grid at 60–240 BPM after engine alignment fixes. Transient-slicing Beats mode missing | PARTIAL |
| WRP-005 | Tonal and textured stretching | Compare sustained-tone and granular-texture modes; document artifacts and deterministic settings. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-006 | Repitch behavior | Verify coupled pitch/speed changes and tempo automation using known-frequency fixtures. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-007 | Complex stretching | Assess polyphonic and formant-aware processing on permitted material; record subjective and numeric tolerances. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-008 | Leader/follower tempo | PARTIAL — warped clips follow the project tempo with clip BPM and length in beats; clips cannot lead the tempo | PARTIAL |
| WRP-009 | Groove pool | Extract, apply, edit, commit, and remove timing/velocity grooves without losing original note/marker data. | PARTIAL: project groove pool with Base, Quantize, Timing, Random, Velocity and Global Amount; non-destructive MIDI clip playback, Commit, extraction, `.agr` reading and a generated library, checked by rendered onsets and accents ([contract](research/grooves-contract.md)). Audio-clip grooves, hot-swap and measured reference arithmetic are missing. |
| WRP-010 | Slicing and conversion | Test transient slicing and separate melody, harmony, and drum transcription fixtures; report errors rather than imply perfect inference. | NOT IMPLEMENTED / UNVERIFIED |
| WRP-011 | Shared media edits | Verify that duplicate clips and alternate sessions do not unexpectedly mutate each other's source files. | NOT IMPLEMENTED / UNVERIFIED |

## MIDI editing, tools, MPE, and tuning


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| MID-001 | Piano-roll editing | Production handlers create/select/move/resize/delete and undo notes in arrangement and Session clips. Selected empty Session slots do not open another clip. Copy/Cut/Paste/Duplicate, visible insertion cursor, whole-group rejection, fresh-ID selection and text-focus isolation now pass the Mac handler suite at `d5a3688`. Marker splitting now has atomic batch, complete-render, undo/reopen and native handler tests; note deactivation and complete native input delivery remain unverified or missing. | PARTIAL — Mac editor handlers |
| MID-002 | Pitch/time operations | Eight deterministic note tools, complete-group undo, bounds/selection validation, arrangement/Session save/reopen and production quantize shortcut pass locally and in three-platform CI at `6467066`. Existing arrows transpose. Scale-aware operations and measured reference equivalence remain missing/unverified. | PARTIAL — automated three-platform model/UI checks |
| MID-003 | Velocity, release, probability | Persist expressive values and ranges; use deterministic seeds to test chance and repeated-note behavior. | NOT IMPLEMENTED / UNVERIFIED |
| MID-004 | Multi-clip editing | Display/edit several MIDI clips; verify focus, selection boundaries, and notes at overlapping loop positions. | NOT IMPLEMENTED / UNVERIFIED |
| MID-005 | Controller/program events | PARTIAL — step-wise CC 0–119, pitch bend and channel pressure lanes per MIDI clip with a piano-roll lane (velocity bars too); rendered bend pitch (zero-crossing measured), sustain hold/release, time edits, undo, reopen, MIDI file round trip and duplication are tested. Ramped envelopes, program/bank editing, release velocity and reference comparison missing | PARTIAL |
| MID-006 | Note lifecycle | Generated Session MIDI loops stop without stuck voices; held arrangement notes release on takeover while same-pitch and different-pitch new notes survive. Instrument replacement, panic, device loss and general overlapping-note semantics remain unverified. | PARTIAL — Mac generated handoff |
| MID-007 | Transformation tools | PARTIAL — Strum, Chop, Span, Recombine, Velocity Shaper, Arpeggiate, Ornament and Connect with settings windows, hand-computed note-for-note results and single-step undo tested; Time Warp, the reference's exact algorithms and live preview missing | PARTIAL |
| MID-008 | Generative tools | PARTIAL — Euclidean and seeded random (Seed) generators with exact pattern, range and repeatability tests; Rhythm, Shape, Stacks and reference comparison missing | PARTIAL |
| MID-009 | MPE recording/editing | Record and edit per-note pitch, pressure, slide, and release data; preserve association after trimming and copying. | NOT IMPLEMENTED / UNVERIFIED |
| MID-010 | MPE routing | Verify member-channel allocation, zones, pitch range, note stealing, and plug-in delivery. | NOT IMPLEMENTED / UNVERIFIED |
| MID-011 | Scales and alternate tunings | PARTIAL — per-clip scale (16 modes, any root) with piano-roll highlighting and a Fit to Scale note tool; membership, nearest-tone fitting, undo, duplication, reopen and corrupt-file rejection tested. Fold, scale-aware devices, tunings and reference comparison missing | PARTIAL |
| MID-012 | MIDI file import/export | Round-trip supported event types, tempo and meter maps, track/channel assignments, and fractional timing. | PARTIAL — PPQ type 0/1 import, channel tracks, notes/CC/program/bend/pressure/SysEx round trips, clip export, native undo/persistence and synth render oracle; tempo/meter-map import, SMPTE/type 2 and same-pitch overlap resolution missing |
| MID-013 | MIDI ports and mappings | Exercise input/output filtering, virtual ports, remote mappings, learn, pickup/scaling, relative controls, and persistence. | NOT IMPLEMENTED / UNVERIFIED |

## Mixer, routing, racks, and automation


Stage: **Further implementation authorized**. Unverified rows are proposed checks; PARTIAL rows name the limited evidence already recorded.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| MIX-001 | Gain, pan, meters | Master output from silence through +6 dB, complete stereo/return gain arithmetic, gesture undo and portable level pass locally at `6cc92ef`. Reference channel/master fader laws, pan/balance modes, peak/RMS behavior, overload indication and display ballistics remain unmeasured. | PARTIAL — Mac native master output |
| MIX-002 | Mute/solo/arm rules | PARTIAL — exclusive solo (tracks and groups) and exclusive arm with Cmd/Ctrl-click to add, one undo step per click; UI-tested. Exclusivity preferences, cue solo and reference comparison missing | PARTIAL |
| MIX-003 | Returns and sends | Native audio/MIDI Pre/Post feeds, shared returns, effect ordering/bypass, mute/solo, preserved 127-sample tails, undo and portable routing pass locally at `0bd4d12`; malformed back-routes reject before project replacement. Nested groups, send automation, feedback and reference combined-solo measurements remain unfinished. | PARTIAL — Mac native aux graph |
| MIX-004 | Internal routing taps | Route pre/post processing and post-mixer signals; verify multichannel sources and feedback graph policy. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-005 | Cue and crossfade | PARTIAL — per-track A/B crossfader assignment and a project crossfader with a dipless cosine law, ramped per block in the track mixer; generated renders match the law at several positions, with undo, persistence, load validation and Session-mixer UI. Cue output, preview level and selectable crossfader curves missing | PARTIAL |
| MIX-006 | Resampling and external inserts | Capture internal buses; loop external hardware with measured compensation and explicit render constraints. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-007 | Delay compensation | Controlled offline VST3 serial/parallel, bypass and removal cases pass. Two renderer defects are corrected; ten edge exports at 48000/64 frames and 127 through 635 samples of graph latency match their dry reference exactly with finite values and exact length. Aux return/source paths also pass complete 48000/64-frame comparisons at `0bd4d12`. Hardware compensation, sidechains, dynamic latency and monitoring exceptions remain unverified. | PARTIAL — Mac controlled offline graph |
| MIX-008 | Track delay | PARTIAL — −1000 to +1000 ms per track: positive delays through a preallocated ring, negative ones through engine delay compensation; sample-exact impulse renders against an undelayed track, sends, undo, reopen, rejection and the mixer field are tested. Sample/beat units, master/group delays and reference comparison missing | PARTIAL |
| MIX-009 | Serial and parallel devices | PARTIAL — Instrument, Audio Effect and MIDI Effect Racks with up to 128 parallel chains of built-in devices and Utilities, nested four deep: add, duplicate, reorder, remove and bypass chains and devices, chain volume/pan/activator/solo, grouping and ungrouping, latency alignment, undo and load validation; generated renders match the expected sums sample by sample, and the callback makes no heap calls. Third-party plug-ins in racks, hot-swap and reference comparison missing | PARTIAL |
| MIX-010 | Drum racks | PARTIAL — 128-note Drum Rack (16-pad banks): pad chains receiving one note or All Notes with their own play note, sample drop into a one-shot Sampler chain, pad swap, 16 choke groups fading over 5 ms, and post-fader sends to up to six return chains inside the rack; exact pad renders, choke ramps, sends, save/move/reopen and the pad grid are tested. Pad outputs to the project's returns, chain extraction and reference comparison missing | PARTIAL |
| MIX-011 | Key/velocity/selector zones | PARTIAL — key, velocity and chain select zones with linear fade ranges and a zone editor (edge, fade and whole-zone drags); splits, overlaps, fades and selector moves through macros and automation render as computed. Chain Selector MIDI-controller filtering and measured reference fade shapes missing | PARTIAL |
| MIX-012 | Macros and variations | PARTIAL — 16 macros (8 visible by default) mapping device controls, chain volume/pan/sends and the chain selector with min/max ranges and inversion, naming, undo, block-rate automation and save/reopen are tested with generated renders; the mapping list and macro knobs have UI checks, and device controls also offer an untested Map to Macro menu. Variations, randomize and sample-accurate macro automation missing | PARTIAL |
| MIX-013 | Automation editing | PARTIAL — graphical track volume/pan, master-volume and every native device/instrument control lane: point editing, snapping, Read bypass, typed values in control units, undo and portable persistence; device lanes render at block rate following the knob scale (generated Sampler level checks). Curves, simplification, clip envelopes, third-party plug-in parameters and capture missing | PARTIAL |
| MIX-014 | Automation capture/override | Record from UI/controller, override playback, re-enable it, and resolve Session versus Arrangement ownership. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-015 | Clip modulation | Test linked/unlinked envelope loops, parameter scaling, automation interaction, and device replacement. | NOT IMPLEMENTED / UNVERIFIED |
| MIX-016 | Tempo automation | PARTIAL — a linear-ramp tempo map (Song Tempo lane, setTempoMap) with beat-preserving remapping; rendered onsets through a drop and a ramp, UI conversions, undo, reopen, MIDI export and rejections are tested. Curved segments, time edits with a map and reference comparison missing | PARTIAL |
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
| PRJ-002 | Save/reopen and Save As | Generated Session cases preserve scenes/slots, MIDI notes, quantization, names and stable IDs; invalid launcher shape preserves the current edit and undo/redo. Save As now preserves pending musical undo/redo in mixed audio/MIDI projects; same-name folder moves refresh both native audio caches and current arrangement/slot output. Focused Mac regressions pass at `c98e564`/`d96f804`; broad automation and full native-dialog coverage remain unverified. | PARTIAL — Mac project subset |
| PRJ-003 | Undo/redo history | Test transactions spanning several objects, invalidated references, saved-state markers, and history limits. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-004 | Autosave and recovery | PARTIAL — 30-second recovery copies of unsaved work (atomic, absolute media, no history or file change), a startup Recover/Discard prompt and untitled recovery; identical renders after recovery, an untouched original file, damaged-copy rejection and the prompt flow are tested. Interrupted recordings and multiple copies are not recovered | PARTIAL |
| PRJ-005 | Media collection and relocation | Generated slot audio is collected and reopens after moving project plus Assets and removing the original. Missing media rejects replacement. Cross-system, case and arbitrary media-library cases remain unverified. | PARTIAL — Mac relocation subset |
| PRJ-006 | Asset formats | Enumerate actual decoder/encoder support; test WAV/AIFF/FLAC and supported compressed formats with invalid/truncated inputs. | PARTIAL — WAV 16/24/32-bit and AIFF/FLAC 16/24-bit exports, actual container signatures, full decoded stereo samples at 48/44.1 kHz and invalid writer-parameter preservation; broader decoder/import/corrupt-file coverage unverified |
| PRJ-007 | Browser indexing | Scan large permitted libraries asynchronously; handle duplicate names, moved files, cancellation, and unavailable paths. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-008 | Search, tags, collections | Specify search/filter semantics, user tags, favorites, and 12.2-era browser interactions. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-009 | Preview and hot swap | Audition files without altering the mix; swap compatible sources/devices while retaining intended mappings. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-010 | Similarity search | Evaluate a Halftrack-owned similarity model against a known library; report relevance and compute cost. | NOT IMPLEMENTED / UNVERIFIED |
| PRJ-011 | Templates and presets | PARTIAL — native device and Utility presets in a user library (save with overwrite confirmation, browser listing, load in one undo step) and a default set for New; exact settings round trips, MIDI-track rules, damaged-preset rejection and an untitled unchanged template project are tested. Rack/plug-in/track/clip presets and hot-swap missing | PARTIAL |
| PRJ-012 | Stereo mix render | Whole Arrangement WAV/AIFF/FLAC export, explicit tail, rate/depth, stereo or averaging mono, bounded sample-peak normalization and exact selected intervals with prior synth/effect history. Generated decoded-file tests cover cancellation, destination/history preservation, selected-only normalization and sample-identical slices of full Halftrack renders. Isolated extra tails for a selected range, dither, loudness normalization and reference numerical parity remain unverified. | PARTIAL — generated native offline exports |
| PRJ-013 | Stems and selected tracks | PARTIAL — All Individual Tracks renders each track and group dry of returns, each return with its sends, and the Master mix into named files; generated-signal stems match their expected sources exactly; no overwrite without consent; project state, history and saved flag unchanged; export window choice and replace confirmation tested. Selected-track subsets and pre-group member stems missing | PARTIAL |
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
| QA-003 | Deterministic engine fixtures | Independent mixer, synth sine/noise/pulse, MIDI and plug-in-delay fixtures pass macOS, Windows and Linux CI at `ea24869`; see the oscillator and preview verification records. Cross-platform captured-buffer equivalence and paired reference renders remain unverified. | PARTIAL — independent fixtures in three-platform CI |
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


Stage: **Excluded / version review**, except OUT-003, restored by the later exact-output request. All checks below are proposed, not performed.

| ID | Capability / target | Proposed acceptance evidence | Status |
| --- | --- | --- | --- |
| OUT-001 | ALS import/export or round-trip | Out of agreed scope. Do not advertise reference DAW Set compatibility or silently add a compatibility commitment. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-002 | Existing AMXD/Max runtime compatibility | Out of agreed scope. The target is an independent extensible environment with its own contract. | NOT IMPLEMENTED / UNVERIFIED |
| OUT-003 | Exact-output device comparisons | The later owner request supersedes the earlier exclusion. Compare controlled outputs where possible and report tolerances/differences; static extraction cannot establish whole-product identity. | REQUESTED — reference equality unverified |
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


Stage: **Further implementation authorized**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| AFX-001 | Amp | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-002 | Auto Filter (12.2 revision) | PARTIAL — original SVF low/high/band/notch, 12/24 dB, LFO (six shapes, Hz/tempo, stereo phase) and envelope follower; analytic magnitude tests. Morph/vowel/DJ circuits and numerical parity missing | PARTIAL |
| AFX-003 | Auto Pan (12.2.1 behavior) | PARTIAL — original closed-form tremolo/panning with six shapes, Hz/tempo, phase and offset; transport-locked phase. Spin/stereo modes and parity missing | PARTIAL |
| AFX-004 | Auto Shift | PITCH | NOT IMPLEMENTED / UNVERIFIED |
| AFX-005 | Beat Repeat | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-006 | Cabinet | FILTER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-007 | Channel EQ | PARTIAL — HP 80 Hz, 100 Hz low shelf ±15 dB, 120 Hz–7.5 kHz mid peak ±12 dB, high shelf whose cut adds a 20–8 kHz low-pass, and output, with closed-form section checks ([contract](research/native-effects-contract.md#channel-eq-2026-10-20)); the spectrum display, undocumented slopes and reference comparison missing | PARTIAL |
| AFX-008 | Chorus-Ensemble (12.2.1 behavior) | PARTIAL — original 1–3 voice modulated-delay chorus with spread/feedback; Ensemble/Vibrato modes and parity missing | PARTIAL |
| AFX-009 | Compressor | Linked peak/RMS compressor, soft knee, makeup and parallel mix; independent dynamics tests and native project/editor integration. Sidechain/lookahead and numerical parity remain missing or unverified. [contract](research/native-effects-contract.md). | PARTIAL — automated three-platform acceptance |
| AFX-010 | Corpus | RESONANCE | NOT IMPLEMENTED / UNVERIFIED |
| AFX-011 | Delay (12.2.1 behavior) | Stereo and ping-pong delay, feedback damping, link, freeze and complete export tails; eleven project-tempo divisions are tested; sample-accurate tempo automation and reference parity remain missing or unverified. [contract](research/native-effects-contract.md). | PARTIAL — automated three-platform acceptance |
| AFX-012 | Drum Buss | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-013 | Dynamic Tube | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-014 | Echo | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-015 | EQ Eight | Eight serial bands, seven filter modes plus off, output/mix and actual response plot; independent response and project tests. Advanced modes and numerical parity remain missing or unverified. [contract](research/native-effects-contract.md). | PARTIAL — automated three-platform acceptance |
| AFX-016 | EQ Three | PARTIAL — Linkwitz–Riley 24/48 dB three-band isolator with all-pass-aligned recombination, kill switches; parity unmeasured | PARTIAL |
| AFX-017 | Erosion (12.2.1 behavior) | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-018 | External Audio Effect | EXTERNAL | NOT IMPLEMENTED / UNVERIFIED |
| AFX-019 | Filter Delay | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-020 | Gate | Original Noise Gate: linked peak detection, hysteresis, attack/hold/release, floor, duck and mix; independent envelopes, export, state/history/editor and callback tests pass locally. Sidechains, lookahead, metering and numerical reference parity remain missing or unverified. | PARTIAL — original native dynamics device |
| AFX-021 | Glue Compressor | PARTIAL — original Bus Compressor with discrete ratio/attack/release, auto release, range, soft clip and mix; sidechain and parity missing | PARTIAL |
| AFX-022 | Grain Delay | TIME | NOT IMPLEMENTED / UNVERIFIED |
| AFX-023 | Hybrid Reverb | REVERB | NOT IMPLEMENTED / UNVERIFIED |
| AFX-024 | Limiter | Original linked-stereo Peak Limiter, fixed lookahead, gain/ceiling/release and native latency reporting. Independent five-rate windows, driven complete exports, short/resampled/serial/parallel/return compensation and state/editor/allocation tests pass locally. True-peak, adaptive release, metering and numerical reference parity remain unverified. | PARTIAL — original native sample-peak device |
| AFX-025 | Looper | LOOPER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-026 | Multiband Dynamics | DYNAMICS | NOT IMPLEMENTED / UNVERIFIED |
| AFX-027 | Overdrive | PARTIAL — original band-pass drive with tone and dynamics restoration; measured-direction tests only; parity unmeasured | PARTIAL |
| AFX-028 | Pedal | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-029 | Phaser-Flanger | PARTIAL — separate original Phaser (2–12 all-pass stages) and Flanger devices; Doubler mode, envelope and tempo sync missing | PARTIAL |
| AFX-030 | Redux | Original Bitcrusher: signed quantization, fractional sample holding, drive, wet output and mix. Hand-calculated samples, held tail, history/project/editor and allocation tests pass locally. Reference filters, jitter, legacy mode and numerical identity are unverified. | PARTIAL — original native reduction device |
| AFX-031 | Resonators | RESONANCE | NOT IMPLEMENTED / UNVERIFIED |
| AFX-032 | Reverb | Original eight-line room with pre-delay, decay, size, damping, width and mix; reflected-impulse and export-tail tests. Proprietary topology and full controls remain missing or unverified. [contract](research/native-effects-contract.md). | PARTIAL — automated three-platform acceptance |
| AFX-033 | Roar | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-034 | Saturator | PARTIAL — six closed-form curves, DC filter, soft clip and mix; waveshaper/color controls and oversampling missing | PARTIAL |
| AFX-035 | Shifter | PARTIAL — Frequency Shifter: single-sideband ±5 kHz shift and ring modulation; pitch mode missing | PARTIAL |
| AFX-036 | Spectral Resonator | SPECTRAL | NOT IMPLEMENTED / UNVERIFIED |
| AFX-037 | Spectral Time | SPECTRAL | NOT IMPLEMENTED / UNVERIFIED |
| AFX-038 | Spectrum | METER | NOT IMPLEMENTED / UNVERIFIED |
| AFX-039 | Tuner | PARTIAL — monophonic YIN pitch detection (30 Hz–4.2 kHz) on a lock-free tap, 410–480 Hz reference, note and cents or Hz with the target-mode meter; detection within half a cent, pass-through, offline and live readings tested ([contract](research/native-effects-contract.md#tuner-2026-10-20)). Strobe mode, Histogram view, note spellings and reference comparison missing | PARTIAL |
| AFX-040 | Utility | Working original processor/editor, 320 matrix cases, bass/DC oracles, complete-file exports, insertion/order, undo and saved state; [contract](research/channel-utility-contract.md). Reference numerical identity unverified. | PARTIAL — automated Mac/Windows/Linux acceptance; reference numerical parity unverified |
| AFX-041 | Vinyl Distortion | NONLINEAR | NOT IMPLEMENTED / UNVERIFIED |
| AFX-042 | Vocoder | VOCODER | NOT IMPLEMENTED / UNVERIFIED |

### Native MIDI effects


Stage: **Further implementation authorized**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| MFX-001 | Arpeggiator | PARTIAL — eight styles, 1–4 octaves, nine synced rates, gate, hold, fixed velocity; transport-grid and free timing; engine renders equal written notes. Retrigger/repeats/swing/transpose steps missing | PARTIAL |
| MFX-002 | CC Control | MIDI | NOT IMPLEMENTED / UNVERIFIED |
| MFX-003 | Chord | PARTIAL — six shifts with per-shift velocity, merged duplicates, shared-tone ownership; render equals written chord | PARTIAL |
| MFX-004 | Note Length | PARTIAL — time or tempo-division length with gate and note-off trigger; render equals written length. Decay/key-scale options missing | PARTIAL |
| MFX-005 | Pitch | PARTIAL — ±48 semitones with input range blocking; release identity across parameter changes; render equals transposed note. Random/lowest-fold modes missing | PARTIAL |
| MFX-006 | Random | PARTIAL — chance, choices, interval, direction, random/alternate with seeded reproducible output | PARTIAL |
| MFX-007 | Scale | PARTIAL — 12 roots × 13 scales, nearest-degree mapping (ties down), transpose; brute-force verified. Custom maps/fold missing | PARTIAL |
| MFX-008 | Velocity | PARTIAL — clip/gate/fixed, input/output spans, drive curve, random; relative/release operation missing | PARTIAL |

### Native instruments


Stage: **Further implementation authorized**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| INS-001 | Analog | PARTIAL — Poly Synth: two band-limited oscillators, noise, multimode filter with envelope/key/velocity/LFO, dual ADSR, mono/legato/glide, unison; closed-form tests. Analog's dual filters/routing and parity missing | PARTIAL |
| INS-002 | Collision | PHYSICAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-003 | Drift | PARTIAL — covered by Poly Synth subset; Drift's specific oscillator shapes, drift and modulation matrix missing | PARTIAL |
| INS-004 | Drum Sampler | SAMPLER | NOT IMPLEMENTED / UNVERIFIED |
| INS-005 | Electric | PHYSICAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-006 | External Instrument | EXTERNAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-007 | Impulse | SAMPLER | NOT IMPLEMENTED / UNVERIFIED |
| INS-008 | Meld | SYNTH | NOT IMPLEMENTED / UNVERIFIED |
| INS-009 | Operator | PARTIAL — FM Synth: four operators, eleven algorithms, feedback, filter, velocity; Bessel sideband oracle. Operator's waveforms, LFO, pitch envelope and filter set missing | PARTIAL |
| INS-010 | Sampler | MULTISAMPLE | NOT IMPLEMENTED / UNVERIFIED |
| INS-011 | Simpler | PARTIAL — Sampler: one sample with Classic/One-Shot modes, root/transpose/detune, start/end region, loop, filter, ADSR, voices, velocity, pan and volume; exact whole-frame playback, Hermite closed-form accuracy, portable sample collection and real-time hand-off are tested. Slicing, warping, crossfade loops, modulation and reference comparison missing | PARTIAL |
| INS-012 | Tension | PHYSICAL | NOT IMPLEMENTED / UNVERIFIED |
| INS-013 | Wavetable | PARTIAL — two oscillators over four built-in band-limited morphing tables, filter, envelopes, LFO to position, unison; user tables and modulation matrix missing | PARTIAL |

### Rack infrastructure


Stage: **Further implementation authorized**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| RCK-001 | Instrument Rack | PARTIAL — parallel instrument chains with key/velocity/selector zones, macros and grouping; see MIX-009 and MIX-011 and the [rack contract](research/racks-contract.md) | PARTIAL |
| RCK-002 | Drum Rack | PARTIAL — Drum Rack with 128 pad chains, per-pad devices, choke groups and in-rack returns (see MIX-010 and the [rack contract](research/racks-contract.md)); the 16-pad Drum Sampler remains. Pad routing to project returns, chain extraction and reference comparison missing | PARTIAL |
| RCK-003 | Audio Effect Rack | PARTIAL — parallel effect chains with chain selector gating, macros, grouping and latency alignment; see MIX-009 and the [rack contract](research/racks-contract.md) | PARTIAL |
| RCK-004 | MIDI Effect Rack | PARTIAL — parallel MIDI effect chains with key/velocity/selector zones scaling velocity and non-note events passed through; see the [rack contract](research/racks-contract.md) | PARTIAL |

### Built-in Max-based reference devices


Stage: **Further implementation authorized**; Pack/legacy entries require version reconciliation first.

| ID | Reference target | Test profile (plus BASE) | Status |
| --- | --- | --- | --- |
| MXL-001 | DS Clang | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-002 | DS Clap | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-003 | DS Cymbal | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-004 | DS FM | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-005 | DS HH | DRUM | NOT IMPLEMENTED / UNVERIFIED |
| MXL-006 | DS Kick | PARTIAL — Drum Kit kick/snare/clap/hats/toms/cymbal pads with tune, decay, tone, level and hat choke; not individual DS devices | PARTIAL |
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
| MXL-021 | Velocity Shaper (MIDI Tool) | PARTIAL — the Velocity Shaper note tool ramps velocities from Start to End with a Curve across note starts, with hand-computed results (see MID-007 and the [note tool contract](research/midi-tools-contract.md)); the reference's drawn breakpoint envelope and exact algorithm missing | PARTIAL |
| MXL-022 | Euclidean (MIDI Tool) | PARTIAL — the Euclidean note generator (Steps, Pulses, Rotation, Pitch, Rate, Gate, Velocity) with exact pattern tests (see MID-008 and the [note tool contract](research/midi-tools-contract.md)); the reference's multiple voices and exact algorithm missing | PARTIAL |

### Additional content/Pack capability candidates


Stage: **Further implementation authorized**; Pack/legacy entries require version reconciliation first.

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

## How to use this ledger during continued development

Start from a complete song-making workflow and expand coverage while keeping the native app runnable. For each row, create the smallest meaningful reference fixture, then test its interactions with transport, save/reopen, undo, routing, automation, and rendering. Reuse general engine fixtures without pretending they verify every device's sound.

For a platform matrix, keep separate results for macOS Apple Silicon, any supported Intel macOS build, and each supported Windows architecture/backend. Native AU-only behavior should be explicitly platform-specific; shared VST/session behavior should use equivalent fixtures. “Windows build passed” and “Windows audio driver tested” are different evidence.

Device profiles need per-parameter cases before a device can be marked VERIFIED. Pack families need versioned device manifests before their scope can be called complete. Physical controllers and interface behaviors require real hardware evidence in addition to simulation.

The first preview was delivered and the owner explicitly authorized continued development. No preview approval gate remains; every new capability still needs its own implementation and verification evidence.

Audio clip gain/fades, non-destructive range, split and duplicate have original implementations and known-output tests; see the [audio-editing contract](research/audio-clip-editing-contract.md). These six additional PARTIAL ledger rows do not assert complete feature or numerical reference parity.

Shared top-level audio/MIDI/return track ordering is implemented through native undo, with retained track/clip/slot/device/send identities, shared Arrangement/Session controls, stable selection and full-buffer dry-plus-return tests. Nested groups and drag reordering are not implemented. This broadens the working preview without inventing another whole-product completion percentage.

Whole ordinary audio/MIDI tracks now duplicate with independent native device/clip/slot identities and new note identities, shared immutable media and retained send destinations. Generated VST3/native effects and synth renders, independent subsequent edits, portable reopen, one-step undo and generated input/Session ownership tests pass. Return/group/frozen duplication remains excluded. This does not change the partial-feature-area denominator.
