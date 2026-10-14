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

## Multiple-note editing defaults

The current editor draws on ordinary empty-space clicks. Keep that behavior while adding Shift-click toggles, Shift-drag additive marquee selection, Cmd/Ctrl+A and Escape. A Shift-click on empty space clears selection; a drag instead combines its rectangle with the selection present at mouse-down. Group motion uses a common bounded time/pitch delta, preserving spacing at the clip and MIDI boundaries; group resizing uses a common length delta. Velocity explicitly sets every selected note to the displayed value. Selection is local editor state, pruned against stable note IDs, and is not itself an undoable musical edit. A separate draw/select mode, relative-velocity editing, time-span selection and multi-clip editing remain separate work. These defaults are not claims of measured reference equivalence.

## Note clipboard defaults

Copy/Cut/Paste/Duplicate act on the selected notes only. Keep a typed in-application clipboard across clip changes, preserving relative timing, pitch, length and velocity. Use a visible insertion cursor set from the ruler and moved by arrows when the selection is empty; a new clip starts at beat zero. A successful paste advances the cursor to the pasted group's end. Duplicate places the copied group after its selected extent, using the current grid. Reject a whole group that does not fit rather than truncating notes, overwriting unrelated notes or extending the clip silently. Insertions receive fresh stable identities and form one undo step. These are explicit working defaults, not measured reference equivalence. System clipboard interchange, time-span editing and automatic clip extension remain separate work.
