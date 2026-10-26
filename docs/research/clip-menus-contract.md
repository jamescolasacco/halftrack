# Clip, slot and scene menus contract

Status: partial. This follows a static reading of the reference's context menus. No reference output was measured.

## Evidence

A research agent read the reference statically on 2026-10-24 (Live 12.2.1, arm64 slice sha256 b37cb738…). It read:
- the Arrangement's builder, `LArrangerControlManager` vt+0x358 (0x10379c6c8), with its handlers and enable checks;
- the right-click selection, `LArrangerScrollable` vt+0x710 (0x1037c6280);
- the Session slot builder, `LSlotManager` vt+0x358 (0x103a38ce4);
- the scene builder, `LSceneManager::OnPopulateContextMenu` (0x1039e7c54);
- the grid menu, `AGridContextMenu` (0x10325d9b4).

Nothing was launched, no preferences or Sets were read, and nothing was decrypted. The English labels are in the reference's encrypted language file, so Halftrack's labels are its own plain names for the commands. The findings are in the ignored `artifacts/reverse-engineering/clipmenu/`.

The rules from the [track header menu](track-header-menu-contract.md) hold here too. A command that cannot act is left out. Items the reference adds with its "always" flag stay, greyed when they cannot act. A separator only comes between items.

## Arrangement clip and time-selection menu (2026-10-24)

### What a right-click selects

This follows the reference's (0x1037c6280):
- On a clip that is not selected, the clip is selected alone, and the time selection covers it on its lane.
- On a clip of the selection, or inside the time selection on a selected lane, the selection stays.
- Elsewhere, the selection gives way to the insert marker at the click. Halftrack snaps the marker to the grid, as its clicks do; whether the reference snaps it was not determined.

### Order

The reference's items, with what Halftrack shows:

| Reference item | When | Halftrack |
|---|---|---|
| Copy to Main Lane, then a separator | a take lane | the same, from a take lane's selection |
| Cut, Copy, Paste, Duplicate, Delete | a time selection with content; Paste when the clipboard fits | the same over the time selection (`copyRange`, `pasteRange`, `duplicateRange`, `deleteRange`), or over the selected clips when several are selected without one. Paste takes a copied time selection, or a copied clip of the track's kind |
| Zoom to Arrangement Time Selection, Zoom Back | a time selection; Zoom Back always | Zoom Back only after a zoom to return from, as Halftrack keeps no other view to go back to |
| Rename, Edit Info Text | one clip | the same: Edit Info Text gives the clip its own text (`setInfoText`, saved, one undo step), which the Info view shows while pointing at the clip |
| Extract Grooves | clips that are MIDI, or audio with a warped sample | the same: a warped audio clip's groove has an event at each transient (see the [grooves contract](grooves-contract.md)) |
| Deactivate / Activate Clip(s) | clips in the selection | the same, Activate when all are deactivated, in one undo step |
| Split | clips cut by the selection's edges | the same, or at the insert marker without a selection |
| Consolidate, Consolidate Time to New Scene | a time selection | the same; the reference also tests an engine state that was not decoded |
| Export MIDI Clip | one MIDI clip | the same |
| Insert MIDI Clip(s) | selected MIDI lanes with no clip in the selection | the same: a clip filling the selection on each lane, in one undo step |
| Video Window | a clip with video | the same |
| Show in Browser, Reveal in Finder, Show Similar Files | one audio clip with a file | the same: Show in Browser opens the sample's folder in the browser's samples, the sample selected |
| Create Fade (In, Out, Crossfade, Default Crossfade), Reset Fades (always shown) | the block for audio tracks that are not frozen; the fade named by the selection against the clips' edges | Create Fade In where the selection holds its audio clips' starts but not their ends, Create Fade Out the other way round, greyed "Create Fade" otherwise. Crossfades over a selection are not made yet. Reset Fades returns the clips' fades to the default, as Delete does to a fade handle |
| Loop Selection, or Activate / Deactivate Loop | always | the same |
| Freeze / Unfreeze Track(s), Bounce Track(s) in Place, Bounce to New Track(s) (always shown) | not on a take lane | the same, over the tracks with content in the selection; Bounce to New Track over the time selection (see the [freeze contract](freeze-contract.md)) |
| Crop Clip(s), Reverse Clip (always shown) | a clip under the pointer | the same (`cropRange`, `reverseRange`): Crop keeps the clips' parts inside the selection; Reverse splits at the selection's edges and plays the clips inside backwards, mirroring a MIDI clip's notes. The reference's keys give them too, Shift+Cmd+J and R ([Live keyboard shortcuts](https://www.ableton.com/en/live-manual/12/live-keyboard-shortcuts/), consulted 2026-10-24), over the time selection or a lone selected clip |
| Slice to New MIDI Track, Convert Harmony, Melody and Drums to New MIDI Track | one audio clip | Slice to New MIDI Track, with Halftrack's divisions. The conversions are not implemented, and 12.2.1 never shows stem separation here |
| the grid's items | not on a clip | Snap to Grid, the adaptive widths and fixed sizes in rows, Narrow and Widen Grid, and Triplet Grid, in the reference's order. The same items are in the editors' grid menus |
| the clip colour grid | on a clip | Halftrack's palette; a swatch colours the selected clips in one undo step (`setClipColours`), marking the colours they use |
| Copy LOM Path | one clip | left out: Halftrack has no object model for it |

Halftrack's own clip menu items are gone: the gain, fades and range dialog, splitting at the playhead, duplicating after a clip, and copying, cutting and pasting a clip. The reference's commands above take their place. The dialog still opens from a double-click on a fade handle, where it edits the fades' curves and lengths, which the reference drags on the clip.

### Evidence in Halftrack

The UI suite builds the menus for an audio clip, a time selection holding the clip's start, and an empty lane. It compares each with the reference's order. It also checks:
- the right-click selections;
- Show in Browser showing the sample's folder in the browser with the sample selected;
- a swatch colouring the clip, undone in one step;
- a clip's info text showing in the Info view while pointing at it;
- Create Fade In fading the clip over the selection's part, and Reset Fades returning it to the default;
- Crop Clip leaving the selection's part, and Shift+Cmd+J doing the same, and R reversing the selection;
- Bounce to New Track making a track after the clip's, removed with one undo.

The range edit suite checks Crop, Reverse and clip colours directly. It renders audio reversed sample for sample, mirrors a MIDI note, and checks refusals.

## Session slot menu (2026-10-24)

A right-click on a slot that is not selected selects it alone, its track and scene with it. On a selected slot, the selection stays (0x103a38af0).

| Reference item | When | Halftrack |
|---|---|---|
| Cut, Copy, Paste, Duplicate, Delete | a selected slot holds a clip; Paste when the clipboard fits | the same, over the selected slots (`copySlotClips`, `pasteSlotClip`, `duplicateSlotClip`, `clearSlots`) |
| Clear All Envelopes | a selected clip has envelopes | the same, in one undo step |
| Rename, Edit Info Text | one clip | the same: Edit Info Text gives the clip its own text (`setInfoText`, saved, one undo step), which the Info view shows while pointing at the clip |
| Extract Grooves | MIDI clips, or audio with a warped sample | the same |
| Deactivate / Activate Clip(s) | clips | the same |
| Export MIDI Clip | one MIDI clip | the same |
| Show in Browser, Reveal in Finder, Show Similar Files | one audio clip with a file | the same |
| Freeze / Unfreeze Track(s), Bounce Track(s) in Place, Bounce to New Track(s) (always shown) | a clip | the same. Freeze stays greyed for a track without Arrangement clips, as Halftrack's freeze renders the Arrangement only. Bounce to New Track renders the selected clips into the same slots of a new track (`bounceSlotsToNewTracks`; see the [freeze contract](freeze-contract.md#session-clips)) |
| Crop Clip(s) (always shown) | a clip that can be cropped | the same (`cropSessionClips`), greyed on an empty slot: each clip keeps only what it plays, its loop or its span from its start, which then begins it. A MIDI clip loses the notes outside, cut at the edges; a warped audio clip gets a new sample in the project's Assets holding that audio. Audio clips with warp markers stay greyed |
| Slice to New MIDI Track and the conversions | one audio clip | left out: Halftrack slices Arrangement clips only |
| Add / Remove Stop Button | any slot selected; Remove while one has a stop button | the same (`setSlotStopButtons`, one undo step): an empty slot without one shows no button, and launching it or its scene leaves its track playing, as a group slot's launch does |
| Insert MIDI Clip(s) | selected empty slots on MIDI tracks | the same, a four-beat clip in each |
| Create Follow Action Chain | a clip | the same (`createFollowActionChain`, one undo step), also on Cmd+Shift+Return; see the [follow actions contract](follow-actions-contract.md#create-follow-action-chain-2026-10-26). It comes after Insert MIDI Clip(s), as the menu builder adds it (0x103a39240, read 2026-10-26); this table had it before |
| Record on Launch | group slots only | left out: Halftrack's group slots have no menu yet |
| Select on Launch, the clip colour grid | a clip | the colour grid, colouring the clips in one undo step. Halftrack has no Select on Launch preference yet |
| Copy LOM Path | one slot | left out |

Halftrack's own slot items are gone: Launch Clip, Edit MIDI Notes, the audio clip gain dialog, Create MIDI Clip, Import Audio Here, Stop Track Clips and the follow action dialog. Each is still reached where the reference has it: a slot's launch button, a double-click into the clip view, the clip view's gain, Insert MIDI Clip, Create > Import File with a slot selected, the track's stop button, and the clip view's Launch box, which opens the follow action.

Away from any slot, below the last scene or under the scenes' column, the reference selects the track there and the last scene, and shows Insert Audio Track, Insert MIDI Track and Insert Return Track, then Insert Scene and Capture and Insert Scene (0x103a39ae0). Halftrack does the same. The reference also adds Paste there, but its handler is not on that view, so it is left out (low to medium confidence).

## Scene menu (2026-10-24)

The reference's order (0x1039e7c54), with what Halftrack shows:

| Reference item | Halftrack |
|---|---|
| Cut, Copy, Paste, Duplicate, Delete | Duplicate, and Delete while there is more than one scene. Halftrack has no scene clipboard yet |
| Rename, Edit Info Text | the same; the Info view shows a scene's text while pointing at it |
| Insert Scene, Capture and Insert Scene | the same. Insert Scene puts the new scene right below the selected one and selects it, in one undo step |
| Select Scene Content | the same: every slot in the scene is selected |
| Cancel Scene Launch, Select Next Scene on Launch, Record on Launch | left out: not implemented |
| Scene Tempo and Time Signature (a switch that shows them) | the same: it shows and hides the scenes' tempo and time signature fields, where a click edits them |
| Reset Color and the scene colour grid | the same (`setSceneColours`, one undo step, saved): a scene's cell shows its colour, and Reset Color takes it away |
| Copy LOM Path | left out |

Halftrack's own scene items are gone: Launch Scene, which the scene's launch button does, and Move Scene Up and Down: a scene drags in the scene column to a new place, as the reference's do.

### Evidence in Halftrack

The UI suite compares these menus with the reference's order:
- a filled MIDI slot's;
- an empty slot's, which offers Insert MIDI Clip;
- a scene's;
- the grid's away from any slot.

It also checks:
- a swatch colouring the slot's clip;
- Insert MIDI Clip filling the empty slot (the clip loops suite crops a Session MIDI clip to its loop, sounding as before, and an audio clip to a new one-second sample holding exactly that audio);
- Remove Stop Button taking the slot's away and offering Add Stop Button (the launcher suite hears a track play on through a scene's empty slot without one, and the slot itself launch nothing);
- Select Scene Content selecting the scene's slots;
- a swatch colouring a scene and Reset Color taking it away;
- Insert Scene going right below the selected scene;
- Bounce to New Track from a slot, removed with one undo.

## Not determined

- English labels.
- Whether the insert marker snaps on an empty-area right-click.
- Whether the header selection survives a right-click on a clip.
- The Cut and Duplicate fallback check, and the Consolidate engine state.
- The exact paste rules.
