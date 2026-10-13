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
