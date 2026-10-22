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

## Screen readers

The same walk checks that a screen reader can name every visible control. Each must be accessible, and what JUCE's accessibility handler would speak, its title or a button's text when it has none, must be a name of three or more characters. JUCE creates handlers only for components on a native window, which the checks do not open, so the title is checked rather than a handler.

- **Track controls** are titled with their track's name ("Bass solo", "Bass volume").
- **Native device controls** take their device and control names ("EQ Three Low").
- **Device panel buttons** name their device ("Remove EQ Three").
- **Everything else** takes its readable name ("Track volume", "Play").
- **The major regions** (Arrangement, Session, MIDI clip editor, Devices, Browser) have titles and descriptions.

Clips, clip slots and notes inside the custom regions are not yet exposed to screen readers, and no real VoiceOver, Narrator or NVDA session has been tried.

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
