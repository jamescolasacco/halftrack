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

## Original implementation corrections resulting from research

Commit `c805766` gives new audio and MIDI tracks a normalized sinusoidal pan law: unity at center and approximately +3.0103 dB in the retained channel at an extreme. This corrects the previous approximately +6.02 dB behavior identified against the public specification. A distinct serialized mixer type preserves old project sound; old tracks are not silently migrated. Mute and solo now create engine undo actions.

The local Release build and full acceptance suite passed in **48.01 seconds**. Thirty-five complete-window stereo matrix renders cover 21 pan positions, fader headroom, exact silent cases, mute/solo undo/redo and project reopening. Maximum observed PCM error against the independent formula was **1.28e-7**. Quiet margins are included, but active clip-edge transients and paired reference renders remain separate unfinished checks. The source callback guard passed nine self-tests and seventeen owned bodies; inherited engine locking remains outside its guarantee.

A separate Linux link correction (`a93b813`) adds the runtime required by the engine's wider atomic operations. Previous combined-preview CI passed on macOS and Windows and failed on Linux before that fix. That correction is confirmed by subsequent Windows/Linux CI at ac20d09. The same run exposed recording loss on macOS: a background MIDI refresh cleared an active take. A deterministic paced synthetic-input regression reproduced it. Commit 5f40131 defers MIDI discovery until recording stops and passes the full local suite in 53.60 seconds. Final CI at 1294f5b passes macOS, Windows and Linux, including the recording regression and packaging. That revision also fixes Windows line-ending conversion in the dependency patch writer. Full results and remaining hardware/UI gaps are recorded in [progress](../progress.md).

## Next evidence to obtain

1. Retain the selected-function manifest and investigate unresolved types/calls only for a concrete behavior question.
2. Build a paired deterministic reference-output corpus; distinguish exact equality from measured tolerance and nondeterministic cases.
3. Add nonzero-latency/event-context plug-in fixtures and exact recording-placement tests.
4. Validate native UI gestures and physical device behavior independently on each supported platform.

Whole-product identity, complete device reconstruction, every plug-in format and physical hardware parity remain unachieved. The reports identify a testable route forward and explicitly preserve unknowns.
