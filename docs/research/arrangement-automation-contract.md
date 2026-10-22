# Arrangement automation contract

Halftrack's Automation Mode follows the reference's [automation chapter](https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/), consulted 2026-10-21.

## Behaviour

- **The mode.** The Automation Mode button above the track headers, or **A** outside text fields, toggles it.
- **Envelopes.** Each Arrangement track's lane shows one envelope over its clips, which are dimmed; the default is the mixer's Track Volume.
- **The header choosers.** In Automation Mode the track header's routing and mixer lines give way to a Device chooser and a Control chooser.
  - The Device chooser offers Mixer, each native device (racks' inner devices and macros included), each loaded plug-in, and None.
  - The Control chooser offers that device's controls.
  - A dot marks devices and controls with automation.
- **Lanes.** The lane button (+) moves the envelope into a lane below the track, with a header naming it and a button to hide it; the main lane then shows None. Hiding a lane keeps its automation. Several lanes can be shown.
- **Breakpoints:**
  - clicking on the envelope line adds a breakpoint under the pointer, snapped to the grid unless Alt or Cmd is held;
  - dragging a breakpoint moves it between its neighbours, and Shift gives a finer vertical step;
  - clicking a breakpoint without dragging deletes it;
  - double-clicking empty space adds a breakpoint there.

  Each gesture is one undo step. A lane without breakpoints shows its manual value; a switched-off lane is drawn grey.
- **Writing.** `arrangementAutomation::write` uses `setTrackAutomation` for the mixer and `setDeviceAutomation` for device controls and plug-in parameters.

## Evidence

The main UI suite turns Automation Mode on with A and checks:

- the header's Mixer and Track Volume choosers, with the volume fader hidden;
- that clicking the 0 dB line adds a breakpoint at beat 2;
- that dragging it moves it down, and that one undo restores it;
- that clicking it deletes it;
- that double-clicking empty space at beat 4 adds a high breakpoint;
- that the lane button moves the envelope into a named lane, that edits there change the same automation, and that hiding the lane keeps it;
- that A again restores the mixer controls.

## Curved segments (2026-10-22)

The reference's manual: hold Alt (Win) / Option (Mac) and drag a line segment to curve it; Alt/Option-double-click straightens it. Halftrack does the same in Automation Mode's envelopes and lanes.

- **Model.** `Session::AutomationPoint::curve` holds the shape of the segment to the next point, as the engine's automation curve does: 0 is straight, ±0.5 bows it, and up to ±1 bows it further with its ends held.
  - It is read and written for track volume and pan, master volume, native device controls and plug-in parameters, and is validated.
  - Time edits and the range clipboard already carried it.
- **Drawing and hit tests.** These follow `Session::curveProportion`, the curve as the engine plays it (`AutomationIterator`). The engine's `AutomationCurve::getValueAt` doubles the curve before the Bézier, but that is not what plays, so the playback form is the one drawn.
- **The gesture.** Dragging a segment's middle up raises it, either way the segment slopes. Snapping is bypassed with Cmd on the Mac and Alt elsewhere, as in the Arrangement.

Evidence:

- **The automation suite.** It renders a constant source through a volume segment from -24 to 0 dB over three beats, straight and with curve 0.4. Halfway along, the curve plays 0.214 of the plateau, against 0.218 predicted from `curveProportion`. The straight segment plays 0.366, against 0.372 predicted; the 2% gaps match block-rate automation. The curve reads back, a curve beyond 1 is refused, and one undo straightens it.
- **The UI suite.** Option-dragging a rising segment up curves it, raising its middle without adding a breakpoint; one undo straightens it, and Option-double-clicking straightens it.

The reference's curve shape is unmeasured: Halftrack's is the engine's quadratic Bézier.

Missing: segment selection and dragging, multi-breakpoint selection, Edit Value, Draw Mode, saving the shown lanes, clip envelopes, and fine checks of the chooser's per-device "show automated only" filter.
