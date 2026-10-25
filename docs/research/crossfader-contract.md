# Crossfader contract

The crossfader fades between the tracks assigned to its sides, A and B, along one of the reference's seven curves.

## Evidence

- Live 12 manual, [Mixing](https://www.ableton.com/en/live-manual/12/mixing/), section 18.5, consulted 2026-10-25: "Seven different crossfade curves are available"; to change the curve, right-click on the crossfader and select an entry from the context menu.
- The reference's strings (Live 12.2.1 arm64, read 2026-10-25) name the curves in this order: Dipped, Intermediate, Constant Power, Slow Fade, Slow Cut, Fast Cut, Transition.
- Its built-in default Set (see the [mixer sections contract](mixer-sections-contract.md)) keeps `<CrossfadeCurve Value="2" />` at the Set's level: the third, Constant Power.
- A static reading of its crossfade code (2026-10-14, in the ignored `artifacts/reverse-engineering/reference-12.2.1/semantic-triage-crossfader-followup-20261014/`) gives the curves exactly. Their internal names, Normal Dip, Slight Dip, Constant Power, Dipless, Slow Cut, Fast Cut and Transition, are in the menu's order. Its constructor's default is the third.
  - The first four saturate past ±0.95. Between, with y = x/0.95: Normal Dip is cos((y+1)π/4) − 0.2071 cos(yπ/2), Slight Dip the same with 0.125, Constant Power cos((y+1)π/4), and Dipless 1 until the centre, then cos(yπ/2). Each is multiplied past y = 0.55 by a shoulder, ½(cos((y−0.55)/0.45·π)+1).
  - Slow Cut saturates past ±0.94: 1 until y = 0.5, then cos((y−0.5)π), multiplied past y = 0.75 by ½(cos(4(y−0.75)π)+1).
  - Fast Cut saturates past ±0.96: 1 until y = 0.9375, then cos((y−0.9375)·8π).
  - Transition is 1 until the centre, then 1 − x.
  - Each curve is sampled into a table of 131 single-precision values at x = −1 + i/64. A gain is read between the two table values round its position along a straight line.
  - A side's position is the crossfader's, seen from that side.
  - Transition's value multiplies the side's fader position before its volume law; the others' multiply its gain after the law.

## Halftrack

- **The curves.** `Session::crossfadeGain` samples the reference's seven curves into tables as it does: single precision, its constants, its fused multiply-adds, and the same straight-line reading. In the track mixer, Transition's value scales the fader position before the volume law; the others scale the gain, ramped across each block as before.
- **Choosing one.** Right-clicking the crossfader on either mixer's Main strip opens a menu of the seven, the project's ticked. Choosing one is one undo step, and the curve is kept with the project (`halftrackCrossfadeCurve`, 0 to 6). A new project, and a project saved before the curve was kept, take Constant Power. An unknown curve in a project file is refused before the open project is replaced.
- **Not followed.** The reference zeroes a side's gain at or below 0.00032 after its curve and ramps an assignment change over a short time. Its menu order is taken to be its table order (both lists agree, and its default is the third in each). Nothing has been measured against the reference's output.

## Evidence in Halftrack

`tests/CrossfadeTests.cpp` checks:

- that each side is full at its end and silent at the other, and an unassigned track ignores the fader;
- Constant Power's 0.7071 at the centre and its value at table positions, worked out afresh in double;
- a position between table entries read along the line between them, which differs from the curve worked out afresh;
- each curve's centre: 0.5, 0.5821, 0.7071 and then 1;
- Transition's straight line and Fast Cut's late fall;
- a new project's Constant Power, renders at the centre, at both ends and halfway along Constant Power, Slow Fade's render, and Transition's render halving side A's fader position;
- that an unknown curve is refused, undo and redo, the curve saved and reopened and playing as before, and an unknown curve in a file refused while keeping the open project.

The UI suite checks that the crossfader's right-click menu lists the seven curves with Constant Power ticked, that choosing Dipped sets it, and that undo returns to Constant Power.
