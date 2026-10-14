# Residual extraction recovery, 2026-10-14

The independently verified main ARM64 inventory now has **392,921 completed outputs out of 392,926 declared entries**, with **three preparation failures and two timeouts**. No entries remain unattempted or interrupted. The retained continuations recovered 75 entries beyond the earlier 392,846-output checkpoint, including one in the latest pass. All 392,921 completed files are nonempty and match their recorded hashes; their total size is 873,850,023 bytes. These are extraction-integrity results, not a measure of implemented DAW features or recovered original source.

## Retained experiments

| Separate recovery pass | Selected entries | Newly completed | Failed | Timed out |
| --- | ---: | ---: | ---: | ---: |
| Explicit decompiler options and 120-second retry | 80 | 7 | 66 | 7 |
| One independently checked switch dispatcher | 1 | 1 | 0 | 0 |
| Verified tail branches and second switch table | 65 | 21 | 42 | 2 |
| Two vector-load instruction models | 37 | 27 | 10 | 0 |
| Third load form and two byte-interleave models | 10 | 8 | 2 | 0 |
| Whole-register vector conjunction model | 2 | 2 | 0 | 0 |
| Remaining non-preparation entries with the new model | 11 | 4 | 4 | 3 |
| Larger response allowance, no syntax-tree return | 2 | 1 | 0 | 1 |
| Disable one invalid optional rewrite | 1 | 1 | 0 | 0 |
| Second oversized-output follow-up | 1 | 1 | 0 | 0 |
| Final isolated 300-second attempts | 2 | 1 | 0 | 1 |
| Isolated 600-second attempts | 2 | 0 | 0 | 2 |
| Independently verified stack-failure import metadata | 1 | 1 | 0 | 0 |
| 600-second comparison after metadata correction | 2 | 0 | 0 | 2 |

Selections overlap; these rows are not independent function inventories. The first retry recovered two conditional-execution errors and five former timeouts, but it does not establish which option caused each recovery. Two independently checked switch tables and 169 exact direct-branch annotations changed only isolated analysis metadata. Grouped results establish the outcome of the combined corrections, not each annotation's individual effect. Two later 120-second retries remained timed out. An earlier unsupported 180-second setting was rejected before execution and is not reported as a run. The earlier explicitly fingerprinted local runner supports bounded 300-second decompilation and a 330-second process deadline. The latest owned runner/exporter copies extend those bounds to 600/630 seconds for the two retained timeout entries; fingerprints distinguish every run. One malformed selection was rejected before execution, then corrected in a separate retained selection. Earlier results retain their original limits.

## Instruction-model validation

A diagnostic build of the distributed decompiler source logged the exact temporary that caused each hash failure without changing the failure condition. The first reproducible cases involved multiple byte loads in one ARM vector instruction. Renaming their scratch address alone did not recover the functions. Modeling each contiguous payload once, then selecting its bytes, resolved the reproduced failures. No decompiler error was suppressed or reclassified as success.

The initial fixtures also exposed omitted upper-register zeroing for two eight-byte vector-load forms. Original native instruction fixtures on this Mac confirmed the required zeroing. Corrections were confined to copied Ghidra language models: `LD2.16B`, `LD4.8B`, `LD2.8B`, `ZIP1.16B`, `ZIP2.16B` and later `AND.16B`. The interleave models capture both inputs before one destination-register assignment, including when operands alias. Failed intermediate prototypes and logs remain retained; only fixture-passing versions were used for recorded recovery.

The preceding model passed **704 original emulator cases**: twelve load/addressing/register-wrap cases and ten interleave/alias cases, each with 32 input states. Tests compare all general and vector registers, program-counter advance, stack pointer and, for loads, source memory and base-address updates against independently calculated expectations. **128 original native processor checks** separately confirm the two short-load zeroing cases and both same-register interleave forms. No reference executable or extracted function was executed. These fixtures do not prove all possible instruction interactions, decompiler transformations or output semantics.

The latest model expresses vector conjunction as one whole-register operation instead of byte-lane temporaries. It passes **320 additional emulator cases** covering two widths and five operand-alias patterns, and the previous 704 cases pass again against that same compiled model: **1,024 emulator cases in total**. A separate **320 native processor cases** verify conjunction against an independent bytewise oracle, supplementing the earlier 128 native checks. Only original generated instructions execute in these fixtures.

Distinct tool copies keep concurrent passes and model versions separate. Each modified language source, compiled instruction model, fixture, command, copied project and retry has a retained fingerprint. Original stopped projects were verified before copying and rechecked after their copies were used. The installed universal executable and isolated ARM64 input still match their original hashes. Raw binaries, inferred code and modified third-party tool distributions remain ignored local artifacts; none are linked into Halftrack.

| Evidence | SHA-256 |
| --- | --- |
| Earlier combined nine-run verification | `c1947778be9bb8003d8173924818210b868a19c411df06ae4d472f93090f88ba` |
| Earlier combined sixteen-run verification | `18e17c02d5333ab6f5620cbc0e2c759531319b95466935964f60eecfd45718ec` |
| First fixture-passing compiled load model | `ab7373da5dc276a8f81d63b2cb28d1ede9171675ce2bfa502629171746e889f1` |
| Earlier compiled load/interleave model | `73228f1dcc0e996bd151417694560680270a50ca65333216b21462c97b5d0c1d` |
| Earlier 384-case load verification | `47db00b76ae145f81bb5789f119b1c089fc9d358c375e66ee5b1456ddbf2f4a4` |
| Earlier 320-case interleave verification | `2eb53977f5413ea8b8caa8d1c27358e1ae6a1f5945c62a34694cf3db17429f0b` |
| Latest compiled load/interleave/conjunction model | `e01805890af9ec7efa244a62d83c79571ee20497b447386e75cb4d6b50e770dd` |
| Latest 384-case load regression | `8f346b93995e3f2c699128d7fcc9c4ccc032eee08b41322e75f3b8a332f1c395` |
| Latest 320-case interleave regression | `e9bdadecacca475c48fada10c379596953d32c974d4f8fbfa75f70d784c2fbae` |
| 320-case conjunction emulator verification | `21448cd9bc1afd797144a7ba2c7c5d9bd4a7ad12402e159fccb8092cec3066ac` |
| 320-case native conjunction verification | `bdb2f3d83e0f977c120c81fa608010c8bbe78b0c48a5eed58910aea35be705f5` |

## Output limits and invalid rewrite diagnosis

Two large routines failed the decompiler's response-size limit. A separate owned exporter copy raises that allowance to 256 MiB and disables return of the unused syntax tree while retaining C output. Both routines then complete with indexed, hash-verified output. The exact exporter, runner, model and command fingerprints remain attached to these runs. This changes output configuration, not target instructions or their interpretation.

The invalid-type failure was reproduced with a diagnostic build of the distributed decompiler source. The retained stack trace locates a negative two-byte type created by the optional `concatleftshift` rewrite. A separate retry disables only that rewrite through the decompiler's rule option, preserving the underlying shift operations; it produces a verified output. The diagnostic failure was not suppressed or relabeled as success. This workaround avoids the observed invalid rewrite without proving every remaining transformation correct.

The final bounded retries recover `0x10456ca58`; `0x10467ff64` still exceeds 300 seconds. The independent aggregate audit rechecks all sixteen runs, including earlier unsuccessful attempts. The frozen source project's nine files and the installed/universal and isolated ARM64 input hashes remain unchanged.

## Non-returning import correction

The memory-range failure in `0x10117055c` followed a call to the stack-failure handler. Apple LLVM's independent indirect-binding table identifies `0x10501e7b4` as `___stack_chk_fail`; the exact call at `0x1011706b4` has instruction word `0x94fab840`. The copied analysis project incorrectly allowed that imported function to return, admitting an impossible fallthrough into exception cleanup. A narrow metadata-only script validates the input hash, import identity, instruction word and destination before setting the non-returning flag. The 444-byte target body is unchanged; the recovered output is 1,797 bytes. No decompiler failure was suppressed.

Two independently copied projects then repeat the remaining timeouts with the same metadata correction and 600-second decompiler / 630-second process limits. Both still time out, matching the preceding 600-second attempts without this correction. All jobs terminate with indexed outcomes. These comparisons do not claim the unresolved routines have been reconstructed.

The final 21-run aggregate verification has SHA-256 `60c2e5b9e80be28e919c6c5fb4271701156ff167990583277b09b5a2a170d15c`. A separate final integrity check verifies twenty frozen source-project files across main/scanner and the three checked executable inputs remain unchanged; report SHA-256 `9a86ccd3827210801a7d46f08605171b7fa8cfae66808466d690fab3b8431bc9`. Both reports and the exact metadata script/commands remain in ignored local artifacts.

## Remaining boundaries and actual application code

The three failed entries are preparation/decoding failures (`0x100005680`, `0x10003de00`, `0x10007e000`). Two entries (`0x100f9ebac`, `0x10467ff64`) still exceed the 600-second bound. The preparation entries map to `__TEXT,__text` but contain data-like bytes and remain unresolved; they were not forcibly cleared and decoded merely to inflate coverage. Their original and later statuses remain explicit. Declared starts can include provisional or invalid boundaries; they are not automatically valid recoverable functions. Successfully emitted output can still have incorrect inferred types, unresolved calls and incomplete semantics.

Halftrack source `ea24869` separately adds working sine, deterministic noise and adjustable pulse synthesis, with native controls, validation, undo/redo, saved projects and offline audio tests. All eight local Mac suites pass, and [macOS, Windows and Linux CI](https://github.com/jamescolasacco/halftrack/actions/runs/38518528688) passes. Its [oscillator contract](synth-oscillator-contract.md) records independently implemented behavior and gaps.

The later `8176007` and `90c9eb2` commits add a working insertable channel Utility: native editing, chain order, full project persistence, undo/redo and independently implemented DSP. Reference registration and arithmetic traces inform its bass/DC topology and normalized balance law, documented in [the Utility contract](channel-utility-contract.md). All nine local Mac suites and [macOS/Windows/Linux CI](https://github.com/jamescolasacco/halftrack/actions/runs/38532090898) pass; the freshly extracted Mac package passes its Utility suite. Exact sample parity remains unmeasured. This extraction work has not converted the complete reference application or seven external instruments into compiling Halftrack code. [Component coverage](additional-component-audit.md), [feature gaps](../feature-parity.md) and runtime sound comparisons remain separate work.

Source `6467066` separately adds the four original [native effects](native-effects-contract.md), eight atomic MIDI transformations and project-tempo delay sync. All eleven local Mac suites pass, and the freshly extracted source-inclusive package passes its effects and note-tools tests. These standard DSP and workflow implementations are not mechanically translated versions of the recovered routines. Full component reconstruction, feature coverage and paired reference comparisons remain unfinished.
