# Control audit contract

No control in Halftrack should look usable and do nothing. The UI suite audits this for the main views.

## The audit

At the end of the UI checks, a project is set up with an audio clip, an EQ Three, a MIDI track, an Arrangement MIDI clip and a Session clip. The audit then walks every visible component of the main window in three views:

- the Arrangement View, with the track's device chain showing;
- the Session View;
- the MIDI clip editor.

Each visible, enabled control must have a handler:

- **Buttons** need a click or state handler.
- **Menus** need a change handler.
- **Sliders** need a value or drag handler.
- **Text fields** need a text or Return handler, unless read-only.
- **Editable labels** need a text handler.

The inner parts of JUCE's composite controls (a slider's text box, a menu's label, scroll bars) are part of their owner and are not checked separately. The audit lists every control that fails, by its path of component names.

## Exceptions

A control whose value is read when another action happens is marked `readWhenUsed` beside its code, which says what reads it. There is one: the MIDI editor's **New note length**, read when a note is drawn and by the Fixed Length note tool.

## Limits

The audit proves that a handler exists, not that the handler does what the control says. That is what the feature checks throughout the UI suite are for.

## Not yet covered

- Dialogs and windows outside the main window (Audio Settings, Preferences, plug-in windows).
- Audio clips' views.
- Rack and native device editors in every state.
- Context menu items.
- A UI check that drawing uses the chosen note length.
