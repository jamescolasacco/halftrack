# Audio engine switch contract

Halftrack's Audio Engine On switch follows the reference's Options menu entry of the same name, which turns audio processing off and on (Cmd+Option+Shift+E on macOS).

## Behaviour

- **Off.** `setAudioEngineEnabled (false)` ends any edit gesture and stops the transport, so recordings end with their takes kept and launches are cancelled. It then closes the audio device.
- **While off.** Play, Continue, Record, scene and clip launches and Session recording are refused with "Turn the audio engine on first". Meters fall to silence once they have been read, because no audio arrives.
- **On.** `setAudioEngineEnabled (true)` reopens the last audio device. It fails, with the reason, if that device cannot be reopened. Choosing a device in Audio Settings also turns the engine back on.
- **The UI.** File > Audio Engine On shows the state with a tick and toggles it; Cmd+Option+Shift+E does the same.

## Evidence

`tests/AudioEngineTests.cpp` runs through the synthetic device and checks:

- that turning the engine off while recording, with a held MIDI note sounding, closes the device, stops the transport and keeps the take;
- that meters fall silent, and play, record and launch are refused with the reason;
- that turning the engine on reopens the device with no note left sounding, and playback works as before.

The UI suite toggles the File menu item off and on.

## Not yet covered

The switch is not saved between launches, and turning it off does not release the device for other applications differently from closing it. CPU-load reporting and comparison with the reference's behaviour on hardware are missing.
