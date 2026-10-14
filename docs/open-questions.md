# Open questions and working defaults

- The supplied phase roadmap was superseded by the instruction to ignore phases. The preview has been delivered, and the latest instruction authorizes further research and implementation. No preview gate currently blocks work.
- The newer scope excludes programmable patching, branded hardware and cloud work. Default: remove those targets from the active product scope.
- A strict callback ban cannot be asserted for arbitrary external plug-ins or all upstream internals by inspecting our code alone. Default: enforce it for owned DSP, document engine thread boundaries, add executable offline checks, and keep whole-engine/dependency guarantees explicitly unverified.
- The previous no-inspection rule was explicitly reversed by the owner. Default: permit local read-only inspection and decompilation with evidence and coverage records; preserve original implementation/third-party artifact boundaries.
- New projects currently start with two empty audio tracks. Default: preserve that behavior and expose a clear MIDI-track command; do not silently put instruments into empty audio projects.
- Exact cross-format plug-in state translation is not specified. Default: persist native identity/state and show a missing-format error; do not guess replacements.
- The phrase one CMake command means a reproducible configure/build entry point. Default: provide CMake workflow presets, retaining standard configure/build commands for contributors.

## Reverse-engineering direction (2026-10-13)

The owner now authorizes local binary analysis/decompilation and prioritizes the closest possible match over the earlier clean-room process. Original Halftrack code remains under the existing AGPL license; this does not relicense extracted third-party material. Default: keep binary/decompiler artifacts local and ignored, publish original tooling and factual summaries only. The absolute-zero-difference target needs explicit coverage of inputs, versions, hardware and nondeterministic behavior; no finite test run establishes every possible state. Hardware/account/cloud access is not expanded by this instruction.

## Sends and returns: implementation defaults

Use native auxiliary buses with return tracks feeding the main mix. Start with no returns; creating one selects Post, unity return gain, and every source send Off. Pre sends follow source effects but precede source pan, gain and explicit mute; Post follows those controls. Return-to-return sends, feedback and separate hardware output remain outside this first routing increment.

The public mixer description does not fully enumerate combined source/return solos. Default: keep a feed when nothing is soloed, its source is soloed, or its destination return is soloed, then apply explicit source mute to Post feeds. Keep required returns audible for a soloed source. This combined rule must pass Halftrack's audio tests and remains unverified against the reference application. Return identity is a stable track ID, never its name or position. See the [primary workflow description](https://www.ableton.com/en/live-manual/12/mixing/).

## Scene command concurrency: required semantics

The existing per-track command submission is not atomic across a concurrent processing callback. The next correction must accept an entire scene at one common pre-graph boundary; commands published after that boundary's snapshot take effect on the next outer callback, including Return and cancellation. Preserve native late-boundary catch-up as a whole-scene behavior. Do not add a shared flag around the existing independently locked handle queues and call it atomic.

The local source audit identified the playback context's existing continuity callback as a candidate integration seam. A concrete implementation still needs bounded whole-command publication, an ordered cancellation path that remains available under queue saturation, target lifetimes through deletion/undo, and delayed ownership publication that cannot cancel a newer scene. These are implementation requirements, not a runtime fix or a completed concurrency proof. The existing serialized-command launcher tests remain valid within their documented scope.

A further native-source audit found that the node player can acquire a different prepared graph for each tempo/loop subrange within one outer callback. Freezing only the command queue boundary is therefore insufficient to prove stable target membership. Retained pointers prevent premature destruction but do not alone prevent deleted targets from relaunching after undo. A safe fix needs coordinated graph-generation retirement or an explicitly narrower structural-edit policy. Timed Stop Track is a silent takeover and cannot be indiscriminately coalesced into global Return/cancellation. No concurrency implementation is claimed.

## Multiple-note editing defaults

The current editor draws on ordinary empty-space clicks. Keep that behavior while adding Shift-click toggles, Shift-drag additive marquee selection, Cmd/Ctrl+A and Escape. A Shift-click on empty space clears selection; a drag instead combines its rectangle with the selection present at mouse-down. Group motion uses a common bounded time/pitch delta, preserving spacing at the clip and MIDI boundaries; group resizing uses a common length delta. Velocity explicitly sets every selected note to the displayed value. Selection is local editor state, pruned against stable note IDs, and is not itself an undoable musical edit. A separate draw/select mode, relative-velocity editing, time-span selection and multi-clip editing remain separate work. These defaults are not claims of measured reference equivalence.

## Note clipboard defaults

Copy/Cut/Paste/Duplicate act on the selected notes only. Keep a typed in-application clipboard across clip changes, preserving relative timing, pitch, length and velocity. Use a visible insertion cursor set from the ruler and moved by arrows when the selection is empty; a new clip starts at beat zero. A successful paste advances the cursor to the pasted group's end. Duplicate places the copied group after its selected extent, using the current grid. Reject a whole group that does not fit rather than truncating notes, overwriting unrelated notes or extending the clip silently. Insertions receive fresh stable identities and form one undo step. These are explicit working defaults, not measured reference equivalence. System clipboard interchange, time-span editing and automatic clip extension remain separate work.


## Built-in instrument pitch-wheel default

The instrument has no specified bend-range control. Use a fixed range of two semitones down/up, center 8192, minimum 0 and maximum 16383. Normalize each side separately so both endpoints reach exactly two semitones. Bend is transient per-channel controller state, applies to existing and subsequent voices without restarting their envelopes or phases, and retains original note identity for note-off. CC121 centers that channel; CC120/123 preserve the wheel, while full host reset clears it. Ignore invalid channels/values. Range editing, RPNs, MPE, aftertouch, controller lanes and external MIDI-device UI remain separate work. These are explicit Halftrack defaults, not measured reference-instrument equivalence.


## Oscillator expansion defaults

Retain saved waveform values 0 (saw) and 1 (square at default width), then add 2 (sine) and 3 (white noise). Pulse width is a normalized 0–1 patch parameter, default 1; map it to 0–50% duty so 100% remains square, consistent with the public workflow description. Use an original zero-mean band-limited pulse; width zero has zero oscillator output. Preserve phase/envelope/filter state when changing controls, and use deterministic per-voice noise for repeatable offline renders. Noise sequence, exact width mapping between endpoints, DC treatment and filter/output response are Halftrack defaults pending controlled reference measurements. Existing projects lacking the field keep their previous square sound. This increment does not claim reconstruction of an entire reference instrument or its dual-oscillator/filter/modulation architecture.

## Channel utility behavior (2026-10-14)

The public controls are specified, but paired reference renders do not yet establish smoothing time, balance law, low-frequency filter topology, DC cutoff, or exact processing order. Default implementation: 5 ms linear ramps for gain/balance/width/mid-side weights; constant-power balance normalized to unity at center; a third-order Butterworth high-pass on side and low-pass audition; a 5 Hz second-order DC blocker with Q=0.5 and unity Nyquist gain. Input polarity precedes channel selection and unweighted mid/side filter history. Bass shaping then receives width/mid-side/mono weights, output gain/balance, DC blocking and final mute. Audition only applies while bass mono is enabled. The balance law and filter orders follow a later binary-assisted trace in the device contract. The numerical realization and remaining defaults are explicit Halftrack choices, not sample-identical recovery claims. Preserve all disabled controls' values. Stable device IDs identify settings and undo operations.

## Native effects and note transformations

Public descriptions do not determine exact DSP equations or every editing edge case. The current working defaults and missing controls are explicit in the [effects contract](research/native-effects-contract.md) and [transformation interface](architecture.md#midi-transformations). Delay sync follows the single project tempo through bounded control packets; sample-accurate tempo maps remain future work. Note transforms reject a whole operation that cannot fit the clip. Exact-output comparisons are now requested by the owner, superseding the old OUT-003 exclusion; no zero-difference result is inferred from these defaults or independent mathematical tests.
