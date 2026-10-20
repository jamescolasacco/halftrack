# Analog processing dispatch trace

Reviewed 2026-10-20. Two previously unresolved processing calls now have concrete static destinations **on the factory-created, successfully initialized object path**: adapter call `0x19df0` reaches wrapper `0x18d24`; wrapper call `0x18eac` reaches inner processing entry `0xe78c`. This resolves where that path goes. It does not recover the complete instrument or establish sound equivalence.

## Target and method

The authorized Analog ARM64 slice from the [component audit](additional-component-audit.md) is 432,640 bytes, image base `0x0`, SHA-256 `91e72fa0e582db6540343251174951f425028f1d74e660b6ceb5ec8d068250f7`. Its hash was unchanged before and after this investigation. All addresses below are preferred virtual addresses for that exact slice; runtime rebasing adds the image slide.

The pinned [REA workflow](rea-evaluation.md) was applied using existing constructor extraction, the retained adapter dossier, direct Mach-O segment/pointer reads, Apple LLVM 17.0.0 disassembly and rebase records, and one new REA 6.7.0 / Ghidra 12.1.4 function dossier. No target code was executed or modified. Raw instructions, pseudocode, binaries and complete dossiers remain in ignored local evidence. No Halftrack source, build or native test was changed or run by this investigation.

## Proven path

| Link | Address-bound evidence | Conclusion |
| --- | --- | --- |
| Create wrapper and adapter | Factory `0x390c` constructs the wrapper through `0x165c` → `0x14dc`, constructs the adapter through `0x197d8`, then stores the wrapper pointer at adapter offset `+8` at instruction `0x3944`. Wrapper constructor `0x14dc` installs table `0x580c0` at `0x150c`. | The factory ties this adapter to this concrete wrapper table. This is stronger than finding a plausible table near the code. |
| Adapter processing call | Instructions `0x19dbc`–`0x19df0` load adapter `+8`, its table, and table slot `+0x60`. The pointer at `0x58120` (`0x580c0 + 0x60`) is `0x18d24`. LLVM lists a pointer rebase at that exact location. | Call `0x19df0` reaches `0x18d24` for this wrapper. |
| Initialize the wrapper | The factory calls forwarding entry `0x1a0c0` at `0x396c`. Adapter constructor `0x197d8` installs table `0x595f8` at instruction `0x197f0`. Its slot `+0x10`, pointer location `0x59608`, contains `0x19f94` and has an LLVM pointer-rebase record. That entry forwards to wrapper slot `+0x10`; pointer `0x580d0` selects initializer `0x17d68`. | The creation path includes the initializer that populates the wrapper’s initially null inner-object field. |
| Construct the inner object | Initializer `0x17d68` calls wrapper slot `+0x70` at `0x17db0`; pointer `0x58130` selects factory `0x1784`. That factory allocates and calls thunk `0x4344` → constructor `0x3a14`. The constructor installs table `0x58268` at `0x3a38`. Its base constructor `0x2b8a8` preserves the object pointer. Initializer instruction `0x17db4` stores the factory result at wrapper `+8`. | This establishes the inner object’s table through actual construction and assignment. |
| Wrapper processing call | Instructions `0x18e88`–`0x18eac` load wrapper `+8`, check it, then load table slot `+0x20`. The pointer at `0x58288` (`0x58268 + 0x20`) is `0xe78c`, with an LLVM pointer-rebase record at that location. | Call `0x18eac` reaches `0xe78c` on the initialized path. |

The five selected pointer locations are in `__DATA_CONST`; their virtual addresses equal file offsets in this slice’s segment mapping. Direct little-endian pointer reads and LLVM’s rebase listing agree. Independent LLVM decoding also confirms the constructor stores, initialization store and indirect-call operands. Cached decompiler signatures alone were not used to establish pointer identity.

The scope remains conditional. The wrapper base constructor initializes its inner pointer to null, and teardown `0x18358` clears it. The reviewed initializer assigns the concrete factory result; reinitialization follows that same virtual factory slot while the wrapper retains its table. This bounded trace does **not** prove that every possible caller, alternate construction path or later aliasing write preserves these objects. It does not establish which runtime paths a particular session executes.

## REA corroboration and limits

The existing adapter dossier is `ev_42951606eb7172c4f9fabc1abb503e164c6ba56aa75a5ed464c3215dc44b5b7d`. New inner-entry dossier `ev_9299319957840f755eadcb3d0de6796ccc6b9286a6a02d66c58814784414baac` has JSON SHA-256 `d2f455817fddf7eac40bcf1472d685fd303953faec9f8b4d294f3c395994512b`. REA identifies the non-thunk processing body at `0xe78c`–`0xf8a3`; its entry instructions agree with independent LLVM decoding. The constructor/table proof above supplies the inbound virtual edge that the dossier does not resolve automatically.

REA reports partial callee coverage and 100 unresolved computed-call sites in this body. Its recovered signature and types remain provisional. Resolving the outer calls therefore exposes further graph dispatch, not a complete oscillator, filter, modulation or voice-allocation implementation. No extraction-count or feature-completion credit is assigned.

The ignored evidence manifest SHA-256 is `e0a4e0f7196d95ffb0a483dd0242298f45f77be24b292654e7aee1032db276d8`. It records selected LLVM ranges and hashes, pointer bytes/rebase rows, reused pseudocode hashes, the new dossier identity and unchanged input hashes.

## Next implementation question

The next bounded question is whether events and parameter changes enter the inner processing graph at their requested sample offset or at a processing boundary. Start with the event-like record consumption near `0xe818` and its direct forwarding helper `0x26148`, resolve the relevant object fields and record schema, then connect them to the wrapper’s timestamped records. Calling those records MIDI or parameter automation before establishing their schema would be an inference.

Once that contract is established, specify paired note-on/off and parameter-step renders at boundary-minus-one, boundary and boundary-plus-one under irregular host blocks, checking onset samples, release and continuity. The existing [Halftrack callback regression](synth-processing-boundaries.md) already tests its own scheduling contract. This trace alone justifies no new latency, block size, DSP formula or production behavior change.


The [subsequent schema investigation](rea-event-timing.md) establishes that this entry loop consumes control records, while the wrapper has a separate timestamped MIDI path. It also traces scalar setters directly to the same control router. The report preserves the unresolved queue producer, receiver semantics and host timing, so this is a refined investigation boundary rather than a completed instrument.
