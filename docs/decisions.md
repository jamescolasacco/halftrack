# Product decisions

Updated 2026-10-13 following the owner's latest instructions.

| Topic | Decision |
|---|---|
| Product | Independent workflow-oriented open-source DAW, original UI and assets. |
| Specification | Public descriptions plus authorized local observation, disassembly and decompilation; record provenance, coverage and uncertainty. |
| Persistence | Versioned human-readable `.halftrack` XML; no proprietary project import target. |
| Audio | Aim for the closest measurable behavior and output, including exact comparisons where meaningful; no unmeasured zero-difference claim. |
| Scope exclusions | Programmable patch environment, branded hardware integration, and cloud features. |
| License | Original code/assets AGPL-3.0-only, distributed modified versions remain open. |
| Stack | C++20, JUCE 8, Tracktion, CMake, Signalsmith Stretch MIT. |
| Platforms | macOS first, shared Windows and Linux builds and tests. |
| Preview | Audio recording/mixing/export plus MIDI tracks/piano roll/synth/save/reopen/undo. |
| Gate | First preview handed off; latest owner instructions authorize renewed research and implementation. |
| Naming | Halftrack identity; reference names may identify research subjects in factual documentation. |
| Workflow | Small tested commits per owned feature; architecture/progress current; ambiguity recorded with simple defaults. |

The latest owner instructions supersede the earlier no-inspection rule and preview gate. Binary-assisted research is not described as clean-room development. Earlier decisions and research remain in history for transparency. The existing license applies to original Halftrack contributions and does not relicense third-party material. Proprietary formats, hardware integration, programmable patch environments and cloud access remain outside the current work unless explicitly revisited.
