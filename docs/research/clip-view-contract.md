# Clip View contract

Status: partial. This follows the reference's manual and static reading. No reference window was measured.

## Evidence

- Live 12 manual, [Clip View](https://www.ableton.com/en/live-manual/12/clip-view/), sections 8.1 to 8.1.3, consulted 2026-10-24. The Clip View has a title bar, clip panels on the left and an editor on the right. The title bar holds the Clip Activator toggle and shows the clip's name and colour; its context menu renames the clip and colours it from the palette, and offers Fold, which, like a double-click on the title bar, minimizes the panels. Audio clips switch the editor between the Sample Editor and the Envelope Editor. MIDI clips switch between the MIDI Note Editor, the Envelope Editor and the MPE Editor. The manual says you switch "by clicking on their dedicated tabs", from the View menu or with keys, and that Option+Tab (Mac) or Ctrl+Tab (Windows) steps between them.
- The manual's [keyboard shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), consulted 2026-10-24, name the tabs:
  - Switch Between Sample/Envelopes Tabs and Switch Between Notes/Envelopes/MPE Tabs: Option+Tab (Mac), Ctrl+Tab (Windows);
  - Switch to Sample/Notes Tab, to Envelopes Tab and to MPE Tab: Option+Shift+1, 2 and 3;
  - Toggle Full-Size Clip View: Cmd+Option+E (see the [layout contract](window-layout-contract.md)).
- Static reading of Live 12.2.1 (arm64):
  - The View builder adds `cont`, `enve` and `expr` (`ALiveApp::sMenuContentEditor`, `sMenuEnvelopeEditor`, `sMenuExpressionEditor`), on Option+Shift+1 to 3, after the clip panel layout items. Their item texts are in encrypted resources, not read.
  - `AClipContentHeaderViewModel` holds the editor's mode selectors with the Fold to Notes and Fold to Scale toggles. Its content mode is renamed by clip type (`OnRenameSetContentEditorViewMode`), and its expression mode is enabled only for MIDI clips.
  - `AClipContentFooterViewModel` holds the lanes under the editor, with a content lane menu and an expression lane menu. `LClipDetailView::OnShowNextEditorTab` and `OnShowPreviousEditorTab` step the tabs.
  - The built-in default Set keeps the MIDI editor's lane models: content lanes (one open, two minimized) and expression lanes (two open, three minimized). Which lane each type number is was not read.

## Halftrack's behaviour

- **The title bar.** It runs over the clip's boxes, in the clip's colour (dimmed while the clip is deactivated), with its activator and name. The activator deactivates and activates the clip, one undo step each. Its right-click menu has Rename, which opens the clip's rename dialog, the palette, which colours the clip, and Fold. An audio clip's Clip box no longer repeats the name.
- **Fold.** A double-click on the title bar, or its menu's Fold, minimizes the panels: the boxes hide, and the title runs down a strip at the clip view's left, its name reading upward, while the editor takes the width. Folding again brings the boxes back. The fold lasts while the window is open, for every clip.

- **The tabs.** The note editor's header starts with Notes, Envelopes and MPE. An audio clip's editor has Sample and Envelopes over its waveform. The tab shown lasts while the window is open, for every clip; an audio clip shown while MPE is chosen shows Sample.
- **Notes** shows the notes with the velocity, release velocity or chance lane under them, or no lane.
- **Envelopes** fills the editor with one lane under the ruler. Its chooser offers the clip's MIDI controllers under a MIDI Ctrl heading (pitch bend, the common controllers and any other controller), then its envelopes by device, for modulation and, on Session clips, automation. At first it shows the clip's first modulation envelope, or its pitch bend.
- **MPE** shows the notes with the lane of the chosen note expression under them: Note Pitch Bend, Note Slide or Note Pressure.
- **An audio clip's tabs.** Sample shows the waveform with its warp markers and loop. Envelopes draws the chosen envelope over it, its chooser beside the tabs. At first it shows the clip's first modulation envelope; it remembers the one shown while Sample shows. Choosing an envelope shows the Envelopes tab.
- **Switching.** Click a tab, or choose View > Notes (Sample for an audio clip), Envelopes or MPE. Option+Shift+1 to 3 choose them. Option+Tab (Ctrl+Tab on Windows and Linux) steps to the next tab, and Shift steps back. The items are greyed while no clip shows in the clip view, and MPE for an audio clip.
- **The header.** After the tabs come the lane chooser, Fold and Scale, as the reference's header holds the tabs and fold toggles. Halftrack's grid, note tools, velocity, the clip's scale and the new-note length follow. In a narrow editor the widest controls narrow first. Then the last that do not fit hide, and the clip name takes what room is left.

## Not yet

- The reference's stacked lanes: several content and expression lanes at once, each minimizable, with menus to choose them. Halftrack shows one lane at a time.
- The MPE Editor's pitch editing on the notes themselves, apart from the lane.
- Arranging the panels vertically, horizontally or automatically; the panels' grouping and names.

See [open questions](../open-questions.md) for the defaults taken where the reference was not read.

## Evidence in Halftrack

The UI suite checks:

- that the title bar runs over a MIDI clip's boxes and an audio clip's, naming the clip;
- that its activator deactivates and activates the clip, and its menu offers Rename and the palette, which colours the clip;
- that a double-click folds a MIDI clip's panels into a strip, the editor taking their width, and the menu's Fold unfolds them; and that folded, an audio clip's boxes hide and its waveform takes their width;
- that the Notes tab shows at first, its lane chooser offering velocity and chance;
- that the Envelopes tab fills the editor with the MIDI controllers and envelopes, and a click in its pitch bend lane bends;
- that Option+Tab steps to the MPE tab with the expression lanes under the notes, Shift+Option+Tab steps back, and Option+Shift+1 returns to Notes;
- that the View menu holds the three items;
- that an audio clip shows its Sample tab at first without envelopes, has no MPE item, and its Envelopes tab offers the clip envelopes;
- that the existing clip envelope and MPE checks work through the tabs.
