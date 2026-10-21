# Timeline navigation contract

The Arrangement and piano roll follow the playhead and zoom to selections, after the reference's Follow switch and zoom commands ([Arrangement View: Navigation](https://www.ableton.com/en/live-manual/12/arrangement-view/), consulted 2026-10-20). This is Halftrack's own interface behaviour; paging versus continuous scrolling is a simplest default, not a measurement.

## Behaviour

- **Follow** (control bar button, Cmd/Ctrl+F) is on by default. While playing, a playhead that leaves the visible time turns the view to the page that starts at the playhead.
  - The Arrangement follows the global playhead, and the piano roll follows its clip's playhead (the launched slot's position for Session clips).
  - Scrolling by hand while playing, with the scroll bar or a horizontal or Shift scroll, turns Follow off, as the button shows.
- **Zoom**:
  - + and − zoom the Arrangement or the open piano roll, and Cmd/Ctrl+scroll zooms the Arrangement.
  - Z zooms the Arrangement to the time selection (or the selected clip), with 5% margins.
  - X returns to the view from before the last Z.

## Evidence

The UI suite checks:

- Follow's default;
- the paging rule for visible, later and earlier playheads;
- the Arrangement turning to a page one and a half views ahead while playing;
- a hand scroll turning Follow off, with the view then staying put;
- Cmd/Ctrl+F turning it back on;
- Z fitting a two-second selection and X restoring the exact previous view;
- the piano roll paging to its playhead at beat 24 of a 64-beat clip.

## Not implemented

- Continuous (non-paging) follow.
- Vertical track-height zoom and Optimize Height/Width.
- Follow in the Session view.
- Any comparison with reference behaviour.
