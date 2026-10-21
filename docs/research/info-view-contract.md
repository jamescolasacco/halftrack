# Info View contract

Halftrack's Info View follows the reference's: a box under the browser that says what the control under the pointer is and does.

## Behaviour

- **What it shows.** It checks the component under the pointer ten times a second and describes the nearest component, among it and its parents, that has a name or a tooltip.
  - The title is a button's text or the component's name.
  - The body is the tooltip, Halftrack's own description of the control.
  - A control that is disabled adds "Not available here now."
  - With nothing describable under the pointer, it invites pointing at a control.
- **Showing it.** It shows by default. View > Info or the ? key hides and shows it.

## Evidence

The UI suite checks:
- that the Info View shows under the browser;
- that it describes the Play button by its name and tooltip;
- that it says when the button is disabled, and invites pointing with nothing under the pointer;
- that ? hides and shows it.

## Not yet covered

- The reference's Help View lessons and a searchable help index are missing.
- Unavailable controls give a general note rather than their own reason.
