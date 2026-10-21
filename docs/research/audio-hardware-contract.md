# Audio hardware contract

Halftrack opens audio interfaces through JUCE's CoreAudio, ASIO, WASAPI and ALSA types, which the engine owns. This contract records what has been measured on a physical interface, separately from the automated suites. The automated suites use simulated devices and never open a real one.

## Disconnection

The device the user chose is remembered. When it disappears (an unplugged interface), the device manager may fall back to another device; CoreAudio falls back to the system output.

- **On disconnection:** playback stops instead of moving there, takes are kept as Stop keeps them, and the project is untouched. A notice says what happened, for example "“M4” was disconnected. Playback stopped; audio goes to “MacBook Air Speakers” until “M4” returns."
- **On return:** the chosen device is reopened with its settings, and a notice says so.
- **Choosing another device by hand.** While the chosen device is present, this is a new choice, not a loss.
- **Settings changes.** A restart from a settings change on the chosen device still resumes playback, as before.

`tests/DeviceLossTests.cpp` checks this with a simulated device type whose interface can be unplugged and plugged back in. With JUCE's generic device manager, as here, nothing replaces the missing device.

## The hardware check

`--hardware-check <folder> [device] [loopback|unplug]` is a manual check, never run by CTest. It opens the named interface through a Session with temporary settings, so the user's Halftrack settings are untouched. It plays only quiet generated signals. Inputs stay hidden from the device manager unless the loopback measurement asks for input 3, so no microphone is opened. It writes `hardware-report.txt`.

## Measurements: MOTU M4, 2026-10-21

The interface is a MOTU M4 on USB, attached to an arm64 MacBook Air running macOS (Darwin 24.6), through CoreAudio, with its outputs only.

| Measurement | Result |
| --- | --- |
| Opening | 48 kHz, 256-sample buffer; 4 outputs; rates 44.1 to 192 kHz; buffer sizes 16 to 2048 samples |
| Reported latency | Output 401 samples (8.4 ms); input not opened |
| Playback against the wall clock | 5.003 s in 5.020 s (the reported position trails by up to one 50 Hz update), level exactly as sent (−30.5 dBFS), no xruns |
| MIDI take with a second virtual input | Starts at 0.000 s with both notes, so the late start in [open questions](../open-questions.md#a-second-virtual-midi-input-and-test-recordings) is the simulated device's |
| Unplug, before the fix | The device manager fell back to the MacBook Air Speakers and playback carried on there |
| Unplug, after the fix | Playback stopped, still stopped a second later; project intact; the notice named the M4 and the fallback |
| Plugging back in | Halftrack reopened the M4 by itself with a notice; playback then advanced 2.000 s in 2 s |

## Not yet measured

- **Round-trip latency.** This needs the loopback measurement, with a cable from output 1 to input 3, so measured round trip and recorded placement are still unmeasured.
- **Inputs.** Duplex and input-only operation.
- **Other drivers.** Other sample rates and buffer sizes, aggregate devices, and Windows and Linux interfaces.
