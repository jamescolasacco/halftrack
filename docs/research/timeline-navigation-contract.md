# Timeline navigation contract

The Arrangement and piano roll follow the playhead and zoom to selections, after the reference's Follow switch and zoom commands ([Arrangement View: Navigation](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-20). This is Halftrack's own interface behaviour; paging versus continuous scrolling is a simplest default, not a measurement.

## Behaviour

- **Follow** (control bar button, Playback > Playback Options > Follow, Option+Shift+F as in the reference) is on by default. While playing, a playhead that leaves the visible time turns the view to the page that starts at the playhead.
  - The Arrangement follows the global playhead, and the piano roll follows its clip's playhead (the launched slot's position for Session clips).
  - Scrolling by hand while playing, with the scroll bar or a horizontal or Shift scroll, turns Follow off, as the button shows.
- **Zoom**:
  - + and − zoom the Arrangement or the open piano roll, and Cmd/Ctrl+scroll zooms the Arrangement.
  - Z zooms the Arrangement to the time selection (or the selected clip), with 5% margins.
  - X returns to the view from before the last Z.
- **Scrolling during a drag** (2026-10-24). Halftrack's Arrangement scrolls while a drag reaches its edge, at speeds of its own. The reference's drag scrolling was neither read nor measured.
  - Dragging a clip, a clip's edge or fade, a time selection or a take lane's selection to within 16 pixels of the timeline's left or right edge, or past it, scrolls the time. Dragging those, or a track's or group's header, to the top or bottom of the lanes shown scrolls the lanes.
  - The speed grows with the distance into the border, from 60 to 2400 pixels a second, and scrolling goes on while the pointer rests there.
  - A dragged clip or edge keeps its place under the pointer as the time scrolls, and a time selection runs to the time under the pointer.
  - A scroll while playing turns Follow off, as other hand scrolls do. Envelope drags do not scroll.

## Evidence

The UI suite checks:

- Follow's default;
- the paging rule for visible, later and earlier playheads;
- the Arrangement turning to a page one and a half views ahead while playing;
- a hand scroll turning Follow off, with the view then staying put;
- Option+Shift+F turning it back on;
- Z fitting a two-second selection and X restoring the exact previous view;
- the piano roll paging to its playhead at beat 24 of a 64-beat clip;
- a clip dragged past the timeline's right edge scrolling the time on and landing under the pointer, and one undo putting it back;
- a time selection dragged past the edge scrolling on and running to the time under the pointer;
- a time selection, and a track's header, dragged below the lanes shown scrolling them down.

## Not implemented

- Continuous (non-paging) follow.
- Vertical track-height zoom and Optimize Height/Width.
- Follow in the Session view.
- Any comparison with reference behaviour.
