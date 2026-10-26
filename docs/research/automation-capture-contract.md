# Automation capture and override contract

Halftrack follows the reference's automation workflow in [Automation and Editing Envelopes](https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/), consulted 2026-10-21. The manual describes these behaviours:

- Automation Arm records manual changes while Arrangement Record is on.
- Moving an automated control otherwise overrides its automation, and Re-Enable Automation lights until every lane is reactivated.
- Mouse moves record as "touch": recording stops on release.
- Controller moves record as "latch": recording continues after release.

The arithmetic of recorded points is Halftrack's own.

## Override

Moving a control whose lane plays automation overrides that lane. This covers a track's volume, pan or sends, the master volume, and any native device or rack control ([send lanes](arrangement-automation-contract.md#send-lanes-2026-10-23)). The lane keeps its points and stops reading: playback follows the moved control, and the control keeps moving freely. Re-Enable Automation switches every overridden lane back on in one undo step. An override joins the move's own undo step.

Previously an automated fader was locked until its points were cleared. The Read switch in the automation editor shows the same state as an override.

## Capture

`setAutomationArm(true)` turns on Automation Arm. It is a transport setting saved with the project, outside the undo history. While it is on and the transport records, moving one of those controls writes its lane at the playhead instead of overriding it. Recording may run for automation alone, without an armed track.

A move starts a pass on that lane. The pass replaces the lane from its first written beat:

- a point 1/960 beat earlier keeps the earlier automation up to the touch;
- moves less than 1/64 beat after the previous point replace it;
- while the pass runs, the lane holds its last value ahead.

How a pass ends depends on how the control was moved:

- **Touch pass.** A pass inside an edit gesture (a held fader or knob) ends when the gesture ends. The earlier automation resumes 1/960 beat after release. The pass joins the gesture's undo step.
- **Latch pass.** Other moves hold their value until recording stops. They share one "Record automation" undo step per stretch of recording.
- **Loop wrap.** A wrap ends a pass at the loop end and starts another at the loop start.

The playhead is the transport position on the message thread.

## Interface

- `setAutomationArm(bool)` and `reEnableAutomation()`.
- `overriddenAutomation()` lists the overridden lanes by key ("master", "mixer:<track>:<control>", "send:<track>:<return>", "device:<plugin>:<parameter>"), and `reEnableAutomation(key)` re-enables one, in one undo step (2026-10-26). A lane that is still playing is left alone, with no undo step.
- `deleteAutomation(key)` is the reference's Delete Automation (manual, read 2026-10-26). It removes the control's Arrangement lane and its clips' automation envelopes in one undo step, so the control holds its own value; `hasAutomation(key)` says whether there is any to delete.
- `Snapshot::automationArm`, and `Snapshot::automationOverridden` (some lane with points is switched off).
- `record()` accepts Automation Arm in place of an armed track.

## User interface

- **Control bar.** Automation Arm (an envelope icon, red when on) and Re-Enable Automation (a returning arrow, orange when anything is overridden) sit after Record.
- **Faders.** Mixer and master faders stay live with automation. Their thumb is red while following a lane and grey while overridden, and the tooltip says which.
- **Device controls.** A native device's knobs and sliders show their lanes the same way: red while following, grey while overridden, with the same tooltips. The effects suite checks a Channel EQ control through following, overridden and cleared.
- **Control menus.** A control's context menu offers Delete Automation while it has automation, and Re-Enable Automation for that lane alone while it is overridden, as the reference's control menus do. This covers the track header's volume and pan and a native device's controls; the header's sliders show no menu when neither applies.
- **Cmd+Backspace** deletes the automation of the control clicked last, as the reference's key does.

## Evidence

`tests/AutomationCaptureTests.cpp` (`automation-capture-acceptance`) renders a tone and checks:

- **Track override.** Overriding a −12 dB volume lane plays the moved fader at −3 dB, and the fader moves freely. Re-Enable Automation restores −12 dB, and undo returns to the override, then to the lane.
- **Device and master.** The same for a Channel EQ Output lane and for the master volume, measured against a baseline.
- **No Automation Arm.** With no armed track and Automation Arm off, there is nothing to record.

With the synthetic audio device and Automation Arm on, it checks:

- **Automation-only recording.** Recording starts with no armed track.
- **Touch pass.** Over an existing −20 dB lane, the pass records −10 then −5 dB and ramps between them. The lane keeps its earlier and later points and is back at −20 dB after release.
- **Latch pass.** A pan move holds its value to the stop.
- **Device control.** Turning a device control records its lane.
- **No take.** No clip is created.
- **Undo.** Undo restores every lane.

The capture suite also re-enables an overridden volume lane alone while a device lane stays overridden, leaves a playing lane alone without an undo step, and undoes both. It deletes a volume lane together with a Session clip's volume automation envelope in one undo step, checks that deleting again changes nothing, and undoes it. The UI suite checks the live faders, their thumb colours, the override, Re-Enable Automation, the fader's menu offering Delete and Re-Enable Automation and re-enabling its lane (and no menu on the pan without automation), Cmd+Backspace deleting the fader's automation, undo and the Automation Arm button. The automation suite's earlier "locked fader" checks now assert overrides.

## Not implemented

- Recording into Session clips (the reference records Session automation as clip envelopes).
- Utility controls. (Third-party plug-ins' parameters are captured and overridden from their own editors as well as Halftrack's controls; see the [plug-in automation contract](plugin-automation-contract.md).)
- Measured comparison with the reference's recorded point density.
