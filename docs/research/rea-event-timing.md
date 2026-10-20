# Control records and MIDI timing in the Analog processing path

Reviewed 2026-10-20. The record loop near `0xe818` is a **control-message drain before inner audio processing**, distinct from the wrapper’s timestamped MIDI path. It dispatches available nonzero-kind records without comparing a sample timestamp. Traced scalar parameter setters can bypass this queue and invoke the same router synchronously. These static findings do not establish when the host invokes those setters or when their effects become audible.

## Scope and provenance

This follows the [initialized factory-path dispatch proof](rea-dispatch-trace.md) for the same authorized ARM64 component: 432,640 bytes, image base `0x0`, SHA-256 `91e72fa0e582db6540343251174951f425028f1d74e660b6ceb5ec8d068250f7`. Preferred addresses below require the runtime image slide when interpreting a loaded image. Input hashes match before and after inspection.

The pinned [REA workflow](rea-evaluation.md), previously verified Ghidra 12.1.4 outputs and Apple LLVM 17.0.0 decoding were reused. One focused REA 6.7.0 router dossier was added. No target code was executed or modified; raw analysis remains ignored. No Halftrack production code or native test changed in this investigation.

## Which records reach the loop?

Wrapper constructor `0x17648` installs table `0x59460` in its embedded object at wrapper offset `+0x18` (store `0x17688`). Wrapper initialization supplies that object in register `x4` at `0x17f3c` before the inner initialization call `0x17f64`. Inner initializer `0x43f4` forwards unchanged arguments to `0x2b94c`; instruction `0x2b998` stores this object at inner offset `+0x38`. That is the field loaded at `0xe818`.

The embedded table’s read slot `+0x10`, pointer `0x59470`, selects `0x18fe8`; its write slot `+0x18`, pointer `0x59478`, selects `0x19044`. Direct pointer bytes, retained LLVM rebase records and constructor/call instructions agree. The reader and writer transfer **56-byte records** through ring indices. They copy payloads without inspecting a timestamp.

The loop calls the reader at `0xe828`, and repeats at `0xf7e0`. A nonzero kind word at record offset `+0` triggers `0x26148` at `0xf7f8`; kind zero is consumed without routing. The loop returns to `0xe830` when the reader reports empty. Subsequent processing notifications and the sample-processing loop follow. This establishes entry-time draining in FIFO order on this path, rather than selecting a record by a requested sample position.

## Partial control schema and direct parameter path

Wrapper parameter forwarding `0x1853c` selects inner slots `+0xc8`, `+0xd0` or `+0xd8` for the traced ordinary scalar cases. Their table pointers are `0x58330` → `0xfe78`, `0x58338` → `0xfed0`, and `0x58340` → `0x2cd44`. LLVM confirms that `0xfe78` and `0xfed0` tail-forward to `0x2ccf0` and `0x2cd98` after a separate virtual notification.

Those three scalar helpers construct this observed prefix and call `0x26148` directly:

| Record offset | Verified use | Limit |
| --- | --- | --- |
| `+0`, 32 bits | Kind: `1` for the integer-value path; `2` and `7` for the two float-value paths. | These are observed discriminants, not a complete command enumeration. |
| `+4`, 32 bits | Selector supplied from parameter metadata. | Its receiver-specific property meaning remains unmapped. |
| `+8`, 32 bits | Routing index. `0x26148` sign-extends it and indexes a table of 16-byte routing entries. | Selected setter paths remove the input index’s high-bit flag before constructing the record. |
| `+16`, 32 bits | Integer payload for kind `1`; floating-point payload for kinds `2` and `7`. | Scaling and smoothing depend on the selected parameter and downstream receiver. |

The remaining bytes, including offset `+12`, are not established by these scalar constructors; some stack fields are not initialized there. This is not a recovered public serialization format.

Router `0x26148` selects a destination and additional linked receivers using the routing index. Helper `0x29440` forwards recursively and ends in an indirect receiver call. Neither the drain nor this router checks a sample timestamp. The scalar setter paths reach the router **within the setter call**, without using the identified ring writer. That does not prove an audible sample changes immediately: host call scheduling, receiver behavior and smoothing remain separate questions.

## Distinction from MIDI and remaining uncertainty

The wrapper’s MIDI loop at `0x18d88`–`0x18dcc` reads a separate 16-byte input record containing a sample-offset word and three MIDI bytes. It stops when the offset is greater than or equal to the supplied inner block length, and passes accepted records through `0x1acf0` into the MIDI context. The [processing-boundary report](synth-processing-boundaries.md) documents the adapter’s separate offset adjustment and retention. The control routing index must not be mistaken for that sample offset.

A concrete producer using the 56-byte ring writer on this initialized product path was **not established**. The inspected scalar setters bypass it. Receiver-specific command semantics, smoothing, concurrency guarantees, runtime host scheduling and exact MIDI-to-audio onset remain unresolved. An empty queue or a possible alternate caller cannot be inferred from the reader alone.

No Halftrack timing defect or new DSP formula follows from this evidence. Before changing behavior, identify a specific parameter receiver and its host invocation timing, then compare parameter steps and note events around the known processing boundary. Halftrack’s existing callback-boundary tests remain valid tests of its own stated behavior; this trace does not turn them into reference-equivalence tests.

## Evidence identities

New router dossier: `ev_812e7a7da9eb527ad8a85e61415fefe6929277967b0b7010e44da0ddfb6ac61c`, JSON SHA-256 `1c91218eaf5b31e9271389c527c77f8183bb2bc6c2b10add8e495ece34f7a7e2`. REA and LLVM agree on the router’s signed-index load and direct calls to `0x29440`. REA reports complete immediate call-reference coverage for this small function; that does not resolve the downstream indirect receiver or the larger instrument graph.

The inner-processing dossier reused here is `ev_9299319957840f755eadcb3d0de6796ccc6b9286a6a02d66c58814784414baac`, JSON SHA-256 `d2f455817fddf7eac40bcf1472d685fd303953faec9f8b4d294f3c395994512b`. The ignored evidence manifest SHA-256 is `b161a572ec68c905321d743f4cd23127e19385ad7f916bd95ab73f8c8a90790b`; it binds selected LLVM ranges, pointer/rebase rows, verified cached pseudocode hashes, dossier identities and unchanged input hashes. This investigation adds no feature-completion claim.
