# MIDI input consumer lifetime

## Reproduction

The monitoring suite now constructs the pinned engine's actual MIDI input node with a real, generated virtual input instance. It prepares graph A, prepares graph B with A as its replacement source, retires A, sends a controller key, and processes B for the first time. No timer, audio hardware, repeat-until-pass loop, or increased timeout is involved.

Before the correction, this test fails: B emits zero note-ons. The complete monitoring command fails in 3.14 seconds at `first key survives retirement before replacement callback`.

A and B share their bounded MIDI queue. The shared state's `activeNode` still names A after A removes its input consumer and is destroyed. B's incoming-message handler rejects input because the active identity is not B. B only publishes itself when it eventually processes an audio callback, which is too late for the earlier key.

## Correction

The retiring node removes its input consumer, then uses an atomic compare/exchange to clear its identity only if it still owns the shared queue. An input consumer finding an unowned queue atomically claims it before enqueueing the message. An inactive graph's destruction cannot clear a newer graph's ownership.

The patch retains the existing initial-activation and receipt-age behavior. It adds no operation to the audio processing callback. It does not replace the dependency's existing synchronization, claim complete concurrent MIDI ingress certification, or prove the cause of every earlier intermittent CI failure.

`PatchMidiInputActivation.cmake` accepts the pinned pristine source and both previously published patch states, validates both source files before either write, and validates exact output hashes. The resulting cpp SHA256 is `a3e623634363acecd4096d29498b8237af3a468015aeccdf48716d4dab115866`; the header remains `82f9fae59e49a0293f5aa0e0ba8926faa359df5d33fafc92226480083fb049e5`. Dependency GPL notices remain intact.

## Coverage

The permanent native regression also checks queued plus fresh keys across overlapping replacements, exactly-once delivery, retirement of an inactive replacement after a newer node is active, and several prepared replacements retiring before the first callback. The existing full monitoring suite still exercises actual synth output, all monitor modes, record-arm history, held notes, sustain, source-free tracks, input reassignment, immediate activation, and stale/fresh input after an interruption.

Patch fixtures additionally verify fresh, repeated, receipt-age upgrade, cpp-only/header-only partial states, and rejection of unknown cpp/header bytes without modifying either file. These fixtures use the pinned dependency's Git source and do not copy dependency source into Halftrack's original files.

The corrected full MIDI monitoring suite passes in **19.49 seconds** with the existing CI dispatch setting and unchanged signal thresholds/timeouts.
The separate MIDI input/recording suite passes in **33.91 seconds**, including native recording, overdub, generated controller input, save/reopen and rendered signal checks.
