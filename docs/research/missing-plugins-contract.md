# Missing plug-in contract

As in the reference, a set whose plug-in is not available still opens. The device stays in its chain as a placeholder.

## Behaviour

- **Loading.** Opening a project no longer fails when an external plug-in cannot load. The engine keeps the device with the identity and state it was saved with.
- **The snapshot** marks such a device `missing`, with a `loadError`. When the engine's message is generic, the snapshot substitutes "It is not installed, or has not been scanned."
- **Audio** passes through the placeholder unprocessed. Its editor and parameters are refused with the load error.
- **Saving** writes its `uid`, `filename`, `name`, `manufacturer` and `state` attributes unchanged. Once the plug-in is scanned, reopening the project loads it from that state.
- **The device chain** shows "Missing plug-in", the reason, and that its settings are kept; its window button is disabled.
- **Opening a project** reports the missing plug-ins by name in the status bar.

## Evidence

The persistence suite (part of `--self-test`) saves a project with the VST3 test fixture and clears the scanned list. It then checks:

- that the project opens with a named placeholder that gives a reason;
- that the placeholder's editor and parameters are refused;
- that a render through it is audible;
- that re-saving keeps the five identity and state attributes byte for byte;
- that, after the fixture is registered again, the re-saved project loads the plug-in.

Missing: placeholders inside racks, reloading in place after a scan without reopening, missing instruments, and the reference's exact placeholder display.
