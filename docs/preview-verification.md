# Preview verification

Evidence snapshot: **2026-10-13**, macOS / Apple Silicon, Release build. The latest local automated result covers revision `a6f7e2e` (including the early-edit dirty-state fix). GUI observations below came from an earlier preview build; later browser changes still need verification. This is not a final release attestation. The full full capability feature sweep remains behind the user's preview approval gate.

## Current result

The shared session acceptance test passed on the local Mac: **1/1 CTest test, 12.66 seconds**. This includes the controlled VST3 fixture, virtual-device recording, worker-export cancellation, explicit tail duration, undo gestures, and dirty-state regression checks. Sources: [acceptance test](../tests/SessionTests.cpp), [test plug-in](../tests/TestGainPlugin.cpp), and the locally generated `build/Testing/Temporary/LastTest.log` (2026-10-13 13:31 EDT). Generated evidence is in `build/test-artifacts/run-5bd94109156344178a6e4a5402236eb1`. The log and generated files are local artifacts, not committed reports. This supersedes the earlier 9.53-second baseline pass.

| Evidence | Result | Limit |
| --- | --- | --- |
| Native macOS build | Latest acceptance executable passed; earlier GUI build imported four generated stems, displayed waveforms, fitted the timeline, and played/stopped with advancing transport and active track/master meters | These observations do not verify all UI actions or acoustic output. A later launch stalled during synchronous Music-folder scanning; repair and retest are in progress. |
| Windows | CI is running against the shared source | No Windows pass is recorded yet. Native UI, physical drivers, and cross-platform state parity remain pending. |
| Physical audio | Earlier GUI reported CoreAudio built-in speaker output at 96 kHz / 512 samples, with no input | Device configuration and active meters were observed. No microphone, acoustic recording, loopback, measured latency, hot-plug, or dropout test was performed. |
| Export refinements | API tests verify worker cancellation, preserved destination bytes, and a selected two-second post-source render interval | GUI export/save and the rate/depth choice matrix remain pending. Dry-source silence is not a reverb-tail comparison. |
| User approval | Pending | Do not start the full implementation sweep until the owner explicitly approves the runnable preview. |

## What the automated test established

All fixtures are generated locally; no reference recordings or vendor samples are used.

- **Import and media ownership:** a one-second stereo WAV is imported, and its owned copy exists separately from the source. A missing import returns an error.
- **Mixing and undo:** rendered samples verify unity routing, approximately −6.0206 dB gain, mute, hard-left pan, solo exclusion, and two-track summing. Single API gestures restore the complete gain, pan, clip-start, or loop-range change with one undo. The volume gesture spans a 450 ms message-loop wait, crossing the undo timer interval, and its redo restores the complete change. These are API gesture tests, not simulated mouse drags.
- **WAV output and extra time:** baseline exports decode as stereo, 48 kHz, exactly 48,000 frames, with expected signal content. A one-second source plus an explicit two-second tail produces exactly 144,000 frames at 48 kHz. Its final second has magnitude below 0.0001 because the source has no effects. This checks render duration and post-source silence, not actual delay/reverb decay. The reader is a fresh JUCE decoder; a separate application/decoder implementation has not been used.
- **Responsive cancellation:** a posted message calls `cancelExport()` while the worker renders a 60-second-tail request. The test confirms the message was dispatched, the result is `Export cancelled.`, and an existing destination retains its exact original bytes. Cancel-button interaction and responsiveness under demanding third-party plug-ins remain untested.
- **Native projects:** save clears the dirty flag; the project and its Assets directory survive relocation after the original source and old project folder are deleted. Track name, tempo, loop settings, resolved media location, and rendered audio are checked. An unsupported project version fails without replacing the current session.
- **Controlled VST3 hosting:** the generated gain plug-in is discovered and instantiated, its parameter is set to 0.25, and its processing, saved/restored parameter state, bypass, and removal are verified. This uses the host API, not editor gestures. No third-party plug-in or Audio Unit compatibility is established.
- **Recording through the engine:** an injected two-channel, 48 kHz / 256-frame device supplies generated sine waves to the actual audio callback and recording graph. Input assignment, arming, monitor Off/In, recording state, stop-to-clip creation, saved WAV existence and signal magnitude, export, and project save pass. The captured duration is checked between 0.8 and 1.3 seconds after 200 callbacks. The device never enumerates or opens physical hardware.
- **Guards and dirty-state initialization:** recording without a device and arming without an assigned input fail. A new session remains clean after 650 ms of message-loop activity. A separate session with the virtual device begins clean, then an immediate gain edit remains dirty after 300 ms; deferred engine initialization no longer clears the first user edit.

The PCM comparisons require maximum absolute sample error below **0.0001**. This run reported at most **0.00000012** in the compared regions. They exclude the first **4096 frames** (85.33 ms) and last **1024 frames** (21.33 ms) of each one-second fixture to avoid startup/dezippering ramps. These are interior-signal checks, not proof of transient preservation, complete-file identity, or effect-tail fidelity.

The synthetic callback is driven by the test with message-loop pumping. It establishes functional signal flow and capture; it does not simulate hardware clock drift, concurrent real-time scheduling, buffer deadline pressure, driver latency, or system permission behavior. The test plug-in has no latency or tail, so delay compensation and effect tails need different fixtures.

## Review the native preview

Follow [the build and launch instructions](building.md). Prefer the CTest command, which supplies the controlled VST3 path:

```sh
ctest --test-dir build -C Release --output-on-failure
```

Direct `--self-test` execution skips the VST3 case unless `HALFTRACK_TEST_PLUGIN` points to the generated test bundle. Check the output for `SKIP`; a passing suite with that case skipped is weaker evidence.

The main task's GUI audit on an earlier preview build observed four generated stems imported, waveforms displayed, Fit working, and Play/Stop controlling an advancing transport with active track and master meters. Audio Setup reported CoreAudio built-in speaker output at 96 kHz with a 512-sample buffer and no input. The unsaved-close dialog's Discard path closed successfully. No microphone was opened and no acoustic or loopback signal was measured.

A subsequent launch exposed a synchronous browser scan stall while reading the Music folder. The browser repair is in progress; successful launch of the revised build must be rechecked. GUI export/save remains pending, along with clip/loop drag and mixer gestures, project save/reopen, and plug-in editor lifetime. The export UI currently offers 44.1/48/88.2/96/192 kHz, 16/24/32-bit, and 0/1/2/5/10/30/60-second extra-time choices, but the full choice matrix has not been validated.

Record concrete results and platform before advancing the corresponding [PRE rows](feature-parity.md#preview-acceptance-gate). Physical capture and output must be measured with intentionally selected hardware; virtual capture and active meters do not establish that behavior.

## Current scope and gaps

The preview provides a native audio Arrangement foundation, track mixing, input/monitor controls, plug-in hosting, Halftrack project persistence, and WAV export. GUI behavior is still being checked. A given input is assigned to one track in this preview; assigning it elsewhere moves its previous assignment. Projects use `.halftrack` plus a sibling Assets directory; move them together.

The broad DAW target remains unimplemented: Session clip/scene launching, MIDI tracks and note editing, MPE/tuning, warp and reference time-stretch behavior, audio slicing/comping, automation, racks and advanced routing, native Suite instruments/effects, the extensible device environment, controller integrations, recovery, and the full browser/library workflow. Existing `.als` projects and unchanged Max patches are outside the agreed compatibility target. VST2 and CLAP are not enabled; macOS Audio Unit hosting is configured but unverified. Windows and cross-platform plug-in state parity remain pending.

No tests here compare Halftrack against proprietary DSP, certify every audio file/plug-in format, establish optimization or latency targets, or demonstrate long-session/crash resilience. This is a basic preview evidence record, not product parity certification.
