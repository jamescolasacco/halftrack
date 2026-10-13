# Halftrack

An open-source desktop music studio, built around a familiar clip-and-arrangement workflow.

**Status: the first native preview is under development. This is not a feature-complete DAW or an Ableton-compatible project loader.**

Halftrack targets macOS and Windows from one C++20 codebase. The reference for behavioral research is Ableton Live 12 Suite 12.2.1. Halftrack is an independent implementation with original branding, code, and assets; it is not affiliated with Ableton.

## Direction

- Record, edit, arrange, mix, and export in a shared desktop interface.
- Reproduce the reference workflow and closely match sound through measured comparisons.
- Keep the engine, project model, commands, and interface shared across platforms.
- Build an open-source programmable device environment over time.
- Keep distributed modifications open source.

The first review milestone is an actual audio recording, plug-in mixing, save/reopen, and WAV export workflow. Full feature work continues after approval of that preview. The project does not aim to load `.als` projects or execute existing Max patches unchanged.

See [the feature ledger](docs/feature-parity.md), [research](docs/research/ableton-reference.md), and [decisions](docs/decisions.md). Features are only marked verified when evidence exists for the relevant platform.

## License

Halftrack's original code is licensed under AGPL-3.0-only; see [LICENSE](LICENSE). Dependencies retain their own licenses. JUCE is used under its open-source AGPLv3 option and Tracktion Engine under its GPLv3-or-later option. No commercial plug-ins, vendor sound libraries, or Ableton assets are bundled.
