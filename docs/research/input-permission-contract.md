# Audio input permission

Status: partial, AUD-008.

macOS keeps each application's permission to use audio inputs (System Settings › Privacy & Security › Microphone). It asks once, the first time an application wants an input. An application without permission still opens the device, but every input delivers silence. Nothing reports that, so a track would record and meter silence.

## Behaviour

**The permission.** `Session::inputPermission()` reports Granted, Undetermined or Denied. On macOS a Session with audio devices reads it from AVFoundation (`src/InputPermission_mac.mm`). On other systems it is granted here; a system that refuses inputs does so when the device opens. A Session without audio devices (as tests build) is always granted it.

**Refused.** A hardware audio input cannot be armed, monitored (In or Auto) or recorded, in the Arrangement, a Session slot or a Session record. The reason says why and what to do: "Halftrack has no permission to use audio inputs, so this input would record silence. Allow Halftrack under System Settings > Privacy & Security > Microphone, then try again."

The following stay allowed:

- disarming and switching monitoring off;
- another track's audio as an input (Audio From);
- MIDI inputs.

**Undetermined.** Arming or monitoring a hardware input asks with the system's prompt and returns at once, saying it is asking. Asking again while the prompt is open does not open another. The answer, arriving later on the message thread, decides the next try. When it is granted, the audio device reopens so its inputs deliver sound.

**Withdrawn.** Permission withdrawn after a track is armed refuses the recording, rather than recording a take of silence. A Session record checks every armed input before any slot starts.

The interface shows the reason in its status line, as it does for other refused edits. Meters stay real: they show what the device delivers.

## Evidence

`tests/InputPermissionTests.cpp` replaces the system's permission and prompt with a stand-in; the real one is never asked. It checks:

- Refused: arming and monitoring are refused with the System Settings explanation; switching monitoring off is allowed; a refused permission is not asked for again; another track's audio arms.
- Undetermined: one prompt only while it is open; a refusal at the prompt is explained on the next try; a later try asks again; a grant arms the track.
- Withdrawn after arming: Arrangement, slot and Session recording refused, no take made, disarming allowed, and arming again once permission is back.

## Not yet covered

- The real prompt, refusal and re-grant on a Mac.
- Whether a device opened before the grant needs the reopen.
- Windows' microphone privacy setting, which refuses when the device opens.
- An indicator on the input chooser while permission is refused.
