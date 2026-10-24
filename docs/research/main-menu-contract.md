# Main menu bar contract

Status: partial. This follows a static reading of the reference's menu bar. No reference output was measured.

## Evidence

A research agent read the reference statically on 2026-10-24 (Live 12.2.1, arm64 slice sha256 b37cb738…; Apple LLVM objdump 17.0.0, Ghidra 12.1.4). It read:
- the menu bar's construction, `ALiveApp` vt+0x930 (0x1002b8b34), and its nine builders (File 0x1002a5220, Edit 0x1002a675c, Create 0x1002aa6e8, Playback 0x1002abf10, View 0x1002ad390, Navigate 0x1002b1df0, Options 0x1002b3068, Help 0x1002b5f20, and a hidden menu 0x1002b6bf8);
- how items are enabled, `TCocoaMenu validateMenuItem:` (0x101555fdc);
- the bundle's unencrypted `English.lproj/MainMenu.nib`, for the application menu.

Nothing was launched, no preferences or Sets were read, and nothing was decrypted. The English labels are in the reference's encrypted language file, so Halftrack's labels are its own plain names for the commands. The findings are in the ignored `artifacts/reverse-engineering/mainmenu/`.

## Rules

- **The menus.** The reference's bar is Live, File, Edit, Create, Playback, View, Navigate, Options and Help, the same in both views. Halftrack's is File, Edit, Create, Playback, View and Options. On a Mac the application menu takes the Live menu's place: About Halftrack, then Preferences (Cmd+comma), then the system's items. Elsewhere Help holds About Halftrack and Options ends with Preferences; the reference's Windows layout was not read. Navigate is left out, as Halftrack has none of its focus commands.
- **Every item stays.** Unlike the reference's context menus, its menu bar never leaves out an item that cannot act: the item is greyed. Halftrack does the same. It leaves out only the reference's items for features it does not have.
- **Shortcuts.** Each item shows the reference's shortcut (macOS; Redo and Fullscreen take their Windows keys elsewhere). A key that nothing focused takes runs the menu command it is shown on, when that can act (`runMenuShortcut`). On a Mac the items carry their shortcuts natively; JUCE does not show a plain Delete key there, so Edit's Delete shows none.
- **While a dialog or menu is open**, every item is greyed and the shortcuts wait, as the reference blocks its menu commands.
- **Text that changes.** Play/Stop, Solo/Unsolo, Arm/Disarm, Add/Delete Locator, the record items, the fold items, Loop Selection or Activate/Deactivate Loop, and the plurals of the clip commands follow the state, as the reference's handlers rename them. Undo and Redo name the step they would take back or do again ("Undo Add MIDI track"), or read Can't Undo and Can't Redo, as the reference's history texts do; the step names are Halftrack's own.

## Edit acts on the area last clicked

The reference finds an Edit command's handler from the focused view. Halftrack keeps the area last clicked (`editArea`): text being typed first, then the clip view's notes, the device view, the headers, a scene, the Session's slots, or the Arrangement. In each, the command does what that area's own menu or keys do:
- **Arrangement:** the clip menu's command over the time selection or the selected clips. A lone clip selected without a time selection is cut, copied, duplicated or deleted as a clip, as its keys do.
- **Slots:** the slot menu's command. Split becomes Add or Remove Stop Button, as the reference's Cmd+E does in the Session.
- **Headers:** the header menu's command for the selected tracks or group.
- **A scene:** Duplicate, Delete, Rename and Edit Info Text from the scene menu.
- **Notes:** Cut, Copy, Paste, Duplicate, Delete, Split, Deactivate, Select All and Quantize on the notes, as the clip view's keys do, and Invert Selection, which only the clip view has, as in the reference.
- **Devices:** Delete, and Group and Ungroup making and breaking racks.
- **Text:** Cut, Copy, Paste, Delete and Select All on the text.

Select All selects every track's content in the Arrangement, every slot in the Session, or every track among the headers.

## The menus

| Menu | Halftrack's items, in the reference's order |
|---|---|
| File | New Project (Cmd+N), Open Project... (Cmd+O); Install Pack...; Locate Missing Media..., at Manage Projects' place; Save Project (Cmd+S, while there are changes or it was never saved), Save Project As... (Shift+Cmd+S), Save a Copy..., Save as Default Set; Export Audio... (Shift+Cmd+R), Export MIDI Clip... |
| Edit | Undo and Redo, naming their step; Cut, Copy, Paste, Duplicate, Delete; Deactivate (0); Select All (Cmd+A), Invert Selection (Shift+Cmd+A, the clip view's notes), Loop Selection (Cmd+L); Group Tracks (Cmd+G), Ungroup Tracks; Cut, Paste, Duplicate and Delete Time; Rename (Cmd+R), Edit Info Text; Solo (S), Arm (C); Freeze Track (Option+Shift+Cmd+F), Bounce Track in Place, Bounce to New Track (Cmd+B); Split (Cmd+E), Consolidate (Cmd+J); Quantize (Cmd+U), Extract Groove, Record Quantization |
| Create | Insert Audio, MIDI and Return Track (Cmd+T, Shift+Cmd+T, Option+Cmd+T), Insert Take Lane (Option+Shift+T); Insert Scene in the Session or Insert Silence in the Arrangement (Cmd+I), Capture and Insert Scene (Shift+Cmd+I), Consolidate Time to New Scene; Insert MIDI Clip (Shift+Cmd+M), Capture MIDI (Shift+Cmd+C); Create Fade (Option+Cmd+F); Slice to New MIDI Track; Add Locator, Insert Time Signature Change; Import File... |
| Playback | Play (Space), Continue Playback (Shift+Space), Move Insert Marker to Playhead (Shift+Cmd+Space), Playback Options (Follow, Option+Shift+F; Start Transport With Record); Record in Arrangement and Arm Record in Arrangement, which swap F9 and Shift+F9 with Start Transport With Record; Record in Session (Shift+Cmd+F9); Metronome (O), Metronome Only While Recording; Back to Arrangement (F10) |
| View | Fullscreen (Ctrl+Cmd+F), Zoom (the display zoom: Cmd+plus, Cmd+minus, Option+Cmd+0); Info (?); Toggle Arrangement/Session View (Tab); Clip View (Option+Cmd+3), Device View (Option+Cmd+4), Toggle Clip/Device View (Shift+Tab), Hide or Show Detail View (Option+Cmd+L); Browser (Option+Cmd+5), Groove Pool (Option+Cmd+6); Search in Browser (Cmd+F), Show Similar Files (Shift+Cmd+F); Plug-In Windows (Option+Cmd+P), Video Window (Option+Cmd+V, Mac); Mixer (Option+Cmd+M), Mixer Controls; Overview (Option+Cmd+O, the Arrangement's), Scene Tempo and Time Signature; Automation Mode (A), Fold All Tracks (Option+U), Fold Selected Track (U); Zoom In (+), Zoom Out (−), Zoom to Arrangement Time Selection or Zoom Out Completely (Z), Zoom Back (X) |
| Options | Edit MIDI Map (Cmd+M), Edit Key Map (Cmd+K), Computer MIDI Keyboard (M); External Sync; Time Ruler Format (Min:Sec and the SMPTE rates); Narrow, Widen, Triplet, Snap to and Fixed Grid (Cmd+1 to 5); Draw Mode (B, see the [note drawing contract](note-drawing-contract.md)); Solo Switches, Cue Switches, Solo in Place; Audio Engine On (Option+Shift+Cmd+E) |

What moved with the rebuild:
- Halftrack's Session menu is gone. Its scene commands are in the scene menu and Edit; a scene drags in the scene column to a new place, as the reference's do; Stop All Clips is the Session's button; a clip's follow action opens from the clip view's Launch box.
- View's Session, Arrangement and Piano Roll items became Toggle Arrangement/Session View and Clip View. The automation window opens from the device view's automation button, for the Main when its chain is shown.
- Audio & MIDI Setup and Plug-Ins became tabs of the one Preferences window, as the reference's (see the [preferences contract](preferences-contract.md)).
- Save a Copy writes the project to another file, with its media collected beside it, and keeps the open project's file, media and unsaved changes. Every Halftrack save collects its media, where the reference collects only with Collect All and Save (see [open questions](../open-questions.md)).
- Import Audio and Import MIDI File became Import File, which takes both. Cmd+F searches the browser, and Follow moved to Option+Shift+F; Cmd+I inserts a scene in the Session; Shift+Cmd+R exports; Option+Cmd+O is the overview's, not Open's.

## Left out

Close Window, Add Folder to Browser, Backups, Collect All and Save, Save as Template, Select Loop, Clip Markers, Apply Algorithm, the note span command, Quantize Settings, Automation Fitting, Emoji & Symbols, the Convert to MIDI commands, Play Arrangement Selection, Return Play Position, Start Transport With Tap Tempo, Link, Second Window, Lessons, File Manager, Undo History, Expand Clip Detail View, the clip view layouts and editors, Tuning Pool, Mod Matrix, Full Height Browser, Max for Live, Arrangement Track Controls, Fold to Notes and Scale, Accessibility, MIDI Prelisten, Browser Pre-Hear, Delay Compensation, Snap Automation to Grid, Step Input, Scale Highlighting, Chase MIDI Notes, the controller and retune options, Lock Envelopes, Arrangement Overdub, Legacy HiQ, and Help's items. Halftrack has none of these commands yet.

## Evidence in Halftrack

The UI suite compares each menu with the reference's order, filtered to Halftrack's items. It checks:
- shortcuts on Save, Export Audio, Bounce to New Track, Insert MIDI Clip, Follow, Search in Browser and Audio Engine On, and Preferences' Cmd+comma in the application menu;
- Cut and Zoom Back staying greyed with nothing to act on, and every item greyed while a dialog is open;
- S and C (Solo and Arm) on the selected track, O for the metronome, and Start Transport With Record swapping F9 and Shift+F9;
- Add and Delete Locator at the play position;
- View's Browser hiding the browser and Search in Browser showing it, and Option+Cmd+L hiding and showing the detail view;
- Invert Selection on the clip view's notes;
- Solo Switches and Cue Switches greyed without a Cue Out of their own, and switching the Solo/Cue mode with one;
- Draw Mode showing B, ticked while on, and following the control bar's pencil;
- Plug-In Windows hiding and showing the plug-in windows (the plug-in editor suite hides an open editor's window, shows it again, and sees opening an editor bring the windows back);
- Cmd+I inserting a scene below the selected one;
- Edit acting on the slots (Cmd+E removing a stop button), a scene and the headers (Duplicate), and a lone Arrangement clip (Cut, then Paste at the insert marker), and Select All in the Arrangement;
- the Launch box opening a clip's follow action.

## Not determined

The Preferences research (2026-10-24) found that the condition on Options' Solo and Cue Switches (0x1018b2fc0) is `IsReWireDeviceMode()`, false in a normal launch, so the reference shows them.


- English labels.
- The Windows and Linux layouts.
- The Insert Silence and Paste enable rules.
- What Metronome While Recording does: Halftrack takes it as its metronome's only-while-recording switch.
