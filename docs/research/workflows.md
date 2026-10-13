# Behavior specification and evidence policy

Updated 2026-10-13 after the owner explicitly authorized local reverse engineering. Public descriptions, firsthand observations and bounded binary analysis may inform independently written behavior specifications. The former public-descriptions-only restriction is superseded; no formal clean-room separation is claimed.

Use exact reference version, binary hash, architecture, addresses, tool version and experiment inputs. Separate measured outputs, tool-generated hypotheses and unresolved behavior. Keep raw binaries, symbol inventories, screenshots and decompiler output in ignored local artifacts. Public source contains original implementations, generic tooling, generated fixtures and factual summaries; do not present extracted third-party material as AGPL-covered Halftrack code.

Primary implementation sources retain their licenses:

- [JUCE documentation](https://docs.juce.com/master/index.html)
- [Tracktion Engine](https://github.com/Tracktion/tracktion_engine)
- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
- [CLAP SDK](https://github.com/free-audio/clap)

The capability ledger is a planning map, not proof of complete behavior. [Measurement research](behavioral-parity-measurement.md) defines the difference between bounded equivalence, static analysis coverage and whole-product claims. Functional tests, complete-file signal comparisons and physical hardware checks answer different questions.
