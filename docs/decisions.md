# Product decisions

Updated 2026-10-13 following the owner's latest instructions.

| Topic | Decision |
|---|---|
| Product | Independent workflow-oriented open-source DAW, original UI and assets. |
| Specification | Public workflow descriptions only, paraphrased independently; no proprietary binary or content inspection. |
| Persistence | Versioned human-readable `.halftrack` XML; no proprietary project import target. |
| Audio | Test observable behavior and known-good DSP output; no bit-identical emulation claim. |
| Scope exclusions | Programmable patch environment, branded hardware integration, and cloud features. |
| License | Original code/assets AGPL-3.0-only, distributed modified versions remain open. |
| Stack | C++20, JUCE 8, Tracktion, CMake, Signalsmith Stretch MIT. |
| Platforms | macOS first, shared Windows and Linux builds and tests. |
| Preview | Audio recording/mixing/export plus MIDI tracks/piano roll/synth/save/reopen/undo. |
| Gate | One runnable preview, then owner approval before larger development sweep. |
| Naming | Halftrack; one inspiration/unaffiliated README sentence, no reference branding elsewhere. |
| Workflow | Small tested commits per owned feature; architecture/progress current; ambiguity recorded with simple defaults. |

The later rules supersede the earlier firsthand-research and programmable-device plans. Earlier commits remain intact for transparency; retired research is not current implementation evidence. The current workflow does not pretend that historical application inspection never occurred.
