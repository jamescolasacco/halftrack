# REA integration and bounded native-analysis evidence

Evaluation date: 2026-10-20. The user requested [REA](https://github.com/morluto/rea) for the ongoing Halftrack work. It is now installed as an isolated local analysis tool, has decompiled an original fixture that was compiled and checked, and has inspected two known entries in the authorized instrument-component copy. This is executable tool integration and static evidence, not a completed device reconstruction or measured sound equivalence.

## Pinned tooling and installation effects

The installed package is **rea-agents 6.7.0**, corresponding to official release commit `27dc42d10249f0410904a75c0997842eb03b931c`. The initially reviewed main snapshot was `d148ce090512aceeddd8c907f03f9dd5d7cf47c8`; it is not substituted for the package's release identity. The installed launcher and both inspected Ghidra bridge files match the release source byte-for-byte. REA's own source is MIT; that license does not relicense analyzed binaries or third-party tools. [Official release](https://github.com/morluto/rea/releases/tag/rea-agents-6.7.0), [release package manifest](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/package.json), [license](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/LICENSE).

The official npm archive passed its published SHA-512 integrity check; its independently recorded SHA-256 is `20958a66dba1a011e50a486beb9463a343128ed368c984f94065362c13f51f90`. Dependencies were installed beneath ignored `artifacts/tools/rea` using `--ignore-scripts --omit=dev --omit=optional`. This excludes optional Frida and avoids lifecycle execution. The source installer was reviewed but not run: it performs a global npm installation and can start interactive agent setup. Setup changes agent registrations and installs workflow instructions; none of those global changes were made. [Installer source](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/install.sh), [setup guide](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/docs/installation.md).

The system's Node 24.5.0 does not meet this package's declared minimum. An isolated official **Node 24.15.0** ARM64 archive was verified against Node's published SHA-256 (`372331b969779ab5d15b949884fc6eaf88d5afe87bde8ba881d6400b9100ffc4`). Existing **Ghidra 12.1.4** and **OpenJDK 21.0.8** were reused. REA's scoped provider diagnostic reports these prerequisites healthy. Its separate informational checks report absent agent registrations and other engines; they are not requirements for this CLI route and were not repaired. Acquisition used GitHub, npm and Node's official download service; this was not a network-traffic audit. [Node checksum list](https://nodejs.org/dist/v24.15.0/SHASUMS256.txt).

## Workflow used

The release's `reverse-engineer-anything` skill was read and applied: bind a precise target, inspect a bounded question, retain evidence identity and unknowns, and corroborate findings before implementation. The [workflow skill](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/.agents/skills/reverse-engineer-anything/SKILL.md) and [native workflow reference](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/.agents/skills/reverse-engineer-anything/references/native-and-artifacts.md) are the actual versions used; installing instructions alone would not connect tools to an active agent.

An ignored local launcher supports repeatable CLI use:

```sh
python3 artifacts/tools/rea/rea.py function /absolute/path/to/authorized-input \
  --procedure=0xADDRESS --provider ghidra --json
```

It selects the pinned Node/package and existing provider paths without changing the system runtime, agent configuration or Halftrack build. Full JSON, original fixtures, LLVM comparison output, snapshots and checksum manifests remain ignored. No reference binary was executed, modified, embedded in Halftrack, or published.

REA's Ghidra route imports into an ephemeral private project; it does not open the existing analysis database. It waits for default analysis and removes the temporary project on close. CLI snapshots retain successful observations but a new uncached query may still start a provider. This makes narrow components practical here; it is a reason not to repeat the much larger main-program sweep. [Native lifecycle and snapshots](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/docs/native-investigation.md), [CLI contract](https://github.com/morluto/rea/blob/27dc42d10249f0410904a75c0997842eb03b931c/docs/cli.md).

## Original fixture validation

A newly written ARM64 C fixture contains an integer clamp and a caller that invokes it twice. Its binary SHA-256 is `3724c41b4aa444201023fb9a0b39532336189339b8dc6285b5ce0b01a058cf76`. REA's function dossiers correctly identify the clamp at `0x100000328`, caller at `0x100000344`, and both call sites (`0x100000354`, `0x100000360`). The returned clamp pseudocode compiles unchanged and passes **20,005 independent input checks**, including both integer extremes and the clamp boundaries.

This fixture also exposes a limitation: the caller's pseudocode omits the first call's argument and infers incomplete signatures, although its assembly and call edges are correct. Compiling one recovered fixture does not validate arbitrary inferred pseudocode. Local verification-report SHA-256: `ea83173a2293cdc4f39799780036a3f864fb007ac18196a867d898d8ef6828e3`.

Evidence IDs:

- Clamp: `ev_82e06518c42d6b70c7ef2129b69c8ba8f3bcfffc548c0f0ddd35d7866c1de543`.
- Caller: `ev_9280dc8fb2015debecf15ea6254090fb4391cdc62355b5d2f32fbc731bc83487`.

These are local REA evidence identities, not public downloadable proprietary artifacts.

## Authorized component observations and next implementation question

The existing Analog ARM64 slice from the [component audit](additional-component-audit.md) is 432,640 bytes with SHA-256 `91e72fa0e582db6540343251174951f425028f1d74e660b6ceb5ec8d068250f7`. Its checksum was unchanged after both queries.

| Selected entry | Actual result | Remaining boundary |
| --- | --- | --- |
| Processing adapter `0x19a10` | Dossier covers a 1,412-byte modeled body, seven resolved callees and one unresolved indirect call at `0x19df0`. Independent LLVM output confirms the indirect instruction. Channel clamping, input/output copying and persistent internal block counters are visible. | The inner DSP call target and its processing semantics are not resolved by this dossier. REA marks callee coverage partial and reports type-propagation warnings. |
| Parameter forwarding `0x19ff8` | Six decoded instructions load a nested object, check for null and dispatch through a virtual-table slot. LLVM independently agrees. | The final computed tail branch has no established destination. REA labels its **call-reference** coverage complete, but that does not resolve the computed jump or recover parameter meaning. |

Processing evidence: `ev_42951606eb7172c4f9fabc1abb503e164c6ba56aa75a5ed464c3215dc44b5b7d`, JSON SHA-256 `bb35c66656f6891dd999c3829248adc382d4b73c17af4b072af5ef40dbdce209`. Parameter evidence: `ev_d02f512ac8b9340fc494af20d71a8606b7136b40ae5be510245ad8b467205453`, JSON SHA-256 `7806fedbc29981ac384bfbf4ba335edd29882333a23f3dc6fb0c4cdd1afb5ea7`.

The next concrete question is **how event offsets, latency and parameter changes behave across this adapter's internal processing boundaries when host block sizes change**. The adapter observations justify investigating that seam; they do not establish a delay value or an oscillator/filter formula. Follow the already mapped constructor/table and inner wrapper, then compare generated note-on/off and parameter-step renders across irregular block sizes before changing Halftrack's audible behavior. Do not copy the reference adapter's exceptional-path logging into Halftrack's real-time callbacks.

REA is useful here for structured, address-bound dossiers, call relationships, limitations and reusable evidence. It does not remove the need to resolve indirect dispatch, reconstruct state, write original portable implementations and compare actual outputs. This evaluation adds no claim to the feature-completion percentage.

## Subsequent dispatch resolution

The [factory-path follow-up](rea-dispatch-trace.md) resolves adapter call `0x19df0` to wrapper `0x18d24`, and initialized wrapper call `0x18eac` to inner processing entry `0xe78c`, through constructor stores, table pointers, relocation data and independent LLVM evidence. This narrows the earlier unresolved edge on that concrete path. A new REA dossier still reports partial callee coverage inside the inner routine; it does not establish complete instrument DSP, timing semantics or sound equivalence.
