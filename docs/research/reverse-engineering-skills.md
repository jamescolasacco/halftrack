# Reverse-engineering skill evaluation

Reviewed 2026-10-13. Scope: agent guidance and local analysis tooling for a macOS ARM64 C++ application. This report evaluates public source; it contains no proprietary code, extracted strings, decompilation output, or application assets. No candidate skill was installed, and no downloaded skill script was executed during this review.

## Recommendation

Use the **Trail of Bits curated `ghidra-headless` skill** as the starting reference for local static analysis. It is a small, inspectable wrapper around Ghidra, with no MCP service or cloud upload in the reviewed files. Keep the analysis address-based and record coverage explicitly: its bundled exports have truncation and C++ naming limitations, so they cannot establish complete behavioral equivalence.

Add **`audit-context-building`** as a methodology for documenting recovered functions and their unresolved assumptions. Use **`dwarf-expert`** only if matching debug information exists. Neither supporting skill is a decompiler.

## Discovery and provenance

Checked the [skills.sh leaderboard](https://skills.sh/) first, then queried the directory for `reverse engineering`, `ghidra`, and `dwarf` scoped to Trail of Bits. Read actual skill text, plugin metadata, shell wrappers, Java exporters, and the audit-context JavaScript workflow from GitHub revisions below. Reviewed alternative candidates for task fit; popularity was not treated as a security guarantee.

GitHub API metadata was retrieved on the review date. Repository stars cover all skills in a repository. Directory pages and CLI results use different caches, so differing counts are recorded rather than silently reconciled.

| Rank | Candidate and role | Observed popularity | Reviewed revision |
| --- | --- | --- | --- |
| 1 | [Trail of Bits `ghidra-headless`](https://www.skills.sh/trailofbits/skills-curated/ghidra-headless), direct static-analysis wrapper, plugin 1.0.0 | 513 repository stars; 438 directory installs / 452 CLI installs | `trailofbits/skills-curated` at `6d05be4889017b06fb15069f371afd220daffb62`; repository pushed 2026-07-14 |
| Alternative to 1 | [Armin Ronacher `ghidra`](https://www.skills.sh/mitsuhiko/agent-stuff/ghidra), upstream source of the curated package | 3,185 repository stars; 609 directory installs / 583 CLI installs | `mitsuhiko/agent-stuff` at `0865c849befd2021490679f96a8dee58c84ac857`; repository pushed 2026-09-27 |
| 2 | [Trail of Bits `audit-context-building`](https://www.skills.sh/trailofbits/skills/audit-context-building), evidence and function-analysis workflow, plugin 2.0.2 | 7,512 repository stars; 6.3K directory installs | `trailofbits/skills` at `8e54050ae63c2f62224c35b1616efe71865b4777`; repository pushed 2026-10-13 |
| 3, conditional | [Trail of Bits `dwarf-expert`](https://skills.sh/trailofbits/skills/dwarf-expert), debug-information analysis, plugin 1.1.2 | Same 7,512-star repository; current CLI reports 5K installs; older directory page reports 1.5K weekly installs, not a comparable total | Same `trailofbits/skills` revision |

## Direct candidate: reviewed execution surface

[Exact skill](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/SKILL.md) and [plugin directory](https://github.com/trailofbits/skills-curated/tree/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless).

The curated package credits Armin Ronacher and retains its Apache-2.0 license. The surrounding marketplace has a different root license; preserve the package's own notices if copying its files. Compared with the upstream revision, the curated scripts add stricter shell handling and broader JSON character escaping. Its README also correctly distinguishes `ExportAll` from separate call-graph and symbol exporters. [Package provenance](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/README.md), [package license](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/LICENSE).

The reviewed package contains two shell scripts and six Java scripts. The wrapper invokes the local `analyzeHeadless` executable with an argument array, imports the selected file, and writes logs and exports. The Java scripts query the Ghidra program model and write files. No network requests, credential access, remote model calls, MCP registration, persistence hooks, or target execution were found in these reviewed package files. This is a source review, not a runtime security certification of Ghidra or every installed extension.

Operational details that matter:

- Set `GHIDRA_HOME` explicitly. The fallback finder searches common install paths and then a portion of the home directory; it is unnecessary when the tool path is known.
- Use a fresh analysis directory and generated project name. Exports use ordinary `FileWriter` and logs use `tee`, so existing outputs can be overwritten.
- Preserve the Ghidra project deliberately: the wrapper adds `-deleteProject` unless `--keep-project` is selected.
- Only use inspected post-scripts. `--script-path` and `--script` allow arbitrary additional Ghidra script execution; `--script-args` performs shell word splitting and glob expansion, not robust structured argument passing.
- Both skill descriptions say Java 17+. That is insufficient for the selected Ghidra **12.1.4 release**, whose tagged documentation requires **JDK 21**. The development branch currently asks for JDK 25; that is not the requirement of release 12.1.4. [Release-specific requirements](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/README.md).

[Finder source](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/find-ghidra.sh), [wrapper source](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra-analyze.sh).

## Export completeness findings

These are concrete source findings, not hypothetical concerns:

| File | Observed limitation | Consequence |
| --- | --- | --- |
| [ExportCalls.java](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra_scripts/ExportCalls.java) | Maps use `Function.getName()` as the key rather than address plus namespace | Same-named C++ functions can overwrite one another in the exported graph; use addresses as identities |
| [ExportFunctions.java](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra_scripts/ExportFunctions.java) | Caps callers and callees at 50 each | Export is not the complete call relationship set |
| [ExportAll.java](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra_scripts/ExportAll.java) | Caps calls at 20; skips unsuccessful decompilations without emitting a failure entry | Summary output alone cannot establish coverage |
| [ExportDecompiled.java](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra_scripts/ExportDecompiled.java) | 30-second timeout per function; excludes external and thunk functions | Record omissions and timeouts; the wrapper's analysis timeout does not bound all post-script work |
| [ExportSymbols.java](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra_scripts/ExportSymbols.java) | Omits default symbols; caps retained symbols at 10,000 with a truncation marker | Does not export every symbol despite the broad description |
| [ExportStrings.java](https://github.com/trailofbits/skills-curated/blob/6d05be4889017b06fb15069f371afd220daffb62/plugins/ghidra-headless/skills/ghidra-headless/scripts/ghidra_scripts/ExportStrings.java) | Uses defined data, excludes strings shorter than four characters, truncates values over 1,000 characters | Not a complete byte-level string inventory |

The package lists an AArch64 processor ID and macOS discovery paths. It provides no special audio-DSP equivalence test, application reconstruction system, or verified procedure for recovering an entire optimized C++ application. A successful analysis is evidence about the examined code, not proof of a difference-free clone.

## Supporting candidates

`audit-context-building` 2.0.2 records invariants, assumptions, calls, unresolved boundaries, and a coverage dossier. Its domain notes explicitly acknowledge fallible recovered function boundaries. The inspected workflow writes local reports and dispatches analysis agents; no explicit HTTP or MCP code appears in that workflow. Its packaged slash-command and `Workflow` API are host-specific, so do not assume they are executable in every agent environment. Use the evidence format through available subagent tools when the packaged runtime is absent. This is a post-extraction reasoning method, not a binary loader. [Exact skill](https://github.com/trailofbits/skills/blob/8e54050ae63c2f62224c35b1616efe71865b4777/plugins/audit-context-building/skills/audit-context-building/SKILL.md), [workflow](https://github.com/trailofbits/skills/blob/8e54050ae63c2f62224c35b1616efe71865b4777/plugins/audit-context-building/workflows/audit-context.js), [binary-analysis domain notes](https://github.com/trailofbits/skills/blob/8e54050ae63c2f62224c35b1616efe71865b4777/plugins/audit-context-building/skills/audit-context-building/resources/DOMAIN_NOTES.md).

`dwarf-expert` 1.1.2 is text guidance for LLVM/libdwarf tooling, with no bundled execution script or MCP server. For macOS, its current text correctly points to matching dSYM/object debug information and warns that pyelftools only handles ELF. It cannot regenerate missing debug information from a stripped executable. Its source lookup advice involves public standards and LLVM documentation. [Exact skill](https://github.com/trailofbits/skills/blob/8e54050ae63c2f62224c35b1616efe71865b4777/plugins/dwarf-expert/skills/dwarf-expert/SKILL.md).

## Candidates not selected

- **Upstream `mitsuhiko/agent-stuff/ghidra`:** useful provenance and a viable alternative, but duplicate capability. Prefer the curated fork's source-reviewed changes rather than installing both.
- **`2389-research/binary-re`:** 20 stars at `31d0a3fdf705ed78c9d8f7d7c1aef673ad15a3a0`. Main workflow targets ELF, Linux/QEMU/GDB and firmware. Tool setup includes a remote shell installer and privileged Docker commands. These are visible setup examples, not proof of malicious behavior, but they are unnecessary for this Mac static-analysis task. Not selected for automatic installation. [Main skill](https://github.com/2389-research/binary-re/blob/31d0a3fdf705ed78c9d8f7d7c1aef673ad15a3a0/skills/binary-re/SKILL.md), [setup](https://github.com/2389-research/binary-re/blob/31d0a3fdf705ed78c9d8f7d7c1aef673ad15a3a0/skills/tool-setup/SKILL.md).
- **`zhaoxuya520/reverse-skill/ghidra-reverse`:** 41,675 repository stars; approximately 1.1K directory installs. Reviewed at `cab634bd855fc287f6e420c1f36fd1a6b9245960`. The skill routes through shared journals/tool indexes and an optional bootstrap/MCP stack. The standalone skill is mostly routing and requires a broader review of its install graph; the repository's popularity does not make that graph necessary here. Not selected. [Exact skill](https://github.com/zhaoxuya520/reverse-skill/blob/cab634bd855fc287f6e420c1f36fd1a6b9245960/skills/ghidra-reverse/SKILL.md).
- **`wshobson/agents/protocol-reverse-engineering`:** 40,615 repository stars; approximately 12.2K directory installs. Reviewed at `46891e7e60da0e52baf1050b7b6391b64e84c6d9`. It focuses on network captures and protocol interception, rather than Mach-O code or DSP analysis. Task mismatch. [Exact skill](https://github.com/wshobson/agents/blob/46891e7e60da0e52baf1050b7b6391b64e84c6d9/plugins/reverse-engineering/skills/protocol-reverse-engineering/SKILL.md).
- **Trailmark:** useful for source call graphs or importing an existing binary-analysis graph, but explicitly does not disassemble binaries itself. Its skill requests installation of a Python tool. Defer until a concrete graph-analysis need exists. [Skill source](https://github.com/trailofbits/skills/blob/8e54050ae63c2f62224c35b1616efe71865b4777/plugins/trailmark/skills/trailmark/SKILL.md).

## Optional installation routes

The directory documents these commands. They are options, not commands executed by this research task, and they resolve a moving repository branch unless a separately verified pinned checkout is supplied:

```sh
npx skills add https://github.com/trailofbits/skills-curated --skill ghidra-headless
npx skills add https://github.com/trailofbits/skills --skill audit-context-building
npx skills add https://github.com/trailofbits/skills --skill dwarf-expert
```

For this project, reading the pinned skill and invoking a separately verified local Ghidra installation is sufficient; global installation and an MCP server are not required. Re-review changed scripts before updating. Keep binary-derived research outputs separate from the public application source and report findings with provenance and explicit unknowns.
