# Parameter forwarding and one concrete value mapping

Reviewed 2026-10-20. On the established factory-created wrapper path, forwarding entry `0x19ff8` reaches `0x1853c`. A bounded follow-up also establishes how incoming parameter index **6** selects metadata and transforms a finite input into an integer control record. The named control, receiver behavior and audible result remain unverified.

## Scope and provenance

This uses the authorized Analog ARM64 slice, 432,640 bytes, preferred image base `0x0`, SHA-256 `91e72fa0e582db6540343251174951f425028f1d74e660b6ceb5ec8d068250f7`. The input hash is unchanged before and after inspection. Addresses require the image slide when interpreting a loaded instance. The [factory/initialization proof](rea-dispatch-trace.md) and [control-record schema](rea-event-timing.md) are reused explicitly.

The pinned [REA workflow and tool identities](rea-evaluation.md), one new REA 6.7.0 / Ghidra 12.1.4 dossier, Apple LLVM 17.0.0 decoding, direct Mach-O pointer reads and rebase records support the findings. No target code was executed or modified, and no Halftrack code or native test was changed or run. Proprietary instructions, pseudocode and complete dossiers remain in ignored evidence.

## Resolved forwarding edge

The factory stores the concrete wrapper at adapter `+8`; its constructor installs table `0x580c0`, as established previously. Entry `0x19ff8` loads that wrapper, returns if null, and selects wrapper slot `+0x38`. Pointer location `0x580f8` contains `0x1853c` and has a pointer-rebase record. The branch at `0x1a008` therefore reaches `0x1853c` on this path. The incoming integer index in `x1` and scalar value in `s0` are unchanged by these six instructions; the receiver pointer in `x0` is replaced by the wrapper. This does not establish the host-facing API's calling convention beyond the observed registers.

At `0x1853c`, the index addresses a 28-byte metadata entry after bounds and initialized-flag checks. The setter clamps according to the entry's type, caches the clamped value at entry `+0x18`, and calls wrapper slot `+0x90`. Pointer `0x58150` resolves that custom hook to `0x2b30`. A set low bit in its result prevents the ordinary fallback dispatch. Consequently, a mapping from index straight to a generic scalar receiver would omit real behavior.

## Parameter index 6

The following is limited to successful initialization on the traced path, with the registered metadata still in effect, and **finite input values**. It does not assign a product control name to index 6.

| Step | Address-bound evidence | Established behavior |
| --- | --- | --- |
| Register metadata | Initializer call `0x17ffc` selects wrapper slot `+0x88`; pointer `0x58148` contains `0x17d4`. At `0x1878`–`0x188c`, this routine passes index 6, a routing identity read from the inner-object view's field `+0x2168`, selector 2 and upper bound 2 to helper `0x17b40`. Helper stores are `0x17c14`–`0x17c34`. | Entry is initialized, with type 3, selector 2 and floating upper bound 2. Its routing identity comes from that field, not from the incoming index itself. Fresh metadata allocation zeroes entry `+9`; this is an ordinary, nonvirtual entry on this path. |
| Clamp and cache | Type-3 branch `0x1860c`–`0x18630`, then store `0x1864c`. | A finite input is clamped to `[0, 2]` and the clamped value is cached before the hook. This setter is not universally normalized to `[0, 1]`. |
| Select special handling | Hook instructions `0x2c10`–`0x2c2c` index a byte jump table. Byte `0x62` at `0x52ec6` selects `0x2c30 + 4 × 0x62 = 0x2db8`. | Index 6 reaches its specific comparison branch. The proof uses the actual table byte, not only an inferred decompiler switch. |
| Transform value | Comparisons/selections `0x2db8`–`0x2dd4`; helper call `0x2de0`. | Clamped values below 1 produce integer 1; values from 1 inclusive to 2 exclusive produce integer 2; value 2 produces integer 4. There is no float-to-integer rounding or truncation in this branch. |
| Route transformed value | Helper `0x187cc` checks entry validity/nonvirtual status, loads its routing identity and selector, then tail-calls inner slot `+0xd0` at `0x188c8`. Pointer `0x58338` selects `0xfed0`, which preserves these arguments around a notification and branches to `0x2cd98`. | The previously established integer helper constructs kind 1, selector 2 and integer payload 1, 2 or 4. It uses the metadata routing identity with its high-bit flag removed when present, then calls router `0x26148` directly. |
| Prevent fallback | Branch `0x2de4` reaches `0x2cdc`, which sets return value 1. Setter tests that low bit at `0x18668`. | This successful custom branch suppresses the generic type-3 dispatch. The cached value remains the clamped input, rather than being replaced with the emitted integer. |

For example, finite inputs `-1`, `0.5`, `1`, `1.5`, `2` and `3` produce control payloads `1`, `1`, `2`, `2`, `4` and `4` respectively under these conditions. These examples are deductions from the decoded comparisons, not runtime measurements. NaN/infinity behavior is outside this report.

The inner-object field's actual numeric routing identity and the destination's semantic name are unresolved here. The metadata builder obtains its inner-object view through the checked helper `0x2944`; this report does not assume that an arbitrary receiver has the same layout. It also does not prove every construction path, later metadata mutation, notification side effect, downstream smoothing, thread safety or sample at which a change becomes audible.

## Evidence and implementation consequence

New setter dossier: `ev_ae1af937c5d4e3f4f1e116d2dccdff188937dd20e2bb26ebe76a5cc025d25efa`, JSON SHA-256 `36420d26054f9651c1b8e6c8d4c284f13a73d1315fd621fd7a8d6f223a8c5d0c`. REA models body `0x1853c`–`0x187cb` and reports partial callee coverage with one unresolved computed call. The table/constructor evidence resolves that hook on the specified path; coverage does not automatically resolve computed tail branches. The older metadata-builder pseudocode also incorrectly marks instructions after `0x181c` unreachable, so the index-6 registration is established from LLVM instructions rather than that truncated inference.

The ignored manifest `artifacts/tools/rea/evidence/parameter-dispatch/manifest.json` has SHA-256 `e79e66d055f332baf43549c81de87700a5b8a62037bdb58ced958127a1a36176`. It binds 12 bounded LLVM ranges, five pointer locations with rebase rows, the exact switch byte, seven reused pseudocode hashes, the new dossier, prior evidence-manifest hashes and unchanged input hashes.

This narrows the previous unresolved forwarding edge and provides a specific parameter-transform contract for future paired measurements. Halftrack has not gained this unnamed control or a verified equivalent receiver from this investigation. No DSP implementation, sound-equivalence claim or feature-completion credit follows.
