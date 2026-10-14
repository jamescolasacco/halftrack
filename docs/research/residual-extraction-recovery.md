# Residual extraction recovery, 2026-10-14

The independently verified main ARM64 inventory now has **392,910 completed outputs out of 392,926 declared entries**, with **seven failures and nine timeouts**. No entries remain unattempted or interrupted. This continuation recovered 64 entries beyond the earlier 392,846-output checkpoint. All 392,910 completed files are nonempty and match their recorded hashes; their total size is 871,680,755 bytes. These are extraction-integrity results, not a measure of implemented DAW features or recovered original source.

## Retained experiments

| Separate recovery pass | Selected entries | Newly completed | Failed | Timed out |
| --- | ---: | ---: | ---: | ---: |
| Explicit decompiler options and 120-second retry | 80 | 7 | 66 | 7 |
| One independently checked switch dispatcher | 1 | 1 | 0 | 0 |
| Verified tail branches and second switch table | 65 | 21 | 42 | 2 |
| Two vector-load instruction models | 37 | 27 | 10 | 0 |
| Third load form and two byte-interleave models | 10 | 8 | 2 | 0 |

Selections overlap; these rows are not independent function inventories. The first retry recovered two conditional-execution errors and five former timeouts, but it does not establish which option caused each recovery. Two independently checked switch tables and 169 exact direct-branch annotations changed only isolated analysis metadata. Grouped results establish the outcome of the combined corrections, not each annotation's individual effect. Two later 120-second retries remained timed out. An attempted unsupported 180-second setting was rejected before execution and is not reported as a run.

## Instruction-model validation

A diagnostic build of the distributed decompiler source logged the exact temporary that caused each hash failure without changing the failure condition. The first reproducible cases involved multiple byte loads in one ARM vector instruction. Renaming their scratch address alone did not recover the functions. Modeling each contiguous payload once, then selecting its bytes, resolved the reproduced failures. No decompiler error was suppressed or reclassified as success.

The initial fixtures also exposed omitted upper-register zeroing for two eight-byte vector-load forms. Original native instruction fixtures on this Mac confirmed the required zeroing. Corrections were confined to copied Ghidra language models: `LD2.16B`, `LD4.8B`, `LD2.8B`, `ZIP1.16B` and `ZIP2.16B`. The interleave models capture both inputs before one destination-register assignment, including when operands alias. Failed intermediate prototypes and logs remain retained; only fixture-passing versions were used for recorded recovery.

The final model passes **704 original emulator cases**: twelve load/addressing/register-wrap cases and ten interleave/alias cases, each with 32 input states. Tests compare all general and vector registers, program-counter advance, stack pointer and, for loads, source memory and base-address updates against independently calculated expectations. **128 original native processor checks** separately confirm the two short-load zeroing cases and both same-register interleave forms. No reference executable or extracted function was executed. These fixtures do not prove all possible instruction interactions, decompiler transformations or output semantics.

Two tool copies keep concurrent passes and model versions separate. Each modified language source, compiled instruction model, fixture, command, copied project and retry has a retained fingerprint. Original stopped projects were verified before copying and rechecked after their copies were used. The installed universal executable and isolated ARM64 input still match their original hashes. Raw binaries, inferred code and modified third-party tool distributions remain ignored local artifacts; none are linked into Halftrack.

| Evidence | SHA-256 |
| --- | --- |
| Final combined nine-run verification | `c1947778be9bb8003d8173924818210b868a19c411df06ae4d472f93090f88ba` |
| First fixture-passing compiled load model | `ab7373da5dc276a8f81d63b2cb28d1ede9171675ce2bfa502629171746e889f1` |
| Final compiled load/interleave model | `73228f1dcc0e996bd151417694560680270a50ca65333216b21462c97b5d0c1d` |
| Final 384-case load verification | `47db00b76ae145f81bb5789f119b1c089fc9d358c375e66ee5b1456ddbf2f4a4` |
| Final 320-case interleave verification | `2eb53977f5413ea8b8caa8d1c27358e1ae6a1f5945c62a34694cf3db17429f0b` |

## Remaining boundaries and actual application code

The seven unsuccessful entries are three preparation/decoding failures, two dynamic-variable hash failures, one memory-range error and one invalid-type-size error. Nine entries still exceed bounded decompilation time. Their original and later statuses remain explicit. Declared starts can include provisional or invalid boundaries; they are not automatically valid recoverable functions. Successfully emitted output can still have incorrect inferred types, unresolved calls and incomplete semantics.

Halftrack source `ea24869` separately adds working sine, deterministic noise and adjustable pulse synthesis, with native controls, validation, undo/redo, saved projects and offline audio tests. All eight local Mac suites pass, and [macOS, Windows and Linux CI](https://github.com/jamescolasacco/halftrack/actions/runs/38518528688) passes. Its [oscillator contract](synth-oscillator-contract.md) records independently implemented behavior and gaps. This extraction work has not converted the complete reference application or seven external instruments into compiling Halftrack code. [Component coverage](additional-component-audit.md), [feature gaps](../feature-parity.md) and runtime sound comparisons remain separate work.
