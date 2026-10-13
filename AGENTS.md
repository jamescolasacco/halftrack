# Halftrack engineering rules

## Product and current approval gate

The user wants an independent open-source recreation of installed Ableton Live 12 Suite 12.2.1. Prioritize behavioral fidelity and close sound matching. Existing .als projects and unchanged Max patches are not compatibility goals. Shared macOS/Windows code and tests are mandatory. The analog identity is for later customization; the baseline layout should remain familiar.

The user authorized ONE runnable basic preview (record audio, mix with plug-ins, save/reopen, export), then requires their approval before the full implementation sweep. Do not publish fake completion claims or silently cross that gate. Small meaningful commits are required throughout. A roadmap checkbox is not an implemented feature.

## Architecture

Use C++20/CMake with exact dependency pins. Keep UI and product commands separate from Tracktion internals through the Session facade. Keep the same project schema and rendering behavior on macOS and Windows; platform differences belong in adapters. Do not create a second Windows application.

Never allocate, take blocking locks, do file I/O, scan plug-ins, or touch UI from an audio callback. Validate input sizes and sample ranges. Use the engine's established streaming and graph mechanisms. Stop and drain recording before replacing project state. Keep device and editor lifetime explicit.

Protect user sessions and audio. Tests generate their own fixtures. Never record ambient microphone audio during unattended verification. Don't overwrite existing user projects/exports without the UI's explicit overwrite choice. Import project assets into a portable project-owned directory. Missing media and missing plug-ins must produce actionable errors.

## Quality

No fake meters, waveform drawings without source audio, unsupported clickable controls, invented success messages, or placeholder processing. Every active control must affect actual state. Record limitations honestly in the feature ledger. Separate built, tested, and hardware-verified status.

Tests must exercise observable behavior: rendered audio, saved/reopened state, recording lifecycle, timing, plugin state and bus behavior. Keep tests deterministic and run on macOS and Windows CI. Do not claim hardware tests from headless CI.

Before commits, inspect staged files for generated build outputs, local paths, personal plug-in inventory, user recordings, and secrets. Keep changes focused, commit after meaningful verified increments, and push those commits to the requested repository.
