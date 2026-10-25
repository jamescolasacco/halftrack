# Window layout contract

Status: partial. This follows the reference's manual and static reading. No reference window was measured.

## Evidence

- Live 12 manual, [Live Concepts](https://www.ableton.com/en/live-manual/12/live-concepts/), section 3.3, consulted 2026-10-24: the browser's menu offers to "expand the browser to full height".
- The reference's View builder adds Full Height Browser (`fhbr`, no shortcut) after the browser, groove pool, tuning pool and mod matrix items. Its song preference data keeps the setting off by default (static reading: `ASongPrefData::mpBrowserIsFullHeight`, default 0, in the constructor at 0x102455ec8).
- Live 12 manual, [Live Concepts](https://www.ableton.com/en/live-manual/12/live-concepts/), section 3.12, consulted 2026-10-24: the Clip View and Device View selectors, with triangle toggles that stack the two views, sit "to the left of the Mixer View toggle in the bottom-right corner of Live". The [keyboard shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/) list Option-click on the view toggle for Toggle Device and Clip View, and a right-click on the Device View selector to see the list of devices on a track.
- The same builder adds `cdvx` (`ALiveApp::sMenuExpandClipDetailView`, Option+Cmd+E, a checkbox item) after Toggle Clip/Device View and before Hide/Show Detail View. The manual's [keyboard shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), consulted 2026-10-24, name it Toggle Full-Size Clip View (Cmd+Option+E on a Mac, Ctrl+Alt+E on Windows).

## Halftrack's behaviour

- **The detail view.** By default it spans the window under the browser, from the window's left edge; the browser and the Info View under it stand above it, beside the Arrangement or Session.
- **Full Height Browser.** View > Full Height Browser, kept with the preferences and off at first, runs the browser down beside the detail view instead, as Halftrack's layout used to be.
- **The browser's width.** Its right edge drags to resize it, from 220 pixels to the smaller of 640 and half the window, starting at 300.
- **Without the browser** the Info View keeps its corner beside the detail view, as before.
- **The corner's selectors.** The Clip View and Device View selectors sit at the right end of the status bar, left of the mixer view control. Each is lit while its view shows in the detail view. A click shows its view, or hides the detail view while that view shows; the Clip View selector is greyed while there is no clip to show. Option-click switches between the two views. A right-click on the Device View selector lists the devices of the chain the device view shows, and choosing one shows it selected.
- **Full-Size Clip View.** View > Full-Size Clip View or Cmd/Ctrl+Option+E gives the clip view the whole height while it shows, the Arrangement or Session hidden and the browser beside it. Pressed again, it gives the height back. Chosen while the clip view is hidden, it shows the clip view full size. It lasts while the window is open.

## Not yet

- The reference's browser menu, from which it also offers Full Height Browser.
- Stacking the Clip View and Device View together with the selectors' triangle toggles, which the reference's manual describes (section 3.12).

## Evidence in Halftrack

The UI suite checks that the detail view spans under the browser at first, that View > Full Height Browser runs the browser down beside it and back, and that the browser's edge drags it wider and past both limits. It checks that Cmd/Ctrl+Option+E gives the clip view the whole height, hiding the Arrangement, and gives it back. It checks that the selectors sit left of the mixer view control and light with their views, that a click on the lit one hides the detail view and the other shows its view, that Option-click switches between them, and that the Device View selector's list shows a device selected. `tests/PreferencesTests.cpp` reads and writes the preferences file, which now holds the setting.
