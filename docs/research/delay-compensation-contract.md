# Delay Compensation contract

Status: partial. This follows the reference's manual and static reading. Halftrack's own playback is measured on the synthetic test device; the reference's output was not measured.

## Evidence

- Live 12 manual, [Working with Instruments and Effects](https://www.ableton.com/en/live-manual/12/working-with-instruments-and-effects/), section 23.3, consulted 2026-10-24: Live compensates for the delays of its devices and plug-ins, return tracks included, and is on by default. The Options menu's Delay Compensation switches it, which is not normally recommended. The Track Delay controls are unavailable while it is off. Reduced Latency When Monitoring, offered only while compensation is on, gives monitored tracks the lowest latency at the cost of their sync with compensated tracks.
- The reference's Options builder adds Delay Compensation (`lcmp`) and Reduced Latency When Monitoring (`lcpt`) after External Sync. Its handlers (Live 12.2.1 arm64, 0x10028a63c to 0x10028a7cc) keep both in one Set value, `LSong::mpLatencyCompensation`: 0 is off, 1 is on with Reduced Latency When Monitoring, and 2 is on without it. Switching compensation off remembers which of the two it was. The bundled default Set holds 2: on, without Reduced Latency When Monitoring.

## Halftrack's behaviour

- **The switch.** Options > Delay Compensation, on at first, is kept with the project, outside the undo history.
- **On.** The engine holds every path back to line up with the most delayed one, as before.
- **Off.** Playback hears each device's delay: Tracktion's player is built without compensation. Track Delay does nothing, its value kept, and its controls are greyed.
- **Renders.** Exports, freezes and bounces keep compensating either way.

## Not yet

- Reduced Latency When Monitoring.
- Renders without compensation.

## Evidence in Halftrack

`tests/AudioEngineTests.cpp` plays a click on two tracks, one through a Limiter with its 1 ms lookahead, on the synthetic device at 48 kHz. Compensated, the clicks are heard as one; off, the Limiter's click is heard 48 samples late, the switch staying out of the undo history; on again, they line up. The track order suite checks the default and that a saved project keeps the switch off. The UI suite switches it from the Options menu and finds a strip's Track Delay greyed meanwhile.
