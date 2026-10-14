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

The independent combined audit verifies **36,590 unique outputs out of 36,599 declared ARM64 entries**, retaining seven failures and two timeouts, with none interrupted or unattempted. The first pass produced 36,582 outputs. A 120-second retry recovered one timeout. A subsequent isolated metadata correction recovered eight entries from the original unresolved selection, including that prior recovery; the combined gain is eight, not nine.

The metadata pass independently checked three exception/abort import bindings, two non-returning exception helpers and two exact direct tail branches before annotation. It used a distinct copied project and the fixture-validated instruction models described in [main residual recovery](residual-extraction-recovery.md). Combined metadata/tool changes support this pass's aggregate result; they do not isolate every individual cause. The installed scanner and its isolated slice retain their original hashes. No scanner target code was executed.

The verifier checked **36,591 completed files across the three overlapping runs**, totaling **87,777,589 bytes**; the duplicate successful address counts once in coverage. The seven retained failures comprise three preparation/decoding failures, three dynamic-variable hash errors and one memory-range error. Two entries still time out. The Intel scanner slice has been inventoried but has not had an equivalent extraction pass.

ARM64 input SHA-256: `489d53c5763e334c3d27b9a776b97c5404dd23545d25a4a997e4175df62fc3f7`. Combined verification SHA-256: `2940b9bd56c15cf71417b34394328fb6ee763e6ba6fdbf7f274c542c614c8b67`.

Halftrack already uses its own tested separate-process scanner integration. This analysis does not replace it with reconstructed reference code or establish compatibility with every vendor plug-in.

## Remaining work

The scanner has nine unresolved ARM64 entries and an unprocessed Intel slice. Other helpers, framework slices, the unsupported legacy candidate, scripts and embedded archives remain outside completed decompilation coverage. Main-program residual recovery is tracked separately in [the main research results](reverse-engineering-results.md). Full device graphs, modulation, filters, parameter mappings and paired reference renders remain necessary before any sound-equivalence claim. The existing Accessibility denial still prevents automated first-hand reference interaction; this static pass does not resolve it.
