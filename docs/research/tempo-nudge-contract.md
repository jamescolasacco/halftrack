# Tempo nudge contract

The Control Bar's Nudge Down and Nudge Up buttons follow a static reading of the reference's code (Live 12.2.1, the "TempoNudge" processor 0x101f13288–0x101f19528 and its scheduler input, 2026-10-23). Nothing was measured from the reference. The reference's manual describes the buttons as temporarily slowing down or speeding up playback, to match another source.

## Behaviour

- **Momentary.** Each button is held, not toggled; a click is a short hold. `Session::setNudge(up, held)` reports a press or release. `Snapshot::nudgeDownHeld` and `nudgeUpHeld` hold the buttons' states, as the reference's transport keeps "Phase nudge down" and "Phase nudge up".
- **The ramp.** While one is held alone during playback, playback's tempo ramps away from the song tempo by up to 70% of it over six seconds: the tempo map's points times 1 ± 0.7 × min(t, 6 s) / 6 s, clamped to 20–999 BPM. t counts the audio stream's time since the press. At 120 BPM that is 14 BPM a second, up to 204 BPM or down to 36 BPM.
- **Steps.** The tempo changes on 16th notes only, from the first after the press to the first after the release, then every 16th while the ramp runs. A release puts the song tempo back at the next 16th, and the song keeps the beats it gained or lost.
- **Both held** nudge nothing. Letting go of one leaves the other nudging, its ramp starting again from 0.
- **The song tempo stays.** The tempo field, `Snapshot::tempo` and `tempoMap` keep showing it, and the project, its saved state and its history do not change: the tempo points are moved outside history and put back. `Snapshot::nudging` says which way playback is nudged.
- **Mapping.** Key and MIDI Map Modes map the buttons as `transport:nudgedown` and `transport:nudgeup`, as the reference maps "Phase nudge down" and "Phase nudge up" in its Transport group. A mapped key holds the button until it is let go; a note does so until its note-off, and a controller while it is above 63.
- **Not while stopped**, nor while following external sync (EXT): the reference's scheduler ignores the nudged tempo when External Sync is on.
- **Saving, rendering, undo and tempo edits let go first.** Before saving, writing recovery, exporting, undo and redo, a tempo or tempo-map edit, tap tempo, switching EXT, or opening or starting a project, Halftrack puts the song tempo back and releases the nudge. In the reference only the scheduler's tempo moves; Halftrack's engine plays the tempo map, which these act on.

Warped audio and MIDI follow the nudged tempo, and unwarped audio keeps its speed, because the nudge changes the tempo rather than the playback rate. Rack devices' automation keeps its beats, as for the external-sync tempo.

## Evidence

`tests/TempoTests.cpp` plays at 120 BPM through the synthetic device, with 256-sample blocks at 48 kHz:
- three seconds of Nudge Up gain 7.0 beats against six, with the song tempo still 120 BPM;
- after the release it plays two seconds at 120 BPM (3.97 beats), with the project clean and no undo step;
- both held nudge nothing, and letting go of Up leaves Down nudging;
- stopped, Nudge Down changes nothing.

The UI suite holds and lets go both Control Bar buttons and checks the Session sees them. The key-map suite holds Nudge Up with a mapped key and lets it go; MIDI notes and controllers go through the same press and release, untested for the nudge.

## Not implemented

- Showing the nudged tempo (the reference shows it only with a debug option).
- Link, which Halftrack does not have.
