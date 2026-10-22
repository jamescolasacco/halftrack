# Mixer sections contract

The reference's View menu shows and hides the Session mixer's sections, and its Set keeps the choice. Its built-in default Set (sha256 aeec3f07599da0a61cc86fd9ddeb78a5ce61fe473f676d50c1ae3e281c289a1f, read 2026-10-22) has these view states:

| Section | Shown at first |
| --- | --- |
| `SessionIO` | yes |
| `SessionSends` | yes |
| `SessionReturns` | yes |
| `SessionVolume` | yes |
| `SessionTrackOptions` (Track Delay) | no |
| `SessionCrossFade` | no |
| `SessionTrackPerformanceImpactMeter` | no |

## Halftrack

- **The sections.** View > Mixer Controls shows and hides six of them; `Session::setMixerSections` sets them. Each strip lays out only the sections shown, and the strips are as tall as those sections need.
  - **In/Out:** the tracks' input, monitoring and Audio To choosers, and the Main strip's Cue Out and Main Out.
  - **Sends:** the send knobs.
  - **Returns:** the return tracks' columns.
  - **Mixer:** pan, volume with its meter, and the activator, solo and arm switches. On the Main strip, the Solo/Cue switch, the cue volume and the volume.
  - **Track Delay.**
  - **Crossfader:** the tracks' A/B assignment and the Main strip's crossfader.
- **Defaults and storage.** The defaults are the reference's. The choice is kept with the project as view state, outside the undo history.
- **Missing.** The Performance Impact section is not offered.

## Evidence

`tests/TrackOrderTests.cpp` checks the defaults, that a choice marks the project changed without an undo step, that a new project has the defaults, and that a saved choice reopens.

The UI suite checks:

- that Track Delay and the crossfader are hidden at first;
- that View > Mixer Controls shows them, the strips growing by 46 pixels;
- that hiding In/Out hides the routing choosers and shortens the strips by 65 pixels;
- that showing In/Out again brings them back;
- that hiding the Returns removes the return's column, and showing them brings it back.
