# Halftrack decisions

Accepted 2026-10-13 by the project owner during the grilling interview.

| Topic | Decision |
|---|---|
| Reference | Installed Ableton Live 12 Suite 12.2.1; exact build identity recorded in research. |
| Product | Independent open-source clone of workflow/features, not a binary port. |
| Project compatibility | Own format; Ableton .als import is not a goal. |
| Audio fidelity | Matching behavior and very close sound with controlled comparison evidence; not bit identity. |
| Extensibility | Eventually an open-source equivalent to Max for Live, not unchanged Max patch execution. |
| Licensing | Distributed modified versions must share source. Original code AGPL-3.0-only. |
| Platforms | Native macOS first, same code/model/interface/tests for Windows. |
| First workflow | Select devices, record audio, mix through plug-ins, save/reopen and export. |
| Review gate | Produce one runnable basic preview; wait for the owner's approval before the full implementation sweep. |
| Branding | Halftrack, editable temporary logo now; analog redesign after baseline. |
| Repository | Public jamescolasacco/halftrack, meaningful incremental commits. |

## Foundation

Evaluate C++20 + JUCE + Tracktion Engine. Tracktion supplies sequencing and graph infrastructure but does not establish Ableton behavior. Halftrack owns the interface and application commands, with a facade around engine details. Pin compatible dependency revisions and carry platform-independent regression tests.

JUCE's open-source license is AGPLv3, Tracktion's is GPLv3-or-later. Preserve each upstream license and notices. Third-party plug-ins are external user-installed components, never bundled. The precise distribution obligations of all dependencies and external plugin integration remain part of release review; do not invent a license exception on behalf of dependencies.

## Completion evidence

A feature needs an observed/reference description, an acceptance test or documented manual check, and platform status. Basic preview success is not full product parity. Exact hardware latency, native-device sound matching, controller behavior, and crash resilience require separate measured verification.
