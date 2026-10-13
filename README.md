# Halftrack

An open-source desktop music studio with an original dark interface and an analog identity.

Halftrack is inspired by Ableton Live and is unaffiliated with Ableton.

**Status: native preview in development and verification. This is not a complete DAW.**

C++20, JUCE 8, and Tracktion Engine share one model and interface across macOS, Windows, and Linux. Original code and SVG assets are AGPL-3.0-only. Dependencies retain their own licenses.

The first preview combines audio recording/mixing/export with MIDI tracks, a piano roll, a built-in polyphonic subtractive synth, project save/reopen, and undo/redo. Only behavior backed by evidence is marked verified. The owner reviews this one preview before the larger implementation sweep.

See [build instructions](docs/building.md), [architecture](docs/architecture.md), [progress and known gaps](docs/progress.md), [verification evidence](docs/preview-verification.md), [capability inventory](docs/feature-parity.md), and [open questions](docs/open-questions.md).

## Independent implementation

Original code, UI, and assets; no copied proprietary binaries, libraries of sounds, presets, artwork, fonts, or manual text. Future behavior specifications use public workflow descriptions. The project uses its own versioned `.halftrack` XML format with a sibling Assets folder.

## License

See [LICENSE](LICENSE) and [dependency inventory](docs/dependencies.md). JUCE uses its AGPLv3 option and Tracktion its GPLv3-or-later option. External commercial plug-ins are not redistributed. Preview archives include corresponding source and dependency notices.
