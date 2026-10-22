# Track heights and optimizing the view

Status: partial, ARR-001.

The reference's manual describes:

- dragging the split line below a track to adjust its height, and holding Alt (Win) / Option (Mac) to resize all tracks at once;
- Alt/Option + and Alt/Option - to resize a track;
- Alt/Option with the mouse wheel in a track's lane to zoom it vertically;
- Optimize Height (H) and Optimize Width (W), which fit all tracks into the Arrangement's height or its length into its width.

## Behaviour

- **The model.** `Session::setTrackHeight (trackId, pixels)` sets a track's Arrangement height, 20 to 600 pixels, or 0 for the default (72). `TrackInfo::height` holds it. It is saved with the project as a track property, outside the undo history and not marked as an unsaved change, and it moves with its track.
- **Dragging.** The split line (the bottom 4 pixels of an Arrangement track header) drags the track's height, showing the resize cursor. With Option (Alt elsewhere) held, every track takes the dragged height.
- **Keys and wheel.** Option/Alt + and - scale the selected track by 1.25. Option/Alt with the mouse wheel over a track's lane scales that track by 1.12 a step.
- **Optimize Height (H).** It gives every track an equal height that fills the view's height, after any group, take and automation lanes. Pressed again, it restores the heights from before.
- **Optimize Width (W).** It fits the Arrangement's length into the view, as Fit does. Pressed again, it restores the zoom and scroll from before.

## Evidence

- The track order suite checks refusals outside 20-600 pixels or for no track, a 140-pixel height that is neither an undo step nor an unsaved change, the height moving with its track, saving and reopening, and 0 restoring the default.
- The UI suite checks:
  - a 40-pixel split-line drag on one track, and an Option/Alt drag giving every track the height;
  - Option/Alt + and -;
  - H fitting every track into the view and H again restoring the heights;
  - W and W again restoring the zoom.

## Not yet covered

- Folding and unfolding tracks (the reference's unfold button, U and Option/Alt-U).
- The reference's exact resize steps, minimum height and how controls collapse in short tracks.
- Optimize states saved with the project.
