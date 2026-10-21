# Exclusive solo and arm contract

Solo and arm are exclusive by default, as in the reference's Exclusive Solo and Exclusive Arm preferences ([Mixing](https://www.ableton.com/en/live-manual/12/mixing/), consulted 2026-10-20). This is interface behaviour over the existing `setTrackSolo`, `setGroupSolo` and `setTrackArmed` commands.

## Behaviour

- Clicking a track's or group's Solo button solos it and releases every other soloed track and group. Cmd/Ctrl-click adds to the solos instead.
- Clicking a track's Arm button arms it and disarms every other armed track. Cmd/Ctrl-click arms an additional track.
- Turning a solo or arm off affects only that track or group.
- Each click is one undo step; for solo, that step includes the releases.

## Evidence

The UI suite checks:

- a second solo releasing the first;
- Cmd/Ctrl-click adding a solo;
- undo removing the added solo, then restoring the first solo in one step;
- arming a MIDI track disarming an audio track, each on its own input;
- Cmd/Ctrl-click arming both.

## Not implemented

- Preferences to turn exclusivity off.
- Cue-output solo.
- Solo-in-place versus pre-listening choices.
