# Exclusive solo and arm contract

Solo and arm are exclusive by default, as in the reference's Exclusive Solo and Exclusive Arm preferences ([Mixing](https://www.ableton.com/en/live-manual/12/mixing/), consulted 2026-10-20). This is interface behaviour over the existing `setTrackSolo`, `setGroupSolo` and `setTrackArmed` commands.

## Behaviour

- Clicking a track's or group's Solo button solos it and releases every other soloed track and group. Cmd/Ctrl-click adds to the solos instead.
- Clicking a track's Arm button arms it and disarms every other armed track. Cmd/Ctrl-click arms an additional track.
- Turning a solo or arm off affects only that track or group.
- Neither solo nor arm is an undo step, as in the reference (static reading of its code: its arm and solo values carry the flag that keeps them out of the undo history; findings in the ignored `artifacts/reverse-engineering/tracks/`). Undo passes over them to the edit before. They still mark the project changed and are saved with it.

## Evidence

The UI suite checks:

- a second solo releasing the first;
- Cmd/Ctrl-click adding a solo;
- undo passing over the solos to take back the track added before them;
- arming a MIDI track disarming an audio track, each on its own input;
- Cmd/Ctrl-click arming both.

## Related

- The Preferences' Exclusive Arm and Exclusive Solo switches turn exclusivity off, swapping the plain and Cmd/Ctrl clicks ([preferences](preferences-contract.md)).
- Cue mode and Solo in Place are in the [cue contract](cue-contract.md).
