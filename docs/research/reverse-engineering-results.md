# Local reverse-engineering results

Recorded 2026-10-13. This is a reproducible starting point for binary-assisted research, not a reconstructed application or an exact-clone claim. Tool and skill selection is explained in the [toolchain report](reverse-engineering-toolchain.md), [vetted skill report](reverse-engineering-skills.md), and [behavioral measurement report](behavioral-parity-measurement.md). The [local runbook](reverse-engineering-runbook.md) records reproducible commands and interpretation checks.

## Reference identity and independent inventory

Installed reference: Ableton Live 12 Suite, version **12.2.1**, build **2025-06-05_c0d59beb04**. Main executable size: **300,162,224 bytes**. SHA-256 before and after analysis: `b16188a466a26e6b9174a8819411d47c1255fd122242258a88a23dba2f096f2d`. The installed executable was not modified.

| Property | Intel slice | Apple Silicon slice |
| --- | --- | --- |
| Architecture | x86_64 | arm64 |
| Slice size | 157,871,632 bytes | 142,269,616 bytes |
| UUID | aaadf4a6-765d-3dc5-9856-6f8f1fa8c36a | 5f0b6ae3-d051-3e0f-944b-558922acdc63 |
| Symbol-table entries | 78,250 | 48,364 |
| Defined / undefined / STAB entries | 76,383 / 1,866 / 1 | 46,643 / 1,720 / 1 |
| Declared function-start entries | 392,854 | 392,926 |
| Embedded DWARF sections found | None | None |

These categories are different. A symbol can identify data, a template wrapper, a thunk or an import; it is not automatically a recovered function. External dSYM files were not investigated. Apple LLVM reports 35,870 defined text symbols in the ARM64 slice. Our original bounds-checked Mach-O parser and Apple's LLVM parser produced exactly the same ordered **392,926** ARM64 function-start addresses.

The isolated ARM64 copy has SHA-256 `b37cb738442bba7787457fe751b0e3f9fbdabc662d8d491d1d9aa46b120bd3fc`. Analysis below concerns that slice only. No Windows executable was analyzed, and no cross-platform behavioral equivalence follows from these results.

## Verified local toolchain

- **Ghidra 12.1.4:** official release archive SHA-256 matched `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`. Its macOS ARM64 native components built successfully from the distributed source with Xcode tools and the supplied Gradle 9.7.1 wrapper. [Official release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build).
- **OpenJDK 21.0.8**, matching this released Ghidra version's requirement.
- **Apple LLVM 17.0.0:** independent object/symbol inventory. The tool distribution's version is distinct from upstream LLVM's current release.
- **Reviewed skill:** Trail of Bits curated `ghidra-headless`, pinned at `6d05be4889017b06fb15069f371afd220daffb62`. Its guidance informed the local workflow; its truncating exporters were not used for coverage claims. No global skill or MCP service was installed.

The reference imported into a persistent local Ghidra project with broad automatic analysis disabled, a 4 GiB Java heap limit and two analysis CPU slots. Ghidra selected `AARCH64:LE:64:AppleSilicon` with the default compiler specification. Its loader's `objc` compiler identification is not evidence of the original application's complete source language or compiler configuration. No dependent libraries were imported.

## Focused decompilation actually completed

Original `scripts/ghidra/ExportSelectedFunctions.java` accepts explicit entry addresses and a new output directory. It records per-entry status, timeout/cancellation, body address ranges, body/output hashes, tool/language identifiers and requested/completed counts. Preparation and decompilation each have a 30-second timeout. Broad auto-analysis remains disabled. A process exit alone is insufficient: inspect the manifest.

Six selected audio-utility processing entries produced nonempty pseudocode with no reported decompiler failure or timeout. One direct call target was then checked against the independently inventoried function-start table and decompiled in a second pass. **Total: seven requested entries, seven successful tool outputs, zero reported failures/timeouts.** This is a small targeted analysis, not decompilation of all 392,926 declared starts or validation of every inferred function boundary.

The outputs still contain inferred/undefined types and unresolved behavior. A subsequent bounded ARM64 disassembly review found a two-channel floating-point return that the pseudocode represented incorrectly as a scalar return plus an incoming argument. An integer mode selector also appeared with misleading float-like labels. The direct callee's reported 316-byte Ghidra body excludes additional jump-table blocks visible in disassembly and pseudocode: the manifest hashes its listed ranges, not a proven complete function. These findings make instruction-level cross-checking and controlled runtime tests necessary before adopting a behavior hypothesis. The bounded review also confirmed input selection/reordering and persistent processing state. Frequency-selective stereo behavior and smoothing are hypotheses; control mappings, coefficient generation, path selection and audible equivalence remain unproved. Detailed address-level notes and bounded disassembly remain local. Producing pseudocode does not recover the original project, establish the meaning of every argument, or validate numerical output. No extracted implementation was copied into Halftrack. The mixer correction below came from the public audio specification and independently written mathematical tests.

Local evidence, deliberately excluded from Git:

- `artifacts/reverse-engineering/reference-12.2.1/`: full metadata inventory, independent LLVM inventory, slice copy, analysis logs, and the `selected-utility-pass1` / `selected-utility-pass2` manifests and pseudocode.
- `artifacts/reverse-engineering/ghidra-projects/reference_12_2_1_arm64`: persistent analysis project.
- `.deps/reverse-tools/`: verified tool distribution and local build/cache.

## Inventory-wide sweep in progress

The continued task adds an original durable coordinator (`scripts/bulk_decompile.py`) and bounded Ghidra exporter (`scripts/ghidra/BulkDecompileBatch.java`). Commits `39a6bf5`, `5317560` and `25f0e53` add coverage bookkeeping, overlap recovery and original synthetic regression fixtures. The optional native suite passed eight tests in 18.512 seconds, including a real interruption/resume, a predecoded entry and a shared-tail arithmetic fixture. Pure test discovery is now part of all three CI platforms; Ghidra-dependent integration remains opt-in on macOS.

The initial conservative preparation pass is retained separately: 3,271 completed outputs, 362 preparation failures and one interrupted entry. Most sampled failures were declared starts inside a provisional containing function. Letting Ghidra reconcile those entries recovered all 21 such failures in the corrected 128-entry pilot. Its remaining entry did not decode as ARM64 instructions in either Ghidra or independent LLVM inspection; it stays an explicit failure. A declared function-start table is not proof that every listed location is a valid semantic function.

The corrected full ARM64 sweep (`bulk-pass2`) is running in batches of 1,024, with a 4 GiB Java heap, two analysis CPUs, 10/20-second preparation/decompilation limits and a 600-second batch ceiling. Broad automatic analysis remains disabled. At **2026-10-14 02:14:37 UTC**, an independent read-only database snapshot contained **266,335 completed outputs, thirteen failures, twenty timeouts and 126,558 unattempted entries**. All 266,335 completed files were present, nonempty and matched their recorded SHA-256 (460,872,719 bytes total). The read transaction was closed before hashing the frozen file list. The active batch was excluded from that snapshot. These counts are a dated checkpoint, not a whole-program completion claim; current local progress is in the append-only sweep log and coverage database.

The private `coverage-check-*.json` records retain that independent verification. Pass folders, request hashes and per-address journals distinguish failures, timeouts, interruptions and entries never attempted. No output count establishes recovered source semantics, full function boundaries, a completed device or a matching application. The [runbook](reverse-engineering-runbook.md#resumable-inventory-sweep) documents continuation and evidence checks.

## Focused crossfader follow-up

Read-only triage of completed outputs on 2026-10-14 identified a crossfader candidate family with seven surviving helper bindings. Twenty retained outputs matched their recorded hashes; eight bounded LLVM/symbol artifacts provide an independent check of the caller/stub links. The table initializer at `0x10193c13c`, selector at `0x10193de70`, and event wrapper at `0x10193de90` connect to the shared update path at `0x10193cd14`. Independent instructions support adjacent-table-entry interpolation; downstream dispatch at `0x101952fe4` remains an unresolved runtime callback.

This narrows a future measurement question; it does not recover the audible processor. UI ordering, parameter units, callback target, table contents and transition timing remain unmeasured. Tail-branch wrappers can include a larger shared body in pseudocode despite a much smaller modeled function body, so output counts and body hashes must not be mistaken for complete semantic coverage. No proprietary implementation was added to Halftrack.

The private evidence manifest hash is `bb85cd1c24061bcf4296d871a81cea7bbf143639a565acb966426b0e3eb4fa03`. The proposed next comparison uses original isolated left/right A/B tones, an unassigned baseline, each visible curve/position, between-knot probes, and position/assignment/curve transitions. Reference self-repeatability must be measured first. The recorded Accessibility denial still prevents claiming an automated reference run. An original fixture pack is now prepared: separate 997 Hz left-only and 1231 Hz right-only sources, each ten seconds of stereo PCM24 at 48 kHz with peak 0.0625. A separate formula oracle and CoreAudio decoding verified all 1,920,000 sample values with zero error; the inactive channels and DC sums are exactly zero. The private manifest hash is `7b8a34326f96a3e4694c9b65ef2ab5209fa69d65e531ed1bf00c681aacc339ef`. Every reference configuration/capture remains NOT RUN.

A second private static follow-up independently checked twenty-six retained outputs and fourteen bounded instruction ranges (5,660 requested bytes), plus 216 metadata bytes. It supports seven finite-input helper shapes, 131-entry lookup tables, fused interpolation and a distinct volume-conversion order for one curve. Outgoing-port metadata identifies the previously unresolved callback object; its constructor installs a verified no-op callback, so the runtime audio destination remains unresolved. Table values used by volume conversion, visible parameter mapping, time units and audible output still require measurement. The private report SHA-256 is `1323eafdae925c5446ce280789956a41b36e7e5c0cbfe3969f98db9302a5d1f9`. This adds factual static evidence, not a copied implementation or sound-equivalence claim.

A third bounded follow-up resolved the selected volume-conversion table's construction from seven scale objects, including its 101-entry amplitude table and endpoint handling. Independent instructions also establish a saved-byte restoration method for the bypass guard; its actual invocation and runtime scope remain unknown. Sixteen retained outputs and 3,144 requested instruction bytes were checked, together with 239 file-backed metadata bytes and nine bytes of zero-fill storage. Context conversion is supported as multiplication through two fields, but the fields' units remain unidentified. Private report SHA-256: `1235e419e64547a1c8400c93df3801cb0f826e2055fea77a2167755c93a589b8`. These findings refine the future measurement plan without claiming a measured control law, a timing unit or audible equivalence. No extracted code or table values were added to Halftrack.

## Original implementation corrections resulting from research

Commit `c805766` gives new audio and MIDI tracks a normalized sinusoidal pan law: unity at center and approximately +3.0103 dB in the retained channel at an extreme. This corrects the previous approximately +6.02 dB behavior identified against the public specification. A distinct serialized mixer type preserves old project sound; old tracks are not silently migrated. Mute and solo now create engine undo actions.

The local Release build and full acceptance suite passed in **48.01 seconds**. Thirty-five complete-window stereo matrix renders cover 21 pan positions, fader headroom, exact silent cases, mute/solo undo/redo and project reopening. Maximum observed PCM error against the independent formula was **1.28e-7**. Quiet margins are included, but active clip-edge transients and paired reference renders remain separate unfinished checks. The source callback guard passed nine self-tests and seventeen owned bodies; inherited engine locking remains outside its guarantee.

A separate Linux link correction (`a93b813`) adds the runtime required by the engine's wider atomic operations. Previous combined-preview CI passed on macOS and Windows and failed on Linux before that fix. That correction is confirmed by subsequent Windows/Linux CI at ac20d09. The same run exposed recording loss on macOS: a background MIDI refresh cleared an active take. A deterministic paced synthetic-input regression reproduced it. Commit 5f40131 defers MIDI discovery until recording stops and passes the full local suite in 53.60 seconds. Final CI at 1294f5b passes macOS, Windows and Linux, including the recording regression and packaging. That revision also fixes Windows line-ending conversion in the dependency patch writer. Full results and remaining hardware/UI gaps are recorded in [progress](../progress.md).

## Next evidence to obtain

1. Retain the selected-function manifest and investigate unresolved types/calls only for a concrete behavior question.
2. Build a paired deterministic reference-output corpus; distinguish exact equality from measured tolerance and nondeterministic cases.
3. Extend the now-passing nonzero-latency export oracle to event context and exact recording placement; complete-window export evidence is in [the regression report](export-latency-regression.md).
4. Validate native UI gestures and physical device behavior independently on each supported platform.

Whole-product identity, complete device reconstruction, every plug-in format and physical hardware parity remain unachieved. The reports identify a testable route forward and explicitly preserve unknowns.

## Reference interaction limitation

The installed reference was reopened during continued work, but macOS denied automated window access from the terminal (Accessibility error -25211). No reference project was edited or microphone captured. Automated firsthand comparisons therefore remain blocked in this environment; no alternate control path was used to bypass that denial. Offline implementation and original-fixture testing continue independently.

### Bounded failure triage, 2026-10-14

A read-only snapshot separately examined thirteen failures and twelve twenty-second decompiler timeouts. Thirty-seven evidence files and all twenty-five bounded byte windows passed hash checks. Independent instruction decoding supports embedded data at two preparation-failure entries; a third remains unresolved. The executable section and empty data-in-code table cannot settle those boundaries. Three small exception-path wrappers have provisional bodies extending into adjacent declared starts, making verified nonreturn metadata and final boundaries the strongest retry lead. The largest historical timeout body includes 120 other declared starts and may have been refined by later preparation. Any retry must use an isolated project after the active sweep releases it, preserve original statuses, and distinguish tool recovery from recovered semantics. The private report is retained under `failure-triage-20261014`; its SHA-256 is `8d6c8ff0d6a9f59f0fa6c48dec5bb0ceec63dd0da7ddc31117c5c7ad55040024`. No extracted implementation enters the application.
