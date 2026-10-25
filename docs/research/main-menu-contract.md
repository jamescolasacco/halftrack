# Main menu bar contract

Status: partial. This follows a static reading of the reference's menu bar. No reference output was measured.

## Evidence

A research agent read the reference statically on 2026-10-24 (Live 12.2.1, arm64 slice sha256 b37cb738…; Apple LLVM objdump 17.0.0, Ghidra 12.1.4). It read:
- the menu bar's construction, `ALiveApp` vt+0x930 (0x1002b8b34), and its nine builders (File 0x1002a5220, Edit 0x1002a675c, Create 0x1002aa6e8, Playback 0x1002abf10, View 0x1002ad390, Navigate 0x1002b1df0, Options 0x1002b3068, Help 0x1002b5f20, and a hidden menu 0x1002b6bf8);
- how items are enabled, `TCocoaMenu validateMenuItem:` (0x101555fdc);
- the bundle's unencrypted `English.lproj/MainMenu.nib`, for the application menu.
- on the same day, File's Open Recent submenu (0x1016d9ad0, titled with the Open Recent key), its entries (0x1016d2f14: one command per recent Set, each with open, enabled and name callbacks) and the commands after them (0x1016d2c38: a separator, then `recn`, removing the unavailable entries and enabled when there are any, and `recr`, clearing the list, always enabled).

Nothing was launched, no preferences or Sets were read, and nothing was decrypted. The English labels are in the reference's encrypted language file, so Halftrack's labels are its own plain names for the commands. The findings are in the ignored `artifacts/reverse-engineering/mainmenu/`.

## Rules

- **The menus.** The reference's bar is Live, File, Edit, Create, Playback, View, Navigate, Options and Help, the same in both views. Halftrack's is File, Edit, Create, Playback, View, Navigate and Options. On a Mac the application menu takes the Live menu's place: About Halftrack, then Preferences (Cmd+comma), then the system's items. Elsewhere Help holds About Halftrack and Options ends with Preferences; the reference's Windows layout was not read.
- **Every item stays.** Unlike the reference's context menus, its menu bar never leaves out an item that cannot act: the item is greyed. Halftrack does the same. It leaves out only the reference's items for features it does not have.
- **Shortcuts.** Each item shows the reference's shortcut (macOS; Redo and Fullscreen take their Windows keys elsewhere). The reference's alternates reach the same items: F11 (Fullscreen), Cmd+underscore (Zoom Out), F12 (Toggle Clip/Device View), Option+Cmd+B (Browser), Option+Cmd+G (Groove Pool), Shift+U (Fold Selected Track), = and _ (Zoom In and Out), Shift+X (Zoom Back), and Shift with H, W, T and R. A key that nothing focused takes runs the menu command it is shown on, when that can act (`runMenuShortcut`). On a Mac the items carry their shortcuts natively; JUCE does not show a plain Delete key there, so Edit's Delete shows none.
- **While a dialog or menu is open**, every item is greyed and the shortcuts wait, as the reference blocks its menu commands.
- **Text that changes.** Play/Stop, Solo/Unsolo, Arm/Disarm, Add/Delete Locator, the record items, the fold items, Loop Selection or Activate/Deactivate Loop, and the plurals of the clip commands follow the state, as the reference's handlers rename them. Undo and Redo name the step they would take back or do again ("Undo Add MIDI track"), or read Can't Undo and Can't Redo, as the reference's history texts do; the step names are Halftrack's own.

## Edit acts on the area last clicked

The reference finds an Edit command's handler from the focused view. Halftrack keeps the area last clicked, or last focused from Navigate (`editArea`): text being typed first, then the clip view's notes, the device view, the headers, a scene, the Session's slots, or the Arrangement. In each, the command does what that area's own menu or keys do:
- **Arrangement:** the clip menu's command over the time selection or the selected clips. A lone clip selected without a time selection is cut, copied, duplicated or deleted as a clip, as its keys do.
- **Slots:** the slot menu's command. Split becomes Add or Remove Stop Button, as the reference's Cmd+E does in the Session.
- **Headers:** the header menu's command for the selected tracks or group.
- **A scene:** Duplicate, Delete, Rename and Edit Info Text from the scene menu.
- **Notes:** Cut, Copy, Paste, Duplicate, Delete, Split, Deactivate, Select All and Quantize on the notes, Loop Selection on the note editor's selected timespan, as the clip view's keys do, and Invert Selection, which only the clip view has, as in the reference.
- **Devices:** Delete, and Group and Ungroup making and breaking racks.
- **Text:** Cut, Copy, Paste, Delete and Select All on the text.

Select All selects every track's content in the Arrangement, every slot in the Session, or every track among the headers.

## The menus

| Menu | Halftrack's items, in the reference's order |
|---|---|
| File | New Project (Cmd+N), Open Project... (Cmd+O), Open Recent Project (its projects, the latest first and greyed when gone, then Remove Unavailable Projects and Clear List); Install Pack..., Add Folder to Browser... (adds a folder to the browser's [Places](browser-places-contract.md)); Locate Missing Media..., at Manage Projects' place, Backups (the project's [backups](backups-contract.md), then Show in Browser and Reveal in Finder); Save Project (Cmd+S, while there are changes or it was never saved), Save Project As... (Shift+Cmd+S), Save a Copy..., Save as Template... (see the [templates contract](templates-contract.md)), Save as Default Set; Export Audio... (Shift+Cmd+R), Export MIDI Clip... |
| Edit | Undo and Redo, naming their step; Cut, Copy, Paste, Duplicate, Delete; Deactivate (0); Select All (Cmd+A), Invert Selection (Shift+Cmd+A, the clip view's notes), Select Loop (Shift+Cmd+L: the loop's time across every track, or in the clip view the notes inside the clip's loop brace), Loop Selection (Cmd+L), Clip Markers (Set Clip Start, Loop Start, Loop End and Clip End, Cmd+F9 to F12; see the [clip loops contract](clip-loops-contract.md)); Group Tracks (Cmd+G), Ungroup Tracks; Cut, Paste, Duplicate and Delete Time; Rename (Cmd+R), Edit Info Text; Solo (S), Arm (C); Freeze Track (Option+Shift+Cmd+F), Bounce Track in Place, Bounce to New Track (Cmd+B); Split (Cmd+E), Consolidate (Cmd+J); Quantize (Cmd+U, with the Quantize tool's settings), Quantize Settings... (Shift+Cmd+U, opening the [Quantize tool](midi-tools-contract.md#quantize-2026-10-24)), Extract Groove, Record Quantization |
| Create | Insert Audio, MIDI and Return Track (Cmd+T, Shift+Cmd+T, Option+Cmd+T), Insert Take Lane (Option+Shift+T); Insert Scene in the Session or Insert Silence in the Arrangement (Cmd+I), Capture and Insert Scene (Shift+Cmd+I), Consolidate Time to New Scene; Insert MIDI Clip (Shift+Cmd+M), Capture MIDI (Shift+Cmd+C); Create Fade (Option+Cmd+F); Slice to New MIDI Track; Add Locator, Insert Time Signature Change; Import File... |
| Playback | Play (Space), Continue Playback (Shift+Space), Play Arrangement Selection (Option+Space: the time selection, stopping at its end; without one, Play from Insert Marker), Move Insert Marker to Playhead (Shift+Cmd+Space), Playback Options (Follow, Option+Shift+F; Start Transport With Record; Start Transport With Tap Tempo, see the [preferences contract](preferences-contract.md)); Record in Arrangement and Arm Record in Arrangement, which swap F9 and Shift+F9 with Start Transport With Record; Record in Session (Shift+Cmd+F9); Metronome (O), Metronome Only While Recording; Back to Arrangement (F10) |
| View | Fullscreen (Ctrl+Cmd+F), Zoom (the display zoom: Cmd+plus, Cmd+minus, Option+Cmd+0); Info (?), Undo History (Option+Cmd+Z); Toggle Arrangement/Session View (Tab); Clip View (Option+Cmd+3), Device View (Option+Cmd+4), Toggle Clip/Device View (Shift+Tab), Full-Size Clip View (Option+Cmd+E; [layout contract](window-layout-contract.md)), Hide or Show Detail View (Option+Cmd+L); Notes or Sample, Envelopes, MPE (the clip editor's tabs, Option+Shift+1 to 3; [clip view contract](clip-view-contract.md)); Browser (Option+Cmd+5), Groove Pool (Option+Cmd+6), Full Height Browser ([layout contract](window-layout-contract.md)); Search in Browser (Cmd+F), Show Similar Files (Shift+Cmd+F); Plug-In Windows (Option+Cmd+P), Video Window (Option+Cmd+V, Mac); Mixer (Option+Cmd+M; the mixer of the view shown, the Arrangement's at its bottom), Mixer Controls (In/Out, Sends, Mixer, Track Options, Crossfader, Return Tracks: the shown mixer's sections, Return Tracks always the Session's; [mixer contract](mixer-sections-contract.md#the-arrangements-mixer-2026-10-24)), Arrangement Track Controls (In/Out, Volume, Track Options, Returns; Option+Cmd+I and Option+Cmd+R in the Arrangement); Overview (Option+Cmd+O, the Arrangement's), Scene Tempo and Time Signature; Automation Mode (A), Fold All Tracks (Option+U), Fold Selected Track (U); Fold to Notes (F), Fold to Scale (G), for the note editor ([scale contract](scales-contract.md#folding)); Zoom In (+), Zoom Out (−), Zoom to Arrangement Time Selection or Zoom Out Completely (Z), Zoom Back (X) |
| Navigate | Focus Session View (Option+1), Focus Arrangement View (Option+2), Focus Clip View (Option+3, with a clip selected), Focus Device View (Option+4), Focus Browser (Option+5), Focus Groove Pool (Option+6); Focus Mixer (Option+Shift+M, the Arrangement's: it shows the Arrangement's mixer, and is greyed in the Session). Each shows its part and gives it the keys and Edit's commands |
| Options | Edit MIDI Map (Cmd+M), Edit Key Map (Cmd+K), Computer MIDI Keyboard (M), MIDI Prelisten (the note editor's Preview switch), Browser Pre-Hear (the browser's Preview button); External Sync, Delay Compensation ([contract](delay-compensation-contract.md)); Time Ruler Format (Min:Sec and the SMPTE rates); Narrow, Widen, Triplet and Snap to Grid (Cmd+1 to 4), Snap Automation to Grid ([automation contract](arrangement-automation-contract.md#snap-automation-to-grid-2026-10-24)), Fixed Grid (Cmd+5); Draw Mode (B, see the [note drawing contract](note-drawing-contract.md)); Scale Highlighting (K); Solo Switches, Cue Switches, Solo in Place; Lock Envelopes ([automation contract](arrangement-automation-contract.md#automation-follows-clips-and-lock-envelopes-2026-10-24)), Arrangement Overdub ([take lanes contract](take-lanes-contract.md)); Audio Engine On (Option+Shift+Cmd+E) |

What moved with the rebuild:
- Halftrack's Session menu is gone. Its scene commands are in the scene menu and Edit; a scene drags in the scene column to a new place, as the reference's do; Stop All Clips is the Session's button; a clip's follow action opens from the clip view's Launch box.
- View's Session, Arrangement and Piano Roll items became Toggle Arrangement/Session View and Clip View. The automation window opens from the device view's automation button, for the Main when its chain is shown.
- Audio & MIDI Setup and Plug-Ins became tabs of the one Preferences window, as the reference's (see the [preferences contract](preferences-contract.md)).
- Undo History is a window of its own beside the main one, Halftrack's, listing the steps by Edit's step names after a Start row; the steps Redo would do again are dimmed. Escape closes it, as the reference's Esc closes its windows and dialogs (manual section 42.1). The reference's window was not read.
- Save a Copy writes the project to another file, with its media collected beside it, and keeps the open project's file, media and unsaved changes. Every Halftrack save collects its media, where the reference collects only with Collect All and Save (see [open questions](../open-questions.md)).
- Import Audio and Import MIDI File became Import File, which takes both. Cmd+F searches the browser, and Follow moved to Option+Shift+F; Cmd+I inserts a scene in the Session; Shift+Cmd+R exports; Option+Cmd+O is the overview's, not Open's.

## Keys outside the menus

From the reference's list of keys ([Live keyboard shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), consulted 2026-10-24), these act without a menu item:
- **R** reverses, and **Shift+Cmd+J** crops, as the reference's hidden menu has them (`rvrs`, `crop`; Option+Cmd+Delete is its Reset Fades, `rsfd`, and Cmd+6 to Cmd+0 its grid items `gri6` to `qno `), the Arrangement's time selection or a lone selected clip ([clip menus](clip-menus-contract.md)).
- **Cmd+Option+Delete** deletes the fades and crossfades of the selected Arrangement clips, as Reset Fades does.
- **Cmd+Delete** clears the envelope last clicked in Automation Mode ([automation contract](arrangement-automation-contract.md#clear-envelope-2026-10-24)).
- **F1 to F8** switch the tracks numbered 1 to 8 on and off, groups counted, as their activators do.
- **Escape** jumps to the selected track's title bar: the headers then take Edit's commands.
- **Cmd+6 to Cmd+9 and Cmd+0** set the global launch quantization to 1/16, 1/8, 1/4, 1 Bar and None.
- **Cmd+[ and Cmd+]** go back and forward through the browser's [history](browser-places-contract.md#history-2026-10-24).
- **Q** links the selected device to the browser for [Hot-Swap](presets-contract.md#hot-swap-2026-10-24), or ends it; Escape ends it too.
- **In the browser:** Right opens the selected folder in place (with Cmd keeping the others open) or previews the selected sample, Left closes a folder, goes to the folder holding an item or at the top moves out to the sidebar (whose Up and Down move through its rows), and 0 takes the selected item out of every collection (1 to 7 already put it in one). See the [folder tree](browser-places-contract.md#the-folder-tree-2026-10-24).
- **Momentary latching**, as the reference's (its key list: "Momentary latching becomes available after holding down a shortcut key for about 500 ms"): A, B, S, Z, Tab and F1 to F8 act when pressed; held for 500 ms or more, letting go returns what they changed, Z zooming back. Their repeats while held do nothing, where before a held key flipped its switch over and over.
- **In the Arrangement:** Left and Right nudge the selected clips a grid step, their automation following, or move the time selection without one; from the headers they fold and unfold the selected track. Up and Down select the track above or below, taking a time selection along. Each nudge is one undo step.
- **In the Session:** Cmd+Return stops the selected slot's track; Return launches the scene when its column was clicked (Shift+Return still launches it, Halftrack's); Page Up and Page Down move eight scenes; Home and End go to the first and last track, and End from the last track to the scene itself, whose cell then shows the selection (manual section 42.15: Jump to the Current Scene from the Last Track; 2026-10-25); Cmd with Left and Right moves the selected track, and with Up and Down the selected scene, each one undo step.

## Left out

Navigate's Focus Transport, Help, Tuning and Clip Panels, its previous and next focus commands and Tab navigation options; Close Window, Collect All and Save, Apply Algorithm, the note span command, Automation Fitting, Emoji & Symbols, the Convert to MIDI commands, Return Play Position, Link, Second Window, Lessons, File Manager, Expand Clip Detail View, the clip view layouts and editors, Tuning Pool, Mod Matrix, Max for Live, Accessibility, Reduced Latency When Monitoring, Snap Automation to Grid, Step Input, Chase MIDI Notes, the controller and retune options, Legacy HiQ, and Help's items. Halftrack has none of these commands yet.

## Evidence in Halftrack

The UI suite compares each menu with the reference's order, filtered to Halftrack's items. It checks:
- shortcuts on Save, Export Audio, Bounce to New Track, Insert MIDI Clip, Follow, Search in Browser and Audio Engine On, and Preferences' Cmd+comma in the application menu;
- Cut and Zoom Back staying greyed with nothing to act on, and every item greyed while a dialog is open;
- S and C (Solo and Arm) on the selected track, O for the metronome, and Start Transport With Record swapping F9 and Shift+F9;
- Add and Delete Locator at the play position;
- View's Browser hiding the browser and Search in Browser showing it, and Option+Cmd+L hiding and showing the detail view;
- Invert Selection on the clip view's notes;
- Solo Switches and Cue Switches greyed without a Cue Out of their own, and switching the Solo/Cue mode with one;
- Draw Mode showing B, ticked while on, and following the control bar's pencil; MIDI Prelisten and Browser Pre-Hear switching the two Preview switches, ticked to match;
- Shift+Cmd+L selecting the loop's time across every track, Play Arrangement Selection's two names, and Option+Space playing the selection from its start and stopping at its end (the transport suite checks the stop within 60 ms, 12 ms locally, as Halftrack watches the range from the message thread every 2 ms; the reference's precision was not measured); Select Loop in the clip view selecting the notes in the clip's loop;
- F1 switching track 1 off and on, and Cmd+Option+Delete deleting a fade (in the Arrangement menu checks);
- momentary latching with a stood-in key state and clock: A acting at once, its repeats doing nothing, a quick release keeping the change, and B held 600 ms turning Draw Mode back off;
- the Arrangement's arrows nudging a clip and the time selection, folding a track from its header, and selecting the next track;
- the alternate keys reaching their items, with Option+Cmd+B showing and hiding the browser;
- the Session keys above, Escape giving the headers Edit's commands, Cmd+6 to 9 and Cmd+0 setting the global quantization, and the browser's Right, Left and 0;
- Navigate's Option+1 and Option+2 showing the Session and Arrangement Views with Edit acting there, Focus Device View and Focus Clip View likewise, Focus Browser showing the browser, and Option+Shift+M showing the Arrangement's mixer, greyed in the Session;
- Undo History opening from Option+Cmd+Z without undoing, listing the steps from the start with the latest selected, a click on an earlier step undoing back to it and on the latest redoing, the main window's menus working while it is open, and choosing it again, or Escape, closing it;
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
