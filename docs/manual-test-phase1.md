# Native preview manual test checklist

## Eight-minute user smoke test

**Before starting the timer:** build with `cmake --workflow --preset preview`, open the resulting application, choose an intentional audio output with no input enabled, and have an empty disposable folder ready. Compilation, dependency downloads, and device setup are outside this test. No microphone or external plug-in is needed. This is a procedure, not a recorded pass.

| Time | Action | Check |
| --- | --- | --- |
| 0:00–0:45 | Start a new project; click **+ MIDI** (Command+Shift+T / Ctrl+Shift+T). | A MIDI track, a 16-beat clip, piano editor, and real synth controls appear. |
| 0:45–2:00 | Draw C4, E4, and G4 on the same beat; press Space to play, then stop. | The chord produces synth output and real meters; transport stops when requested. If you cannot listen, record acoustic audition as untested. |
| 2:00–3:15 | Select a note; drag its body to a new pitch/time, drag its right edge, and change Velocity. Undo the last gesture once, then redo once. | Each edit affects the selected note; one undo reverses the whole gesture and redo restores it. |
| 3:15–4:15 | Play again; lower track volume, pan, and toggle Mute. Restore Mute, then change Saw/Square and filter cutoff. | The mix and timbre change; meters follow the real signal; the controls remain responsive. Stop playback. |
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
