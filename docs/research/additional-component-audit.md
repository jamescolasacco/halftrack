# Additional installed-component audit

The question is which native components sit outside the main application, how far their static extraction has progressed, and which findings can support working Halftrack code. This is an owner-authorized read-only study of the installed 12.2.1 bundle. No target code was executed by these tools. Raw binaries, pseudocode, internal names and private manifests remain in ignored artifacts; this document is an original factual summary.

## Bundle inventory

The header scan read 51,057 regular files, recording symlinks separately without following them. It found **941 native-binary occurrences representing 939 unique file hashes**, with zero file-read errors. The native count includes the main executable, seven separate audio-device binaries, helper processes, third-party libraries/frameworks and 878 files in the separate programmable environment. Header classification additionally distinguished 131 Java class candidates from universal-binary magic. Script extensions were counted; archives and scripts have not all been recursively analyzed.

One legacy mixed-architecture binary is retained as an unsupported candidate: the original inventory parser accepts little-endian 64-bit Mach-O slices, and does not claim coverage of its 32-bit slices. The inventory is therefore explicitly incomplete for that candidate. Component files are not a feature denominator, and third-party/framework code is not all unique product logic. Programmable devices, branded hardware and cloud implementation remain outside the agreed product scope.

Inventory manifest SHA-256: `4ad8af52a7807cc9192d49a8202a508ec42831ee284e6b4873ac097434dd0b29`.

## Seven separate audio devices

Every listed ARM64 and x86-64 slice was copied by verified universal-container bounds and SHA-256, imported into its own Ghidra 12.1.4 project, and processed without executing the binary. The independent verifier checks every completed output against its recorded hash and reconciles declarations and retry overlaps. Baseline attempts use a 20-second function timeout. Three timeout entries were retried for up to 120 seconds in verified copies of the stopped projects; all three recovered. Original and extracted input hashes were rechecked afterward.

| Component | ARM64 completed / declared | x86-64 completed / declared |
| --- | ---: | ---: |
| Amplifier | 1454 / 1454 | 1453 / 1453 |
| Analog | 991 / 991 | 987 / 987 |
| Cabinet | 1328 / 1328 | 1309 / 1309 |
| Collision | 824 / 824 | 820 / 820 |
| Corpus | 534 / 534 | 534 / 534 |
| Electric | 656 / 656 | 652 / 652 |
| Tension | 941 / 941 | 937 / 937 |

All **13,420** declared entries across the fourteen slices now have verified extraction output; none remain failed, timed out or unattempted in this device set. The chosen successful outputs total **58,749,683 bytes**. The compact index of per-component verifier hashes has SHA-256 `ce7231ef6da14e9926bc69724a89bfe4e420db411ae52cab971b52c75d774662`. The source binary, modeled instruction body and output each have distinct fingerprints in the retained evidence.

This result does **not** mean these devices have been reconstructed as compiling Halftrack implementations. Decompiler warnings, unresolved virtual calls, inferred signatures and absent runtime comparisons still require analysis. C-like output is not original source or a proof of behavioral equivalence.

## Instrument call mapping and working code

A separate bounded auto-analysis model of the Analog ARM64 slice resolved its outer constructor and virtual table. The processing export enters a buffer adapter, then an inner wrapper, then another virtual processing method. Parameter assignment likewise forwards through the adapter and wrapper. The mapped adapter table is at `0x595f8`, its processing method at `0x19a10`, and its parameter-forwarding method at `0x19ff8`; the inner table at `0x580c0` leads to `0x18d24` and `0x1853c`. These are address-based static findings for this exact slice, not measured audio algorithms. The wrapper’s inferred argument types remain incomplete. The original binary is unchanged.

Halftrack source `ea24869` now implements actual sine, white-noise and adjustable pulse synthesis with UI, validation, native undo, persistence and complete offline audio tests. Its [oscillator contract](synth-oscillator-contract.md) distinguishes publicly described waveform behavior from explicit Halftrack defaults. It does not claim translation of the complete reference device. All eight local Mac suites pass in 133.15 seconds, and [CI 38518528688](https://github.com/jamescolasacco/halftrack/actions/runs/38518528688) passes on macOS, Windows and Linux.

## Native scanner component

The independent combined audit verifies **36,596 unique outputs out of 36,599 declared ARM64 entries**, retaining three preparation failures and no timeouts, with none interrupted or unattempted. The first pass produced 36,582 outputs. A 120-second retry recovered one timeout. An isolated metadata correction subsequently recovered eight entries from the original unresolved selection, including that prior recovery. The fixture-validated vector conjunction model then recovered three dynamic-variable hash failures, bringing the combined gain to eleven. The latest continuation recovers three further entries: one memory-range failure and two timeouts, for a total gain of fourteen.

The earlier metadata pass independently checked three exception/abort import bindings, two non-returning exception helpers and two exact direct tail branches before annotation. The latest pass independently confirms that `0x10093197c` binds to `___stack_chk_fail`, checks the call at `0x1002bd558` (instruction `0x9419d109`), and corrects that import's non-returning flag in a distinct verified project copy. This recovers the memory-range case at `0x1002bd400`; two former timeouts (`0x1004cb858`, `0x100589b6c`) complete under the copied instruction model, larger response allowance, no syntax-tree return and 600/630-second decompiler/process bounds. Combined metadata/tool/option changes support the aggregate result, without isolating each individual cause. Original instructions are unchanged. No scanner target code was executed.

The verifier checked **36,597 completed files across five overlapping runs**, totaling **90,386,722 bytes**; the duplicate successful address counts once in coverage. Three preparation/decoding failures remain at `0x100004680`, `0x10003cb60` and `0x10007d000`. These map to `__TEXT,__text` and contain data-like bytes, but that is insufficient to exclude them automatically from the declared inventory. The Intel scanner slice has been inventoried but has not had an equivalent extraction pass.

ARM64 input SHA-256: `489d53c5763e334c3d27b9a776b97c5404dd23545d25a4a997e4175df62fc3f7`. Final combined verification SHA-256: `8737f73adbaefe1a4748fad02abeb24a2fb1d04b83a7c0a2101d2019224d7acb`. The earlier four-run report remains historical evidence with SHA-256 `e60b3743edf11de2d13ee2f85602c31f9b780804576683ce5d7435210c6a44fb`. The frozen source project and isolated input were rehashed unchanged after the stopped continuation.

Halftrack already uses its own tested separate-process scanner integration. This analysis does not replace it with reconstructed reference code or establish compatibility with every vendor plug-in.

## Remaining work

The scanner has three unresolved ARM64 preparation entries and an unprocessed Intel slice. Other helpers, framework slices, the unsupported legacy candidate, scripts and embedded archives remain outside completed decompilation coverage. Main-program residual recovery is tracked separately in [the main research results](reverse-engineering-results.md). Full device graphs, modulation, filters, parameter mappings and paired reference renders remain necessary before any sound-equivalence claim. The existing Accessibility denial still prevents automated first-hand reference interaction; this static pass does not resolve it.
