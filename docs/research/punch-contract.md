# Punch-In and Punch-Out contract

Halftrack follows the reference's Punch-In and Punch-Out switches, described in the manual's [Recording New Clips](https://www.ableton.com/en/live-manual/12/recording-new-clips/) chapter (section 19.3.1), consulted 2026-10-21. The punch points are the Arrangement Loop's start and end, whether or not the loop plays:

- with Punch-In on, nothing is recorded before the loop start;
- with Punch-Out on, recording stops at the loop end.

## Behaviour

- **Switches.** `setPunchIn(bool)` and `setPunchOut(bool)` are transport settings saved with the project, outside the undo history. `Snapshot::punchIn` and `Snapshot::punchOut` report them. They cannot change while recording.
- **Trimming at stop.** When Arrangement recording stops, the clips that recording created are trimmed to the punch range as part of the take's undo step:
  - an audio take keeps its timing, because its source offset moves with its start;
  - a MIDI take is split at the punch points with the range edits' MIDI split, so it keeps a zero offset and loses the notes outside;
  - a take wholly outside the range is removed.
  Clips that existed before the recording are untouched.
- **Automation.** Automation capture ignores moves before the loop start and ends a pass at the loop end.
- **Engine.** Tracktion's own punch mode (punch in and out together) is not used. Halftrack records the whole pass and trims it, so playback before and after the brace is unchanged.

## Interface

In the control bar, In sits before the loop start field and Out after the loop length field. Both are orange when on.

## Evidence

`tests/PunchTests.cpp` (`punch-acceptance`) records the synthetic device's input from 0 to about 3 s with the brace at 1–2 s. It checks:

- **Audio.** A take without punching covers the pass. Punch-In keeps it from 1 s with its source offset moved by the same amount. Both switches keep exactly 1–2 s, and Punch-Out alone ends it at 2 s.
- **Short pass.** A pass stopped at 0.8 s keeps nothing.
- **Undo and locking.** One undo removes a punched take, and the switches are locked while recording.
- **MIDI.** A virtual controller's notes at 0.5, 1.5 and 2.5 s leave one clip covering the brace, holding the middle note at beat 1.
- **Automation.** Captured automation at 0.5, 1.5 and 2.5 s records only the middle move and ends at the loop end.

The UI suite checks the buttons' placement and toggling.

## Not implemented

- Pre-roll and count-in tied to the punch point (recording starts where playback starts).
- The "only while recording" metronome sounding only after the punch-in point.
- Session View punching.
- Take lanes for loop recording (REC-010).
- Measured comparison with the reference.
