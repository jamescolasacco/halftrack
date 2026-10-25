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
  - **Return Tracks:** the return tracks' columns.
  - **Mixer:** pan, volume with its meter, and the activator, solo and arm switches. On the Main strip, the Solo/Cue switch, the cue volume and the volume.
  - **Track Options:** Track Delay.
  - **Crossfader:** the tracks' A/B assignment and the Main strip's crossfader.
- **The whole mixer.** In the Session View, View > Mixer (Cmd/Ctrl+Option+M) hides and shows it; the clip grid takes its place. The entries' names are the reference manual's (2026-10-24; see [the Arrangement's mixer](#the-arrangements-mixer-2026-10-24)).
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

## The Arrangement's mixer (2026-10-24)

### Evidence

- Live 12 manual, [Arrangement View](https://www.ableton.com/en/live-manual/12/arrangement-view/) section 6.15 and [Mixing](https://www.ableton.com/en/live-manual/12/mixing/) sections 18.1, 18.4 and 18.7, consulted 2026-10-24. The Arrangement View has a mixer at its bottom. View > Mixer, Cmd/Ctrl+Option/Alt+M or the mixer view control in the window's bottom right corner shows it. A menu toggle beside that control shows or hides mixer components. View > Mixer Controls sets which controls the mixer shows. The manual names two of its entries: Return Tracks, and Track Options, which shows Track Delay. Performance Impact indicators, track delays and the crossfader appear only in the mixer.
- The reference's built-in default Set (see above) keeps `MixerInArrangement` off and `ArrangerMixerIO`, `ArrangerMixerSends`, `ArrangerMixerReturns` and `ArrangerMixerVolume` on, with `ArrangerMixerTrackOptions`, `ArrangerMixerCrossFade` and `ArrangerMixerTrackPerformanceImpactMeter` off. Its song preferences (`ASongPrefData`, Live 12.2.1 arm64, constructor 0x102455a08) have the same defaults.
- Static reading of the menu toggles (Live 12.2.1 arm64, `TGlobalMenuCmdHandler::OnToggleSongPrefRemoteable<cmd>`, for example 0x10028cc94 for `mixr`). Each toggle looks up its preference by command in 0x10028c12c and flips it.
  - A shared table holds `fhbr`, `retu`, `aio`, `avol`, `atop`, `aret` and `lock`.
  - Otherwise the table depends on the focused window's view type: 0 for the Arrangement, which also holds `over` → ArrangerShowOverView. In the Arrangement, `mixr`, `io`, `fxsl`, `send`, `vol`, `topt`, `crsf` and `prfi` reach the `ArrangerMixer…` preferences. In the Session they reach `MixerInSession` and the `Session…` preferences.
  - `retu` (Mixer Controls > Return Tracks) is in the shared table at the offset of `SessionReturns`, so it is the Session mixer's in either view. The offsets were mapped to preferences by declaration order. They agree with the `a…` entries and with `over`.
- `LArrangerMixerManager` (0x1037b69c0) subscribes its return visibility to the song preference at +0x228, `ArrangerReturns`, which Arrangement Track Controls > Returns toggles. Nothing in the menu reaches `ArrangerMixerReturns`.
- Navigate > Focus Mixer (`fomx`): its enable callback (0x100289d14) returns true only while the focused view type is the Arrangement. Its action (0x100289cd4) sets `MixerInArrangement` on and then focuses a view.
- The Mixer Controls' FX Slots item (`fxsl`) is added only when a run-time feature flag is registered (see the [menu contract](main-menu-contract.md)), so it is left out.

### Halftrack

- **Showing it.** In the Arrangement, View > Mixer, Cmd/Ctrl+Option+M and the mixer view control show and hide the Arrangement's mixer. In the Session they act on the Session's. `Session::arrangementMixerSections` keeps it with the project as view state, hidden at first.
- **Where.** Under the tracks, the bottom time ruler and the scroll bar, above the detail view. It takes the height its sections need and leaves the tracks at least 160 pixels.
- **Strips.** The Session mixer's strips, side by side in the tracks' order: a group's strip comes before the tracks in it, and a folded group hides them. Each strip sits under a title bar in its track's colour. The Main strip follows the last strip, or keeps the right edge once the strips scroll. A title selects its track (Shift adds it to the selection). A group's title selects the group, and its triangle folds the group. A title's menu is the track's header menu. The controls are the tracks' own, so moving one moves it in the Session's mixer and the Arrangement's headers too.
- **Sections.** View > Mixer Controls shows the sections of the mixer of the view shown. Its entries are In/Out, Sends, Mixer, Track Options (Track Delay), Crossfader and Return Tracks. Return Tracks is the Session mixer's in either view, as the reference's is. The Arrangement's mixer shows the returns when Arrangement Track Controls > Returns does.
- **The mixer view control.** It sits at the right end of the status bar, lit while the mixer of the view shown shows. Its arrow opens the Mixer Controls.
- **Focus Mixer** (Option+Shift+M) is the Arrangement's. It shows the Arrangement's mixer and gives it the keys. There Left and Right select the strip beside the selected one. In the Session it is greyed.
- **Taller faders.** Dragging the top edge of either mixer up makes its faders and meters taller, and dragging it down returns them to their usual height, no shorter; the Session's leaves the clip grid at least a row. Each mixer keeps its own height with the project as view state (`MixerSections::extraVolumeHeight`, up to 600 pixels more), as the reference keeps `ViewStateSessionMixerVolumeSectionHeight` and `ViewStateArrangerMixerVolumeSectionHeight` with its Set (120 at first in its default Set), after its manual's section 18.1.1: "dragging upwards on the top of the mixer will extend the height of the track meters".
- **Column widths.** In the Session, a track's or group's column widens or narrows by dragging its right edge in the title row, from 60 to 400 pixels, the columns after it and the Main column moving along, and its clip slots and mixer strip with it. Each width is kept with its track or group in the project, outside the undo history (`Session::setSessionWidth`), as the reference keeps each track's `ViewStateSessionTrackWidth` (manual section 18.2: tracks' edges drag their width in the Session View).
- **The decibel scale.** A strip at least 150 pixels wide whose meter is at least 120 pixels tall shows a scale beside the meter, marked every 12 dB from 0 to -60 on the meter's own scale, as the manual says a widened track's taller meter gains one (section 18.1.1). Track, group and Main strips show it, in either mixer.
- **Missing.** The Arrangement mixer's own strip widths, Performance Impact and FX Slots.

### Evidence in Halftrack

The track order suite checks:

- that the Arrangement's mixer starts hidden, with In/Out, Sends and Volume shown and Track Delay and the crossfader hidden;
- that showing it and choosing its sections is view state, apart from the Session mixer's;
- that a new project hides it, and a saved choice reopens;
- that both mixers' faders start at their usual height, a taller one than allowed stops at the limit, and each keeps its own;
- that Session widths outside 60 to 400 pixels, or for no track, are refused, a width is kept with the project outside the undo history, and 0 restores the default.

The UI suite checks:

- that View > Mixer and Cmd/Ctrl+Option+M show the mixer in the Arrangement, with a strip for each column, under the tracks and above the detail view, the Session's mixer untouched;
- that its faders are the tracks' own;
- that Track Options shows its Track Delay but not the Session's;
- that Return Tracks stays the Session's, while hiding the Arrangement's returns hides their strips;
- that a title selects its track, and Left and Right step along the strips;
- that the mixer view control hides it and follows the Session's mixer in the Session;
- that Focus Mixer is greyed in the Session, and in the Arrangement shows the mixer;
- that dragging the Session mixer's top edge up makes its strips and faders taller without an undo step, and dragging it far down returns them to their usual height; and that the Arrangement mixer's edge does the same for its own;
- that a column's edge widens it, moving the columns after it and widening its strip, without an undo step, narrows it no further than 60 pixels, and returns to the usual width;
- that a strip at its usual size shows no decibel scale, and widened and made taller shows one.
