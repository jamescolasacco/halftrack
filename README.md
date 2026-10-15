# Halftrack

An open-source desktop music studio with an original dark interface and an analog identity.

Halftrack is inspired by Ableton Live and is unaffiliated with Ableton.

**Status: native preview in development and verification. This is not a complete DAW.**

C++20, JUCE 8, and Tracktion Engine share one model and interface across macOS, Windows, and Linux. Original code and SVG assets are AGPL-3.0-only. Dependencies retain their own licenses.

The first preview combines audio recording/mixing/export with MIDI tracks, a piano roll, a built-in polyphonic subtractive synth, project save/reopen, and undo/redo. Only behavior backed by evidence is marked verified. Continued development adds Session audio/MIDI slots, scenes, beat/bar launching, tested plug-in latency compensation at export boundaries, native Pre/Post sends and shared return tracks, hardened plug-in scanning/editor lifetime, grouped MIDI editing, a note clipboard with one-step undo, tested sine/noise/pulse-width synthesis, and an insertable channel Utility with stereo/bass processing, saved controls and undoable device ordering. Four further native effects provide an eight-band EQ, peak/RMS compressor, tempo-synchronized stereo delay and room reverb. Eight piano-roll note tools add quantization, reversal, pitch inversion, legato and time/length edits, all using the same project and undo model. Arrangement editing now includes audio/MIDI clipboard operations, cross-track movement, locators, non-destructive reversal, Standard MIDI File interchange, track ordering/duplication and precise 20–999 BPM tempo. MIDI controller selection, monitoring, Arrangement recording and overdub create editable notes with native undo and project persistence. Local reverse-engineering research remains separate from original application source.

See [build instructions](docs/building.md), [architecture](docs/architecture.md), [progress and known gaps](docs/progress.md), [verification evidence](docs/preview-verification.md), [capability inventory](docs/feature-parity.md), [open questions](docs/open-questions.md), and [reverse-engineering results and research](docs/research/reverse-engineering-results.md).

## Independent implementation

Original code, UI, and assets; no copied proprietary binaries, libraries of sounds, presets, artwork, fonts, or manual text. Behavior specifications combine public workflow descriptions with authorized local observations and binary analysis. Raw reference binaries and decompiler output stay in ignored local research artifacts; they are not distributed as Halftrack source. The project uses its own versioned `.halftrack` XML format with a sibling Assets folder.

## License

See [LICENSE](LICENSE) and [dependency inventory](docs/dependencies.md). JUCE uses its AGPLv3 option and Tracktion its GPLv3-or-later option. External commercial plug-ins are not redistributed. Preview archives include corresponding source and dependency notices.
