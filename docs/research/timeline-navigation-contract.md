# Timeline navigation contract

The Arrangement and piano roll follow the playhead and zoom to selections, after the reference's Follow switch and zoom commands ([Arrangement View: Navigation](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-20). This is Halftrack's own interface behaviour; paging versus continuous scrolling is a simplest default, not a measurement.

## Behaviour

- **Follow** (control bar button, Playback > Playback Options > Follow, Option+Shift+F as in the reference) is on by default. While playing, a playhead that leaves the visible time turns the view to the page that starts at the playhead.
  - The Arrangement follows the global playhead, and the piano roll follows its clip's playhead (the launched slot's position for Session clips).
  - **Pausing** (2026-10-25). The reference's manual (section 6.2, consulted 2026-10-25): "Follow will pause if you make an edit or scroll the view horizontally in the Arrangement, or if you click on the beat-time ruler", and starts again "once you stop or restart playback, or click in the Arrangement or clip scrub area". The reference's strings name both kinds of pause, "during the mouse gesture" and "until playback is resumed" (static reading). In Halftrack, while playing:
    - A press in the Arrangement's lanes pauses Follow while the gesture lasts. At its release Follow goes on, unless the gesture made an edit (the undo history changed) or scrolled the view.
    - A hand scroll (the scroll bar, a horizontal or Shift scroll, a Cmd+Option drag, a drag past the edge, the overview, the bar numbers or the time ruler, and the note editor's scroll bar, Cmd+Page keys and Cmd+Option drag), an edit made with the mouse in the Arrangement, or a press on the bar numbers pauses Follow until playback stops or starts again, or a click in the Arrangement's lanes, its scrub area or the note editor's ruler.
    - Paused, the Follow switch stays on, dimmed to half. Switching Follow off and on, or Play from a playing transport, ends the pause.
    - The reference's look for the paused switch, and whether its keyboard edits pause Follow, were not read; Halftrack's keyboard edits leave it alone.
- **Zoom**:
  - + and − zoom the Arrangement or the open piano roll, and Cmd/Ctrl+scroll zooms the Arrangement.
  - Z zooms the Arrangement to the time selection (or the selected clip), with 5% margins.
  - X returns to the view from before the last Z.
- **Panning** (2026-10-25). The reference's manual pans the Arrangement display by dragging with Cmd+Option held (Ctrl+Alt elsewhere; [Arrangement View](https://www.ableton.com/en/live-manual/12/arrangement-view/), section 6.2), and its key list gives the drag as Pan Left/Right of Selection and Scroll Display Left/Right of Selection (sections 42.9 and 42.16, consulted 2026-10-25).
  - In Halftrack's Arrangement lanes a Cmd+Option drag moves the view with the pointer, in time and down the tracks, and selects and changes nothing. In the note editor's grid it moves the view in beats and keys, as far as the editor scrolls.
  - With Shift as well, the drag slides a clip's contents off the grid instead ([selection contract](arrangement-selection-contract.md)), as the reference's manual has it (section 6.7).
  - The reference's pointer for the drag, and the drag in the Session view, were not read.
- **The overview** (2026-10-24). The reference's built-in default Set (inside its app bundle, read statically) shows the Arrangement's overview (`ArrangerShowOverView` 1) and not the Session's (`SessionShowOverView` 0). Halftrack's Arrangement shows one above the ruler, 20 pixels high:
  - It spans the clips, the loop and the time shown, each ordinary track a row of its clips in their colours, deactivated clips grey, with an outline round the time the Arrangement shows.
  - A press outside the outline centres the view on that time. A drag across moves the view with the outline, and dragging down or up zooms in or out about the pressed time, 1% a pixel. The reference's directions and rates were not read: Halftrack takes down as zooming in, as the reference's beat ruler does by its manual.
  - A press while playing pauses Follow, as a hand scroll does.
  - A double-click inside the outline shows the whole Arrangement (as the fit button does), as the reference's manual says ("To zoom out to the full Arrangement, double-click anywhere within the black outline", section 6.1, consulted 2026-10-25).
  - View > Overview (Option+Cmd+O) hides and shows it. The choice is kept with the project as view state, outside the undo history (`Session::setArrangementOverview`).
- **The time ruler** (2026-10-24). The reference's Arrangement view takes Options > Time Ruler Format's six commands (`LArrangerView::OnRulerNormalTime`, `OnRulerSmpte24`, `OnRulerSmpte25`, `OnRulerSmpte30NonDrop`, `OnRulerSmpte30Drop`, `OnRulerSmpte29NonDrop`), and its Set keeps the choice (`SmpteFormat`, 0 in the default Set). Halftrack's Arrangement shows a time ruler under the tracks, 16 pixels high:
  - Minutes and seconds ("1:15"), with milliseconds once the labels step by less than a second ("1:01:500"); or SMPTE hours, minutes, seconds and frames at 24, 25 or 30 frames a second, 30 drop-frame (29.97 frames a second, skipping frames 0 and 1 of each minute but every tenth, the frames after a semicolon), or 29.97 without dropping.
  - Labels step by the first of the format's steps at least 64 pixels long: clock steps from a millisecond to an hour, or for SMPTE 1, 2, 5 or 10 frames, then whole seconds.
  - Options > Time Ruler Format chooses the format, kept with the project as view state (`Session::setTimeRulerFormat`). The reference's label text was not read.
  - Dragging it scrolls the Arrangement, the pressed time staying under the pointer, without zooming, as the reference's manual has its time ruler do (section 6.1, consulted 2026-10-25). A press while playing pauses Follow.
- **The beat-time ruler** (2026-10-25). The reference's manual (sections 6.1 and 6.2, consulted 2026-10-25) says clicking and dragging its beat-time ruler works as in the overview, scrolling across and zooming up and down; double-clicking zooms to the current selection, or with nothing selected out to the whole Arrangement; and clicking it pauses Follow. Playback is launched from the scrub area below it instead. In Halftrack's Arrangement ruler the bar numbers are the beat-time ruler:
  - A drag keeps the pressed time under the pointer: across, it scrolls; down zooms in and up zooms out, 1% a pixel as in the overview. The reference's rate and the direction of its scroll were not read.
  - A double-click zooms to the time selection or the selected clip, as Z does, and with neither shows the whole Arrangement.
  - A press neither locates nor selects; while playing it pauses Follow. Shift-dragging still draws a loop.
  - The locators' row below the bar numbers (and the loop brace's row above them) locates as before ([transport contract](transport-contract.md)).
- **Scrolling during a drag** (2026-10-24). Halftrack's Arrangement scrolls while a drag reaches its edge, at speeds of its own. The reference's drag scrolling was neither read nor measured.
  - Dragging a clip, a clip's edge or fade, a time selection or a take lane's selection to within 16 pixels of the timeline's left or right edge, or past it, scrolls the time. Dragging those, or a track's or group's header, to the top or bottom of the lanes shown scrolls the lanes.
  - The speed grows with the distance into the border, from 60 to 2400 pixels a second, and scrolling goes on while the pointer rests there.
  - A dragged clip or edge keeps its place under the pointer as the time scrolls, and a time selection runs to the time under the pointer.
  - A scroll while playing pauses Follow, as other hand scrolls do. Envelope drags do not scroll.

## Evidence

The UI suite checks:

- Follow's default;
- the paging rule for visible, later and earlier playheads;
- the Arrangement turning to a page one and a half views ahead while playing;
- a hand scroll pausing Follow with its switch on and dimmed, the view then staying put; a press in the Arrangement pausing it for the gesture and the click resuming it, the view turning to the playhead; a clip moved by dragging pausing it until resumed; a click in the scrub area resuming it and a press on the bar numbers pausing it; and a stop resuming it;
- Option+Shift+F switching it off and on again;
- the time ruler's labels in each format, its steps, its place under the tracks and Options > Time Ruler Format switching it (the track order suite keeps the format with the project);
- the overview shown at first, hidden by Option+Cmd+O and shown from View, a press outside its outline centring the view, a drag across moving it and a drag down zooming in about the pressed time (the track order suite keeps the choice with the project);
- Z fitting a two-second selection and X restoring the exact previous view;
- a drag on the bar numbers taking the pressed time along with the pointer without locating, then zooming in with the time kept under the pointer; a double-click on them zooming to a time selection, and without one showing the whole Arrangement; a drag on the time ruler scrolling without zooming; and a double-click inside the overview's outline showing the whole Arrangement;
- a Cmd+Option drag in the Arrangement moving the view later by the distance dragged without selecting, and one in the zoomed note editor moving the view two beats later and two keys higher without changing a note;
- the piano roll paging to its playhead at beat 24 of a 64-beat clip;
- a clip dragged past the timeline's right edge scrolling the time on and landing under the pointer, and one undo putting it back;
- a time selection dragged past the edge scrolling on and running to the time under the pointer;
- a time selection, and a track's header, dragged below the lanes shown scrolling them down.

## Not implemented

- Continuous (non-paging) follow.
- Vertical track-height zoom and Optimize Height/Width.
- Follow in the Session view.
- Any comparison with reference behaviour.
