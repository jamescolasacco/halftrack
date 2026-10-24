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

- **The automation suite.** It renders a constant source through a volume segment from -24 to 0 dB over three beats, straight and with curve 0.4. Halfway along, the curve plays 0.214 of the plateau, against 0.218 predicted from `curveProportion`. The straight segment plays 0.366, against 0.372 predicted; the 2% gaps match block-rate automation. The curve reads back, a curve beyond 1 is refused, and one undo straightens it. A saved project reopens with the curve and plays it as before; loading refused curved volume and pan points until 2026-10-23.
- **The UI suite.** Option-dragging a rising segment up curves it, raising its middle without adding a breakpoint; one undo straightens it, and Option-double-clicking straightens it.

The reference's curve shape is unmeasured: Halftrack's is the engine's quadratic Bézier.

## Selections, segment drags and Simplify Envelope (2026-10-22)

From the reference's manual:

- **Moving a selection.** Dragging in an envelope's background makes a time selection, and dragging a breakpoint inside it moves every breakpoint in the selection. In Halftrack the breakpoints move by the same beats and the same 0-1 proportion, held between the breakpoints outside the selection. An automation lane's background now selects time on its track.
- **Moving a segment.** Shift-dragging a segment moves it: both its ends move up or down with the pointer, and no breakpoint is added.
- **Simplify Envelope.** The envelope's context menu offers it over the time selection; it keeps only the breakpoints the shape needs, as one undo step. The reference's algorithm is not described. Halftrack runs a Ramer-Douglas-Peucker pass over the lane's 0-1 proportions: a breakpoint is kept when it lies more than 0.005 from the straight line through the breakpoints kept around it. The selection's ends are always kept.

Evidence, from the UI suite:

- a drag across an automation lane's background selects that time on its track;
- dragging one of two selected breakpoints moves both by the same proportion while the others stay;
- Shift-dragging a segment lowers both its ends;
- simplifying five breakpoints on one line and a sixth off it keeps three;
- one undo restores them.

Missing: segment selection without Shift (the reference also selects a segment by clicking near it), multi-breakpoint selection, Edit Value, Draw Mode, saving the shown lanes, clip envelopes, and fine checks of the chooser's per-device "show automated only" filter.

## Send lanes (2026-10-23)

The reference automates sends like any mixer control: its Send parameter takes the same automation settings as volume and pan (static reading, see the [mixer laws](mixer-laws-contract.md)). Halftrack's Mixer device offers a send lane for each return, after Track Volume and Track Panning.

- **Model.** `Session::setSendAutomation (trackId, returnTrackId, lane)` takes beat points in decibels from −100 (Off) to 0. `SendInfo::automation` reports them. Between points the send's knob moves straight along the send law, curved as other lanes are. Points on a send that is Off make the send, at Off, in the same undo step. Edits are refused on returns, on frozen tracks, while recording (except automation recording) and while exporting.
- **The level set by hand.** The engine plays automation into the send's stored level, so Session keeps the level set by hand as `halftrackManualSend`. Clearing the lane returns the send to that level, and saving keeps it. On load, a send's lane must hold at most 4096 ordered finite points, from Off to 0 dB, with curves from −1 to 1. The level set by hand must be in range too. Otherwise the project is refused and the open one is kept.
- **Override and capture.** Turning an automated send's knob, by mouse, MIDI or a key mapping, switches its lane off until Re-Enable Automation, as for the mixer. With Automation Arm on, recording writes the knob's moves into the lane. That includes a send that was Off, which the first move makes.
- **Mapping.** A send is a MIDI and key mapping target (`send:<track>:<return>`), moving from Off to 0 dB along the send law; CC 64 is −19.84 dB.
- **Interface.** In Automation Mode the Mixer's Control chooser names each send for its return (for example A-Echo); its envelope draws and edits along the send law. The Session mixer's send knobs and the Sends panel's sliders carry their mapping targets for MIDI and Key Map Mode.

Evidence:

- **The sends suite.** It renders a send rising from Off at beat 0 to 0 dB at beat 4, within 0.3% of the send law at three times (−20 dB halfway). It also checks:
  - one undo removing the lane and the send it made, and redo;
  - a −20 dB lane playing over a −6 dB level set by hand;
  - a knob move overriding the lane, and the render following the knob;
  - Re-Enable Automation;
  - refusals above 0 dB and from a return;
  - a reopen keeping the lane and the level set by hand;
  - three malformed saved lanes refused.
- **The automation capture suite.** A send that was Off is recorded at −12 dB from its first move while recording, and undo turns it Off again.
- **The MIDI mapping suite.** A mapped send's range is Off to 0 dB. CC 64 gives −19.84 dB, 127 gives 0 dB, and 0 gives Off.
- **The UI suite.** The Mixer's controls offer the send named for its return. Double-clicking halfway up its lane adds a −20 dB breakpoint, one undo removes it, and the Session send knob is a mapping target.

Not read in the reference: how its choosers name a send (Halftrack uses the return's name), and whether it allows mixer automation edits on a frozen track (Halftrack refuses them, as for volume and pan).

## Clear All Envelopes (2026-10-23)

The track header menu's Clear All Envelopes, after a static reading of the reference's (0x1037d8af4, enabled 0x1037f18b4): `clearAllEnvelopes(ids)` removes the selected tracks' and groups' own Arrangement automation, their mixers', sends' and devices' (rack devices included; a frozen track's mixer only), and their Arrangement clips' envelopes for the same controls, in one undo step. Tracks inside a selected group and Session clips keep theirs. `canClearAllEnvelopes(ids)` says whether there is anything to clear. Checked in the groups and UI suites ([menu](track-header-menu-contract.md)).

## Draw Mode (2026-10-24)

After the reference's manual ([Automation and Editing Envelopes](https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/), consulted 2026-10-24), in Draw Mode (B; see the [note drawing contract](note-drawing-contract.md)) a drag in an envelope draws instead of editing breakpoints:
- **Steps.** Each cell of the Arrangement grid the pointer passes holds the value drawn on it, a step as wide as the grid. Cells it skips take values between the two positions, and passing a cell again draws it again.
- **Freehand.** With the grid's snapping off, or Cmd held (Alt elsewhere), each position gives a breakpoint, replacing those drawn between it and the last.
- **The rest of the lane.** Outside the drawing the envelope keeps its shape: a breakpoint a 960th of a beat before the drawing and one at its end hold the old values there. A lane without breakpoints keeps the control's own value outside it.
- **Undo.** One drag is one undo step, and a double-click draws nothing more.

`arrangementAutomation::Drawing` holds a drawing over a lane's original breakpoints; clip envelopes use it too. The UI suite draws two quarter-note steps over a rising volume segment and checks the eight breakpoints, the line's old values on both sides, one undo, and freehand breakpoints with Cmd.

Not yet: Shift's finer steps, and holding B to switch Draw Mode for the moment.

