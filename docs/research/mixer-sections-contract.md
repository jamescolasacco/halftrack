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
| `MixerInSession` (the whole mixer) | yes |

## Halftrack

- **The sections.** View > Mixer Controls shows and hides six of them; `Session::setMixerSections` sets them. Each strip lays out only the sections shown, and the strips are as tall as those sections need.
  - **In/Out:** the tracks' Audio From or MIDI From chooser and the channel or tap chooser under it ([track inputs](track-inputs-contract.md)), their monitoring as the reference's three switches (In, Auto and Off, one lit), their Audio To chooser, and the Main strip's Cue Out and Main Out in line with the last two.
  - **Sends:** the send knobs.
  - **Returns:** the return tracks' columns.
  - **Mixer:** pan, volume with its meter, and the activator, solo and arm switches. On the Main strip, the Solo/Cue switch, the cue volume and the volume.
  - **Track Delay.**
  - **Crossfader:** the tracks' A/B assignment and the Main strip's crossfader.
- **The whole mixer.** View > Mixer (Cmd/Ctrl+Option+M in the Session View) hides and shows it; the clip grid takes its place.
- **Defaults and storage.** The defaults are the reference's. The choice is kept with the project as view state, outside the undo history.
- **Missing.** The Performance Impact section is not offered.

## Evidence

`tests/TrackOrderTests.cpp` checks the defaults, that a choice marks the project changed without an undo step, that a new project has the defaults, and that a saved choice reopens.

The UI suite checks:

- that Track Delay and the crossfader are hidden at first;
- that View > Mixer Controls shows them, the strips growing by 46 pixels;
- that hiding In/Out hides the routing choosers and shortens the strips by 65 pixels;
- that showing In/Out again brings them back;
- that hiding the Returns removes the return's column, and showing them brings it back;
- that Cmd/Ctrl+Option+M hides the whole mixer and shows it again, leaving MIDI Map Mode alone.

## Arrangement Track Controls (2026-10-24)

The reference's View menu has an Arrangement Track Controls submenu (In/Out, Volume, Track Options, then Returns: `aio`, `avol`, `atop`, `aret`), and its default Set keeps them as ArrangerIO, ArrangerVolume, ArrangerTrackOptions and ArrangerReturns: In/Out, Volume and Returns on, Track Options off. Halftrack's `Session::ArrangementControls` keeps them with the project as view state, outside the undo history.
- **In/Out** shows the Arrangement headers' input and monitor (a return's send position).
- **Volume** shows their volume and pan.
- **Track Options** shows Track Delay under them, on a header tall enough.
- **Returns** shows the return tracks in the Arrangement; hidden, their lanes take no height.
- In the Arrangement, Option+Cmd+I and Option+Cmd+R toggle In/Out and Returns, as the reference's keys there do; in the Session they stay the mixer's.

The track order suite saves and reopens chosen controls; the UI suite hides and shows each part of a header and the returns.

