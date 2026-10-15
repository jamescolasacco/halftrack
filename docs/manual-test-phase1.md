# Native preview manual test checklist

## Eight-minute user smoke test

**Before starting the timer:** build with `cmake --workflow --preset preview`, open the resulting application, choose an intentional audio output with no input enabled, and have an empty disposable folder ready. Compilation, dependency downloads, and device setup are outside this test. No microphone or external plug-in is needed. This is a procedure, not a recorded pass.

| Time | Action | Check |
| --- | --- | --- |
| 0:00–0:45 | Start a new project; click **+ MIDI** (Command+Shift+T / Ctrl+Shift+T). | A MIDI track, a 16-beat clip, piano editor, and real synth controls appear. |
| 0:45–2:00 | Draw C4, E4, and G4 on the same beat; press Space to play, then stop. | The chord produces synth output and real meters; transport stops when requested. If you cannot listen, record acoustic audition as untested. |
| 2:00–3:15 | Select a note; drag its body to a new pitch/time, drag its right edge, and change Velocity. Undo the last gesture once, then redo once. | Each edit affects the selected note; one undo reverses the whole gesture and redo restores it. |
| 3:15–4:15 | Play again; lower track volume, pan, and toggle Mute. Restore Mute, then change Saw/Pulse/Sine/Noise and filter cutoff. | The mix and timbre change; meters follow the real signal; the controls remain responsive. Stop playback. |
| 4:15–5:15 | Save as `Smoke.halftrack` in the disposable folder. | Saving succeeds and clears the unsaved marker. Note the chord positions and changed synth settings. |
| 5:15–6:15 | Start a new project, then reopen `Smoke.halftrack` through File → Open. | The saved MIDI track, notes, mixer values, and synth settings return. |
| 6:15–7:15 | Play the reopened chord; stop, move one note, then undo and redo that gesture. | The reopened project plays; editing and undo still work after loading. |
| 7:15–8:00 | Undo the final test edit, save, and close. Record the build/OS and any failed step. | The saved project closes without losing an unconfirmed edit. Mark untouched checks as NOT RUN. |

Stop and record a failure if a step cannot be completed; do not count a drawn control or a moving meter as proof of an untested behavior. This quick path checks the basic MIDI workflow. Export, external editors, media relocation, device changes, and hardware timing belong to the extended checks below.

## Extended QA

Use this checklist to record evidence for a particular build. It is not a claim that the checks have passed, nor a restriction imposed by a numbered roadmap. Automated, interactive, and physical-hardware results are separate.

Record the commit, operating system/architecture, build type, sample rate/block size, test IDs, actual result, and any generated artifacts. Do not put personal plug-in inventories, private projects, hardware serial numbers, or user recordings in public reports. Use generated fixtures and disposable project folders. Never capture an unattended microphone.

## Prepare

1. Build the same source on each platform with `cmake --workflow --preset preview`. On headless Linux, run `xvfb-run -a cmake --workflow --preset preview`; a virtual display is for tests, not physical audio verification. CI installs audio/font/X11/OpenGL development packages and a virtual display according to the [pinned framework dependency guide](https://github.com/juce-framework/JUCE/blob/37c894f83d379179b2070d437ccd0f1cd9af9576/docs/Linux%20Dependencies.md). See [building](building.md).
2. Run `python tests/realtime_audit.py`. Record its scoped result; review [known realtime gaps](realtime-safety.md) before making performance claims.
3. Generate the audio demo with `python scripts/create_demo.py /absolute/path/to/disposable-demo`. Keep a separate disposable folder for project saves and exports.
4. Open the built application. The automatic tests use a synthetic device and must not request microphone permission. Physical input checks below are optional, intentional tests with a generated loopback source.

## Interactive checks

Every row below begins **NOT RUN** for the candidate build. Fill in actual results and retain failed cases; do not substitute the presence of a control for its effect.

| ID | Action | Expected evidence |
| --- | --- | --- |
| UI-01 | Start from a fresh launch; leave it idle briefly, then close | Window is responsive, the untouched project is clean, and closing does not invent unsaved edits. Record any browser scan delay. |
| UI-02 | Import the four generated demo stems, then use Fit | Four source-derived waveforms appear; the complete arrangement is visible; track names and durations match the imported files. |
| UI-03 | Play, Stop, seek, and Play again | Transport advances only during playback; track/master meters follow real content; seeking changes the playback position. Meter movement alone does not prove acoustic output. |
| UI-04 | Change volume and pan; mute one track; solo one track | The chosen state is visible and changes the mix. Compare a rendered result with the generated source when acoustic monitoring is unavailable. |
| UI-05 | Drag one audio clip, then undo/redo | One undo restores the entire drag, one redo restores the final position, and the clip remains associated with its source. |
| UI-06 | Adjust a loop range over more than half a second; undo/redo; enable looping | A drag is one undo step. The enabled range repeats at its displayed boundaries; record any audible gap separately. |
| UI-07 | From a new project, click + MIDI or use Command+Shift+T; open Piano Roll | A real MIDI track, 16-beat empty clip, and built-in synth controls appear. The editor follows the selected clip. |
| UI-08 | Draw C4/E4/G4; select a note; drag its body across time/pitch and its right edge for length; change Velocity; Delete; undo/redo each action | Playback/export reflects each edit. One undo restores a complete gesture, including note identity. Check clip boundaries, Snap 1/8, Option/Alt bypass, arrow-key grid/semitone moves, and Shift+Up/Down octave moves. |
| UI-09 | Add overlapping notes, play, then Stop and replay | Polyphonic sound is produced by the built-in processor. No voice remains stuck after Stop. An empty MIDI clip remains silent. |
| UI-10 | Change waveform, filter cutoff/resonance, envelope stages, and level while playing | The controls alter real processor settings and audible/rendered output. Exercise range endpoints: cutoff 20–20000 Hz, resonance 0–1, attack/decay 0–10 s, release 0–20 s. The UI stays responsive; undo restores settings. Record clipping or dropouts separately. |
| UI-11 | Switch Arrangement/Piano Roll and audio/MIDI selections; double-click a MIDI clip; use + MIDI Clip at the snapped playhead; resize to 1100×720; scroll pitch/time and use Fit/+/- | Arrangement previews show actual notes; double-click restores the matching note editor; + MIDI Clip adds four bars. MIDI tracks hide unsupported audio input/arm controls. Both editors remain usable; no other track is edited accidentally. |
| UI-12 | Discover the generated VST3 gain fixture, insert it, open its editor, change gain, bypass and remove it | Editor state changes actual rendered output; bypass restores dry processing; closing/removing/reopening editors does not crash or leave orphan windows. Do not infer arbitrary plug-in compatibility. |
| UI-13 | Save a mixed audio/MIDI project; close and reopen it; after using the note editor, delete a MIDI track, undo/redo, then create another MIDI track | Notes, synth settings, track/clip state, loop/tempo, external plug-in state, and rendered output match the saved state. Save clears dirty state; track deletion/undo preserves stable selection identities without a stale editor. |
| UI-14 | Copy the project and sibling Assets folder elsewhere; reopen the copy after moving the original test folder away | Collected audio resolves in the moved folder. MIDI needs no external media. No absolute path to the old folder is required. |
| UI-15 | Attempt to open an unsupported-version project or one with deliberately missing generated media | An actionable error appears; the already-open session and unsaved edits remain intact. |
| UI-16 | Close after editing; exercise Cancel, then Save, then a separate Discard case | Cancel retains the session; Save only proceeds after successful writing; Discard acts only when explicitly selected. Cancelling a file chooser must not discard edits. |
| UI-17 | Export a dry mix at the selected rate/depth and zero extra time; decode it independently | Confirm channel count, rate, bit depth, duration, non-silent signal, and mixer/plug-in effects. Record the decoder used. |
| UI-18 | Export with two seconds of extra time | Duration grows by exactly two seconds. A dry source should yield silence afterward; use a known delay/reverb fixture separately to test actual tail preservation. |
| UI-19 | Export over an existing disposable destination and cancel while progress is active | The window continues processing input, cancellation completes, and the existing destination bytes remain unchanged. Repeat with a newly chosen path and check no incomplete final file is published. |
| UI-20 | Open Audio Setup with no input chosen; change output and block size deliberately | The choices reflect the available backend; unavailable/removed devices fail clearly; the UI remains responsive. Record actual configuration without claiming measured latency. |


## Session and launcher checks

Each row below is **NOT RUN** for the candidate build until executed and recorded. Use generated audio, a disposable project, and an intentional output. The automated UI suite calls production hit testing and event handlers with an injected synthetic device; it does not establish native mouse delivery or acoustic output on a physical device.

| ID | Action | Expected evidence |
| --- | --- | --- |
| UI-21 | Open a new project; switch Session, Arrangement, and Session again with the tabs and Tab key | Session shows eight real empty scene rows and tracks as columns. Switching views alone changes no playback ownership. |
| UI-22 | Double-click an empty audio slot to import a generated WAV; separately drop another generated WAV onto a chosen empty slot and activate a browser file with that slot selected | Each clip appears in the addressed track/scene. Audio comes from that source. Drop the same file onto an occupied slot: an error preserves the existing clip; Clear Clip followed by import is the explicit replacement path. |
| UI-23 | In Session, insert a MIDI track or double-click an empty MIDI slot; draw notes in the opened piano editor, move/resize one, then undo and redo | A real four-beat slot clip is edited. Return to Session and reopen the same clip: notes and identity are retained. An occupied MIDI slot opens for editing without replacing its content. Select an empty slot and open Piano Roll: it must not open another row's clip. Dropping audio into this editor gives an actionable error without changing arrangement or slot content. |
| UI-24 | Set quantization to None and launch a clip using its triangle, then launch a selected slot with Enter | The addressed clip plays and the visible state follows the engine. Record actual acoustic output separately from meter or state movement. |
| UI-25 | While transport is already playing, select 1 Beat and then 1 Bar; request a different clip shortly before each boundary | A queued state precedes the actual transition when a wait is required. Only one Session clip on that track plays after transition. Record timing error through a generated capture if measuring it; a visual check does not prove sample accuracy. |
| UI-26 | Put audio and MIDI clips in a scene, leave another track's slot empty, and launch the scene using its triangle and Shift+Enter | Filled slots launch together under the chosen quantization. The empty slot stops that track's previous Session clip or silences its arrangement at the same boundary. It remains silent after Stop/restart until Return to Arrangement. No clip from an unrelated row is launched. |
| UI-27 | Stop a track using the strip below its slots; launch again, then use Stop All Clips | The requested Session clips stop under the selected quantization. Queued/stopped indicators reflect actual engine state and no note remains sounding indefinitely. |
| UI-28 | Play arrangement content, override a track by launching its Session clip, switch to Arrangement, then use Return to Arrangement | Merely switching views leaves the Session override intact. The explicit return command releases it and the arrangement becomes the playback source again. |
| UI-29 | Add a scene; rename it by double-clicking its scene name or its menu; delete it; undo and redo; repeat using the native Session menu | Scene names, clip row alignment, and undo state remain correct. Deleting the selected scene moves selection to valid remaining model state without editing another clip. |
| UI-30 | Scroll a grid with more tracks/scenes than fit, use arrow keys to move slot selection, and resize to 1100x720; operate the column volume/pan/mute/solo controls | Scene launches remain aligned with rows, mixer controls with tracks, and keyboard selection scrolls into view. Selected-track devices and the piano editor follow the chosen clip. |
| UI-31 | Save a mixed arrangement/Session project, close and reopen it, then relocate its project plus sibling Assets directory | Scenes, slots, notes, source media, quantization, and mixer values persist. The reopened project does not resume a stale performance launch automatically; explicit launch still works. |
| UI-32 | Edit slot notes, return to Session, delete the track, undo/redo, add another track, then delete/undo a scene containing a clip | Editor/selection references remain valid; no stale clip is edited and the application remains responsive. |
| UI-33 | While Session clips are active, choose Export Arrangement as WAV; compare the export with arrangement content and inspect playback afterward | The export contains the arrangement, as the command states. Session performance ownership is restored afterward; do not report this as Session performance capture. |

## Sends and return checks

These additions are **NOT RUN** through native mouse/keyboard delivery. Use a disposable project with drawn synth notes or generated audio; the automated signal/handler tests are separate evidence. This optional routing walkthrough supplements the short core smoke path.

| ID | Action | Expected evidence |
| --- | --- | --- |
| UI-34 | Choose Insert Return Track from the Track menu; rename it and select an ordinary audio or MIDI track | The return is visibly identified, its input/recording controls are absent, and the selected source has a Sends control bearing the return's name. New sends start Off. |
| UI-35 | Play the source, raise its send, then move the source fader while changing the destination return between Post and Pre | Post follows the source fader and pan; Pre retains its separate feed. Muting the source silences its Post feed while Pre remains. Restore conservative listening levels before further tests. |
| UI-36 | Add an intentionally selected, scanned effect to the return and feed it from two sources | Both sources reach that one effect. Bypassing/removing it affects the shared return; it does not replace source processing. Record the actual plug-in tested. |
| UI-37 | Solo a source, then a return; separately mute the return | Source solo retains return output. Return solo exposes the incoming send mix while other dry outputs are silent. Return mute silences its output. Combined source/return solos follow the documented working default. |
| UI-38 | Drag one send through several values, release, then Undo and Redo; separately change Pre/Post, rename and delete/restore the return | Each edit restores its prior state, and the send still targets the same return after rename or prior-track deletion. A deleted return leaves no orphaned audible feed. |
| UI-39 | Switch Arrangement/Session, resize to 1100x720, and select audio, MIDI and return tracks | Sends remain reachable without covering devices. Returns expose their destination mode and do not offer playable clips or recording inputs. A clip drop on a return preserves the project and reports the unsupported destination. |
| UI-40 | Save with a send at 0 dB and another lower, then reopen and relocate the project plus Assets folder | Routing destinations, levels, Pre/Post, mixer state and media survive. Playback matches the saved mix. |
| UI-41 | Open a return effect's editor, delete the return, then Undo/Redo | The editor closes before its processor is removed. The restored return opens the actual editor again and remains connected to the intended sources. |

## Multiple-note editing follow-up

These optional checks are **NOT RUN** through native mouse/keyboard delivery. Automated component-handler tests are recorded separately. Use a disposable MIDI clip with three notes at different pitches and times.

| ID | Action | Expected evidence |
| --- | --- | --- |
| UI-42 | Click one note, Shift-click another, then Shift-click it again. Shift-drag from empty grid space across two notes. Try Cmd/Ctrl+A and Escape. | Shift toggles individual membership. The marquee adds intersecting notes without drawing new ones. Select All selects the clip's notes; Escape clears. Ordinary empty-space click still draws. |
| UI-43 | Select two notes, drag a note body, then use arrows and Shift+Up/Down. Move toward the clip edge and the pitch limits. Undo and Redo. | The notes move by the same bounded amount, preserving spacing and intervals. Each completed drag or key edit undoes together. Octave keys request twelve semitones within the shared range. |
| UI-44 | Drag a selected note's right edge, then change Velocity. Undo/Redo each edit. | All selected lengths change by the same allowed amount. Velocity sets every selected note to the displayed value. Each gesture undoes together; unselected notes stay unchanged. |
| UI-45 | Delete a selection, Undo/Redo, switch clips and reopen a saved project. | Deletion is one undoable action. Stale selection IDs do not select unrelated notes or edit another clip. Notes and their musical edits persist; editor selection itself is not stored as project content. |

## Note clipboard and Save As follow-up

These optional native-input checks are **NOT RUN**. Use a disposable mixed audio/MIDI project.

| ID | Action | Expected evidence |
| --- | --- | --- |
| UI-46 | Select two MIDI notes, Copy with Cmd/Ctrl+C, click the ruler, then Paste with Cmd/Ctrl+V twice. | The orange insertion marker sets the first paste location. Relative timing, pitch, length and velocity remain intact; each paste selects only its new notes and advances the marker by the whole group extent. |
| UI-47 | Duplicate a group with Cmd/Ctrl+D, then Cut with Cmd/Ctrl+X and Paste. Undo/Redo each musical edit. | Duplicate begins after the selected extent on the grid. Each operation undoes together. Copying alone does not dirty the project. The notes clipboard stays within Halftrack and survives clip changes. |
| UI-48 | Attempt to paste or duplicate a group beyond the clip end. Then edit a velocity text field using ordinary text shortcuts. | The out-of-bounds group is rejected without partial insertion, lost selection or discarded redo. Text commands do not edit notes. |
| UI-49 | Undo a note edit in a project containing audio, Save As into another folder, then Redo and Undo. Repeat with the same project basename and play arrangement/slots before reopening. | Saving preserves the real note history, adds no musical undo step and relinks current playback to collected media. Keep the new project and Assets folder together. |

## Intentional hardware checks

Run these only with an intentionally supplied generated source and selected hardware. Do not use ambient microphone input. Initial status for every platform is **NOT MEASURED** unless a specific report says otherwise.

| ID | Procedure | Record |
| --- | --- | --- |
| HW-01 | Feed a generated loopback signal into a chosen input, assign it, arm, record, stop, and replay | Device/backend class, sample rate, block size, captured duration, nonzero content, start alignment, and whether the take can be saved/reopened. Do not publish a personal device inventory. |
| HW-02 | Check monitoring Off, Auto, and In against arm/play/record state | Actual input audibility or loopback measurement, not only button state. Avoid an acoustic feedback path. |
| HW-03 | Measure a generated impulse round trip at multiple supported block sizes | Latency in samples, measurement method, driver-reported values, and alignment error. The synthetic CI device reports zero latency and cannot establish this. |
| HW-04 | Sustain playback while editing settings and adding/removing supported processing; run a longer intentional recording | Duration, track/voice load, deadline misses/dropouts, CPU distribution, memory growth, and recording completeness. An average CPU meter is not a worst-case deadline measurement. |
| HW-05 | Stop safely, disconnect/reconnect the selected test interface, then resume | Clear unavailable-device state, successful recovery, unchanged project, and no unintended input activation. |

## Report format

For each exercised ID, write `PASS`, `FAIL`, or `BLOCKED` with the exact observation. Keep `NOT RUN` for every untouched row. Include the commit and platform on every report, and link generated evidence where useful. A passing Mac check does not advance Windows or Linux status. Headless CI does not complete the GUI or hardware rows.

Before handoff, list the remaining failures and unsupported controls plainly. The known dependency locks in the realtime audit remain a limitation even if this checklist passes.


## Oscillator controls

Optional two-minute check, **NOT RUN** through physical mouse/audio delivery: create a MIDI track, draw a sustained note, and play it. In the built-in instrument choose Saw, Pulse, Sine and Noise; each should sound different. Pulse enables its width control: 100% is the existing square, smaller values narrow it, and zero silences its oscillator after any filter tail. Other modes disable width but retain its value. Change width, undo, redo, save and reopen; the mode/value and sound should return. Use a disposable project and an intentional output. Automated fixtures separately cover production UI handlers and complete rendered stereo buffers.

## Channel utility

Optional three-minute native-input check, **NOT RUN**: use a disposable audio or MIDI project and an intentional output. Choose **+ Device → Utility (Built-in)**, open it, adjust Gain/Balance and undo one drag. Check Left/Right/Swap and independent polarity; Left/Right should disable stereo controls without losing their values. Compare Width, Mid / Side and Mono, then enable Bass mono and Audition bass. Toggle DC filter and Mute. The device's context menu moves it left/right through insert effects; its chain buttons support bypass/removal. Save, reopen, undo/redo changes, and remove it while its editor is open. Values, device order and audible behavior should follow the model, and its window should close safely. Avoid extreme gain on an uncontrolled output. Automated tests separately cover known generated signals and production handlers.

## Native effects check (about four minutes)

1. On a track containing generated or chosen audio, open **+ Device** and add **Eight-band EQ**. Raise a bell band and confirm the response plot and sound change. Change it to Low Cut; gain becomes inactive while frequency and Q still operate. Undo the gesture once.
2. Add **Compressor**. Lower threshold, change attack/release and compare Peak/RMS. Bypass it and restore it. Move it before and after an EQ output boost; the dynamics should change.
3. Add **Stereo Delay**. Set short left/right times, raise wet mix, then compare linked and independent times and ping pong. Switch Time mode to Project tempo, try straight/dotted/triplet divisions, and change the transport tempo; repeats should follow it. Enable Freeze after feeding audio; new input stops entering the delay. Disable Freeze again.
4. Put **Room Reverb** on a return and send the source to it. Compare pre-delay, short/long decay, size, damping and width. Export with several seconds of tail and confirm decay continues after the source ends.
5. Save/reopen the project and confirm all four devices and settings return. Remove one with its editor open, undo, and reopen its editor.

Automated DSP, model and editor-handler tests are separate from this native mouse/audio walkthrough. This section remains NOT RUN until explicitly recorded.

## Piano-roll transformations (about two minutes)

Draw or select several notes and open **Note tools**. Try Quantize Starts, Quantize Ends, Reverse Time, Invert Pitch, Legato, Compress Time by Half, Stretch Time to Double and Set Fixed Length. The last operation uses the Length chooser; quantization uses Snap. Undo each operation once and verify the whole group returns. Cmd/Ctrl+U quantizes starts; adding Shift quantizes endings. With no selection, these tools apply to all notes in the current clip. A stretch that exceeds clip bounds reports an error and leaves every note unchanged. Repeat in a Session MIDI slot, then save/reopen. Native input walkthrough remains NOT RUN; production handlers are tested separately.

## Audio clip editing (about three minutes)

1. Import a disposable audio file into Arrangement. Double-click its clip (or right-click and choose **Gain, fades and range...**). Change gain, fade durations and curves; the envelope should follow the fades. Undo a slider drag once. Close the editor, select the clip and drag its two orange timeline handles; each drag should undo in one step.
2. Set Length shorter than the source and Source offset inside it, then **Apply range**. Playback should use that source region. An overrun or fades longer than the new range must report an error without changing the clip.
3. Place the playhead inside the clip, outside its fades. Right-click **Split at playhead** (Cmd/Ctrl+E); the right half should become selected. **Duplicate after clip** (Cmd/Ctrl+D) should create a new adjacent copy. Undo each action once, then redo.
4. Overlap two clips and give the first a fade out and the second a fade in. Export, save/reopen and verify settings and sound remain consistent.
5. Import audio into a Session slot and use **Audio clip gain...**. Gain should affect launch playback immediately; manual fade/range controls are disabled there.

Native mouse/audio walkthrough: NOT RUN. Generated full-buffer and production-handler tests are separate evidence.

## Arrangement audio clipboard additions

Native input checks below are NOT RUN until performed. Use generated audio and disposable projects.

1. Select an Arrangement audio clip; set gain, fades, source offset and duration in its editor. Copy it with Cmd/Ctrl+C, select another audio track, place the playhead, then Cmd/Ctrl+V. The copied sound/range is retained, the original stays in place, and the new clip is selected.
2. Cmd/Ctrl+X cuts that copy. Undo restores it in one step; undo again removes the preceding paste. Redo both. Copy adds no undo step.
3. Copy another clip, create a new project, and paste onto its audio track. Save, reopen, then relocate the project together with its Assets directory. Audio remains available. Right-click empty timeline space and choose Paste audio here to place another copy.
4. In a text field, use C/X/V shortcuts with Cmd/Ctrl. Text editing must not copy, remove or paste audio. In the piano roll, note clipboard shortcuts retain their existing behavior.

## Direct audio trim and source slip additions

Native input checks are NOT RUN until performed. On a disposable imported clip, make a shorter range inside its source using the clip editor, then select it in Arrangement.

1. Drag either left or right edge near its lower white handle. Left changes start and source offset together; right changes length only. Grid snapping applies; Alt allows free timing. Undo and redo each complete drag in one step.
2. Set both fades and drag an edge inward beyond their combined length. The retained range must still contain both fades. Extend outwards: neither source beginning nor source end can be crossed.
3. Shift+Alt-drag the body: the waveform moves inside fixed timeline bounds. Play/export to confirm a different source region is heard. Undo restores the original region.
4. Body dragging still moves the clip. Fade handles still edit fades. Delete and replace a clip during an in-progress gesture in an automated fixture: the replacement must remain untouched.

## Cross-track audio dragging

NOT RUN through native input until performed: drag an Arrangement audio clip by its body onto another audio track with a different mixer level. The sound must follow that destination's mixer; gain/fades/source region stay attached to the clip. Undo restores the original owner and timing in one step; redo restores the move. Dragging over MIDI/return rows must not move audio into them. Save and reopen to retain the final track placement.


## Named song sections (about one minute)

Native input/audio walkthrough: NOT RUN until performed. In Arrangement, place the playhead and choose **Add locator**. Create two more; click their flags and use Previous/Next to jump between sections without wrapping. Double-click a flag to edit its name and position. Drag it on the grid, then hold Alt for a free position; each complete drag undoes once. Press Delete with the locator selected, undo, and save/reopen to recover the same named positions. Resize the window: the locator buttons must remain in the track header. A marker beyond the end of all clips must not add silence to an export. Shift-drag on the upper ruler still edits the loop range.


## Audio reversal (about one minute)

Native input/listening check: NOT RUN until performed. Import a disposable recording into Arrangement and use its editor to expose an interior source region, with gain and fades. Enable **Reverse audio**, then immediately export: the same region should play backwards with unchanged placement and envelope. Undo once and export again; redo and export immediately. Duplicate the reversed clip, then save/reopen and listen. Switching reversal off restores the forward region and enables trimming/slipping/splitting/copying again. Unsupported clips disable the toggle. The waveform should display the reversed source region; original media files must remain untouched.


## MIDI file interchange (about two minutes)

Native chooser/input/listening walkthrough: NOT RUN until performed. Choose **File → Import MIDI File...** with a type 0/1 note file. Each source-track/channel part should get a new MIDI track at the captured playhead beat, and the first clip should open in the piano roll. Playback follows the project tempo. Edit a note, then choose **File → Export Selected MIDI Clip...** and reopen the resulting file in a disposable project. Note timing/velocity and supported controller events should survive. Undo import once to remove all imported parts; redo and save/reopen to restore them. Export itself must add no document undo step. A file with missing note-offs, SMPTE timing or type 2 should report an error without changing the project. The initial importer does not apply file tempo/meter maps or resolve overlapping same-pitch/channel notes.


## Whole MIDI clip clipboard (about one minute)

Native input/listening check: NOT RUN until performed. In Arrangement, select a MIDI clip and use Cmd/Ctrl+C, then place the playhead on another MIDI track and paste with Cmd/Ctrl+V. Paste selects an independently editable clip. Cmd/Ctrl+D duplicates after it; Cmd/Ctrl+X cuts it. Undo/redo each operation once. Copy the clip, create a new project at a different tempo, add a MIDI track and paste: its duration in beats must be unchanged. Save/reopen. Copying an audio clip replaces the whole-clip MIDI clipboard and vice versa; text and piano-roll note commands keep their own scope. A right-click clip deletion must still target the clicked clip if selection changes before the menu action runs.

Optional MIDI movement check (about 30 seconds; native input not yet verified): create two MIDI tracks with different synth waveforms. Drag a MIDI clip from the first to the second, then play it: the destination instrument should sound. Drag it through both rows and release; Undo once should restore its original track and time. Dragging it over an audio or return row should leave it on a compatible track.

Optional track order check (about 30 seconds; native input not yet verified): select a track and use Track → Move Selected Track Earlier/Later, or right-click its header for up/down (Arrangement) or left/right (Session). Switch views and verify the same order, selected clip/slot, instruments and return sends. Undo once should restore the previous order.

Optional track duplication check (about 45 seconds; native input not yet verified): right-click an audio or MIDI track header and choose Duplicate track. Confirm its clips, devices and Session slots appear on a new adjacent track. Edit the duplicate's notes or device settings and confirm the original stays unchanged. Undo the edit, then undo once more to remove the whole duplicate. A duplicate of an armed audio track should have no input assignment or record arm.

Optional tempo check (about 30 seconds; native input not yet verified): double-click BPM, enter 500.5, then 999. Draw/play a short MIDI phrase and verify the faster tempo. Undo once to restore the previous value. Entering “300 trailing” or a value over 999 should restore the current display. Save/reopen should retain the chosen valid tempo.


## MIDI controller recording (about three minutes)

Status: **NOT RUN with a physical controller**. Generated-controller and production component-handler tests are separate evidence. Use a disposable project and a controller you intend to enable.

1. Add a MIDI track. Choose its controller from the input menu; it should assign on the first selection and remain disarmed. Choosing All MIDI Ins intentionally enables all currently available inputs.
2. Select In and play a few keys while stopped. Select Off and repeat; the input should be silent. Select Auto; it should sound only with the track armed.
3. Start recording, play separated notes and a sustain-pedal phrase, then Stop. Open the resulting Arrangement MIDI clip and move/resize a recorded note. Undo that edit, then undo the take and redo it.
4. Return to the same section, record more notes and Stop. The overdub should join the overlapping clip. One Undo should remove only the added take.
5. Save, close and reopen. Notes should remain editable. The track should reopen unarmed with monitoring Off. Export the selected MIDI clip and the Arrangement WAV if desired.
6. Record a held note, then click another timeline position. The take should finish before the seek, with the held note ending at recording stop. Input/arm/monitor changes and history are unavailable during a take.

This path currently records into Arrangement and uses one native clip channel. MPE, input channel filters, explicit record quantization, take lanes, physical timing/hot-plug and Session performance capture remain unverified or unfinished.


## Metronome and count-in (about two minutes)

Physical listening/controller check: **NOT RUN** until performed. In a disposable project, enable transport **Click**, play and change BPM. Use the adjacent **...** menu to change accent, click level and **Only while recording**; that last option should silence ordinary playback. Set **Count-in → 1 bar**, select/arm an intended input, then record from the beginning and later in the timeline. The display should count down before capture. Count-in works with Click switched off. Notes played entirely before capture should be omitted; a note held into capture should begin at its start. Stop during count-in to cancel without an audio clip. Finish a take, then undo/redo it once. Save/reopen and verify click settings, and export a recording: the monitoring click should be absent. Current count-in is 4/4 and uses the main output.


## Recording quantization (about one minute)

Physical controller/input check: **NOT RUN** until performed. Choose **Edit → Recording quantization → 1/16 note**, arm a MIDI track and record a slightly loose phrase. After Stop, note starts should align to sixteenth notes, while held lengths remain unchanged. Overdub onto an earlier deliberately off-grid note: only the new notes should move. Undo once removes the new take, redo restores it. Save/reopen retains the grid. Choose Off for the original unquantized workflow. Snapping clamps at clip edges to preserve note lengths; enabled recording quantization currently rejects loop recording and targets with looped/offset/grouped/multiple-take MIDI clips.


## Master volume (about 30 seconds)

Physical listening/input check: **NOT RUN** until performed. Play a phrase with a return send, adjust the bottom-right Master fader and check that the complete mix changes level. Drag to the bottom for silence; double-click to restore 0 dB. Enter -6 dB, then undo/redo. A full drag should undo once. Save/reopen retains the value, and Arrangement export uses the same output level.


Optional monitoring release check (about one minute; physical test **NOT RUN**): hold a key or sustain a note, then switch monitoring Off. The note should enter its release rather than remain stuck. Repeat by disarming Auto, using undo/redo of those controls, and moving/disconnecting the input. Changing an unrelated volume setting and undoing it should leave a held note sounding.


## Tap tempo (about 30 seconds)

Physical rhythmic input check: **NOT RUN** until performed. Click transport **Tap** to a steady beat. The first click leaves BPM unchanged; subsequent clicks update it. Tap a new rhythm after a pause over three seconds. Undo/redo a tempo change, save/reopen, and confirm only the BPM persists. The control should work while stopped without starting playback.


## Clip activation (about one minute)

Native input/listening check: **NOT RUN** until performed. Select an audio clip in Arrangement, press **0**, and confirm its label shows **OFF /**, playback is silent after any effect tail, and the clip stays in place. Toggle again and repeat with a held MIDI note. Undo/redo while playing; each change should take one step and other tracks should continue. Repeat in Session using the context menu: a disabled clip retains its slot and launch ownership, and re-enabling resumes future events. Save/reopen the disabled state. Typing 0 into a number or name field must not disable a clip.


## Split MIDI notes (about 30 seconds)

Native input/listening check: **NOT RUN** until performed. Draw two long overlapping notes, click the piano ruler halfway through them, and press **Cmd/Ctrl+E**. Both notes should split at the marker; the new right halves should be selected. Undo should rejoin both in one step. Repeat after selecting only one note, and in a Session slot. Listen for the new note onset at the split. The Note tools menu offers the same operation.


## Rename clips (about 30 seconds)

Native input check: **NOT RUN** until performed. Select an audio or MIDI clip in Arrangement and press **Cmd/Ctrl+R**, or choose **Rename clip…** from its context menu. Enter a name, confirm, then undo/redo. Repeat with a Session slot and cancel once. Save/reopen and confirm both names persist. The audio source filename must stay unchanged.


### Copy a Session scene

Put an audio clip and a MIDI clip in one scene, rename a clip, and deactivate another clip in that row. Launch the scene. Use its context menu **Duplicate Scene** (or **Session → Duplicate Selected Scene**): the copy appears immediately below and becomes selected, while the original keeps playing. Launch the copy and verify its sound and inactive clip match. Edit a copied note and confirm the original is unchanged. Undo/redo the copy as one step; save and reopen to retain both rows without starting playback.


### Reorder Session scenes

With a scene playing, choose **Move Scene Up/Down** in its context menu or **Move Selected Scene Up/Down** in Session. The entire row moves together, its selection and notes remain attached to it, and playback continues. Queue another scene with beat quantization, move it before the boundary, and confirm the queued clip still launches. Undo/redo a move, then save/reopen and verify row order.


### Activate and deactivate MIDI notes

Select several piano-roll notes and press **0**, or use **Note tools → Deactivate Selected Notes**. Those notes turn gray and stop triggering while the clip and other notes remain enabled. Press 0 again to restore them, then undo/redo. Duplicate a muted group and verify the copy is also muted. With a mixed selection, 0 activates every selected note. Save/reopen to retain note states.
