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

**Session clips (2026-10-26).** The override also covers the control's automation envelopes in its track's Session clips, as the manual describes (read 2026-10-26):

- Moving the control bypasses them in the move's undo step, so the clip playing stops setting it.
- Re-Enable Automation, and the control's own Re-Enable Automation, switch them back on.
- Launching a clip again re-enables the envelopes of the controls it automates, as the manual's relaunch does. Like the launch, this is no undo step.
- Recording a Session pass into a control re-enables its envelopes.
- An engine patch makes a bypassed clip curve leave its parameter alone ([dependencies](../dependencies.md#tracktion-clip-automation-bypass-patch)).

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

## Session clips (2026-10-26)

As the reference's manual describes (its automation chapter, read 2026-10-26), Automation Arm also records into Session clips while Session Record runs:

- **Which clips.** Moving a track's volume or pan, or a control of a device directly on the track, writes the automation envelope of the Session clip the track overdubs (an armed MIDI track whose slot held a clip). With Preferences' Session Automation Recording at All Tracks, the playing Session clip of any track takes the moves too. Armed Tracks is the default, as the reference's (static reading: `mpSessionAutomationRecordingInArmedTracksOnly`, on).
- **Where.** At the clip's place in its loop, from the beats its launch has played; an unlinked envelope at its own loop's place.
- **Touch.** A move inside an edit gesture (the mouse) records until release. The envelope the clip had resumes 1/960 beat later.
- **Latch.** Other moves (a controller) hold their last value to the loop's end.
- **Loop wrap.** The loop coming round ends a pass, and the next move starts another over what the clip now has.
- **A control without an envelope.** The clip's new envelope starts at the control's value before the move, so the parts of the loop no pass reached keep it.
- **Points.** As in Arrangement passes, moves less than 1/64 beat apart replace the last point.
- **Undo.** A gesture's pass joins the gesture's undo step. Other moves share one "Record automation" step until Session Record stops.
- Without Session Record, moves override as before. Arrangement recording still writes lanes.

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

The session-record suite overdubs a MIDI clip with Automation Arm on and checks:

- **Touch.** A drag from −12 to −6 dB lands at the clip's beats 1 and 1.5, with 0 dB, the volume it had, before and after.
- **Latch.** A controller's move to −20 dB holds to the loop's end.
- **Undo.** Each pass is one undo step; redo brings them back. The clip's notes are untouched.
- **Tracks not armed.** A playing audio clip on an unarmed track records nothing by default. With All Tracks it records the move from the volume the track had. With Session Record off, nothing is recorded.

The preferences suite reads All Tracks back, and the UI suite finds Session Automation Recording on the Record page, at Armed Tracks, and switches it.

With the synthetic device, a Session clip whose automation sets its track to −12 dB plays there. The suite then checks:

- moving the fader to −3 dB overrides the clip's automation, listed by its key, and the clip plays at −3 dB;
- Re-Enable Automation brings back −12 dB;
- after another override, launching the clip again brings back −12 dB;
- the control's own Re-Enable Automation is one undo step, and undo overrides it again.

The capture suite also re-enables an overridden volume lane alone while a device lane stays overridden, leaves a playing lane alone without an undo step, and undoes both. It deletes a volume lane together with a Session clip's volume automation envelope in one undo step, checks that deleting again changes nothing, and undoes it. The UI suite checks the live faders, their thumb colours, the override, Re-Enable Automation, the fader's menu offering Delete and Re-Enable Automation and re-enabling its lane (and no menu on the pan without automation), Cmd+Backspace deleting the fader's automation, undo and the Automation Arm button. The automation suite's earlier "locked fader" checks now assert overrides.

## Not implemented

- Recording automation into the new clips Session Record makes. Only clips that already exist (overdubbing, or playing with All Tracks) take Session automation.
- Session automation of sends and of controls inside racks, which clip envelopes do not take yet.
- A hosted plug-in's own editor reports its move after the parameter has changed, so a Session clip without an envelope for it starts at the moved value.
- Utility controls. (Third-party plug-ins' parameters are captured and overridden from their own editors as well as Halftrack's controls; see the [plug-in automation contract](plugin-automation-contract.md).)
- Measured comparison with the reference's recorded point density.
