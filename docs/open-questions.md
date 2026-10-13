# Open questions and working defaults

- The supplied phase roadmap conflicts with the final instruction to ignore phases. Default: continue the existing single preview, adding the newly requested MIDI workflow; retain the owner's earlier preview approval gate.
- The newer scope excludes programmable patching, branded hardware and cloud work. Default: remove those targets from the active product scope.
- A strict callback ban cannot be asserted for arbitrary external plug-ins or all upstream internals by inspecting our code alone. Default: enforce it for owned DSP, document engine thread boundaries, add executable offline checks, and keep whole-engine/dependency guarantees explicitly unverified.
- Earlier instructions authorized firsthand application inspection. The new rules prohibit it. Default: stop further inspection, retire those notes from the active spec, preserve historical commits, and do not claim a formal historical clean-room separation.
- New projects currently start with two empty audio tracks. Default: preserve that behavior and expose a clear MIDI-track command; do not silently put instruments into empty audio projects.
- Exact cross-format plug-in state translation is not specified. Default: persist native identity/state and show a missing-format error; do not guess replacements.
- The phrase one CMake command means a reproducible configure/build entry point. Default: provide CMake workflow presets, retaining standard configure/build commands for contributors.
