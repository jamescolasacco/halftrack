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
