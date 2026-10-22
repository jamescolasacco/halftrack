# Evidence map

Each capability with partial implementation (a PARTIAL row of the [capability ledger](feature-parity.md)) and the evidence that checks it. Suites are CTest names without "-acceptance": "session" is the Session suite and "python" the tooling tests. Manual entries say what was checked by hand. `scripts/evidence_report.py --check` keeps this table and the ledger in step. In CI, `--junit` adds a run's results: commit, environment, command and each suite's expected and actual result.

| Row | Suites | Manual |
| --- | --- | --- |
| AUD-001 | audio-engine, device-loss, device-settings | MOTU M4 hardware check (--hardware-check, macOS arm64) |
| AUD-005 | input-config, ui |  |
| AUD-006 | audio-engine, device-settings |  |
| AUD-007 | device-loss | MOTU M4 unplug and replug by hand |
| AUD-008 | input-permission |  |
| AUD-009 | recording-latency, ui |  |
| AUD-010 | midi-input, midi-monitor, session |  |
| AUD-011 | audio-engine, ui |  |
| AUD-012 | device-settings |  |
| REC-001 | session, tempo |  |
| REC-002 | transport, ui |  |
| REC-003 | session, tempo, tempo-map, time-signature |  |
| REC-004 | punch, ui |  |
| REC-005 | metronome |  |
| REC-006 | audio-from, multitrack-recording |  |
| REC-007 | recording-format, ui |  |
| REC-008 | auto-arm, computer-keyboard, ui |  |
| REC-009 | capture-midi, ui |  |
| REC-010 | take-lanes, ui |  |
| REC-011 | linked-tracks, ui |  |
| REC-012 | external-sync, midi-ports, ui |  |
| ARR-001 | ui |  |
| ARR-002 | groups, track-duplicate, track-order, ui |  |
| ARR-003 | audio-clips, clip-loops, ui |  |
| ARR-004 | range-edit, ui |  |
| ARR-005 | midi-files, range-clipboard, ui |  |
| ARR-006 | audio-clips, range-edit |  |
| ARR-007 | time-edit |  |
| ARR-008 | crossfade |  |
| ARR-009 | locators |  |
| ARR-010 | freeze |  |
| ARR-011 | launcher, session-record |  |
| ARR-012 | audio-clips, clip-activation, clip-loops |  |
| SES-001 | clip-loops, launcher, scene-capture, slot-clips, ui |  |
| SES-002 | launcher |  |
| SES-003 | launch-modes |  |
| SES-004 | follow-actions |  |
| SES-005 | scene-tempo, ui |  |
| SES-006 | midi-input, midi-monitor, session, session-record, ui |  |
| SES-007 | launcher |  |
| SES-008 | midi-controllers, ui |  |
| WRP-001 | audio-clips |  |
| WRP-002 | preferences, tempo-analysis, ui, warp | Reference Core Library loops named with their tempo (--tempo-evaluate, macOS arm64) |
| WRP-003 | ui, warp |  |
| WRP-004 | ui, warp-modes |  |
| WRP-005 | ui, warp-modes |  |
| WRP-006 | repitch |  |
| WRP-007 | ui, warp-modes |  |
| WRP-008 | warp |  |
| WRP-009 | grooves, ui |  |
| WRP-010 | slicing, ui |  |
| WRP-011 | shared-media |  |
| MID-001 | ui |  |
| MID-002 | note-tools, ui |  |
| MID-003 | note-chance, ui |  |
| MID-004 | ui |  |
| MID-005 | midi-controllers |  |
| MID-006 | session, stuck-notes |  |
| MID-007 | midi-tools |  |
| MID-008 | midi-tools |  |
| MID-009 | mpe, ui |  |
| MID-010 | mpe, ui |  |
| MID-011 | midi-tools, ui |  |
| MID-012 | midi-files |  |
| MID-013 | key-map, midi-mapping, midi-ports, ui |  |
| MIX-001 | master-volume, session |  |
| MIX-002 | preferences, ui |  |
| MIX-003 | sends |  |
| MIX-004 | audio-from, routing, ui |  |
| MIX-005 | crossfade |  |
| MIX-006 | audio-from, ui |  |
| MIX-007 | latency |  |
| MIX-008 | track-delay |  |
| MIX-009 | racks |  |
| MIX-010 | racks |  |
| MIX-011 | racks |  |
| MIX-012 | racks |  |
| MIX-013 | ui |  |
| MIX-014 | automation-capture, ui |  |
| MIX-015 | clip-envelopes, ui |  |
| MIX-016 | tempo-map |  |
| MIX-017 | parameter-identity, ui |  |
| PLG-001 | plugin-binary, plugin-timing, session, track-duplicate, ui |  |
| PLG-003 | au |  |
| PLG-005 | plugin-binary, plugin-timing, session, track-duplicate, ui |  |
| PLG-006 | plugin-scan |  |
| PLG-007 | plugin-scan |  |
| PLG-008 | plugin-binary, plugin-timing, session, track-duplicate, ui |  |
| PLG-009 | sidechain, ui |  |
| PLG-010 | plugin-binary, plugin-timing, session, track-duplicate, ui |  |
| PLG-011 | parameter-identity, plugin-automation, ui |  |
| PLG-012 | parameter-identity, session |  |
| PLG-013 | plugin-editor |  |
| PLG-014 | latency |  |
| PLG-015 | plugin-presets, presets, ui |  |
| PLG-016 | plugin-editor |  |
| PRJ-001 | fuzz, session |  |
| PRJ-002 | session |  |
| PRJ-003 | session, ui |  |
| PRJ-004 | recovery |  |
| PRJ-005 | missing-media, shared-media |  |
| PRJ-006 | audio-export, wav-decoder |  |
| PRJ-007 | browser-index, ui |  |
| PRJ-008 | collections, ui |  |
| PRJ-009 | preview, ui |  |
| PRJ-010 | similarity, ui | Core Library drum one-shots evaluated with --similarity-evaluate |
| PRJ-011 | presets |  |
| PRJ-012 | audio-export |  |
| PRJ-013 | stems |  |
| PRJ-014 | render-agreement |  |
| PRJ-015 | ui, video |  |
| PRJ-016 | packs, ui |  |
| UX-001 | auto-arm, computer-keyboard, ui |  |
| UX-002 | ui |  |
| UX-003 | ui |  |
| UX-004 | ui |  |
| UX-005 | themes, ui |  |
| UX-006 | key-map, ui |  |
| UX-008 | mackie, ui |  |
| UX-009 | ui |  |
| UX-010 | preferences, ui |  |
| UX-011 | python | scripts/verify_package.py installs, identifies and removes each CI package |
| QA-001 | session | tests/realtime_audit.py source audit in CI |
| QA-002 | fuzz, graph-edits, large-session, soak, stuck-notes |  |
| QA-003 | python, session |  |
| QA-004 | wav-decoder |  |
| QA-005 | separation |  |
| QA-006 | fuzz, graph-edits, large-session, soak, stuck-notes |  |
| QA-007 | fuzz, graph-edits, large-session, soak, stuck-notes |  |
| QA-008 | fuzz, graph-edits, large-session, soak, stuck-notes |  |
| QA-009 | fuzz, graph-edits, large-session, soak, stuck-notes |  |
| QA-010 |  | CI on macOS 15, Windows 2022 and Ubuntu 24.04 runs every suite |
| QA-011 | device-loss | MOTU M4 hardware check (--hardware-check) |
| QA-012 | python | docs/dependencies.md hash index regenerated and checked |
| QA-013 | python |  |
| QA-014 | ui |  |
| AFX-002 | native-effects |  |
| AFX-003 | native-effects |  |
| AFX-005 | spectrum, tuner |  |
| AFX-007 | spectrum, tuner |  |
| AFX-008 | native-effects |  |
| AFX-009 | spectrum, tuner |  |
| AFX-011 | spectrum, tuner |  |
| AFX-013 | spectrum, tuner |  |
| AFX-015 | spectrum, tuner |  |
| AFX-016 | native-effects |  |
| AFX-017 | spectrum, tuner |  |
| AFX-019 | spectrum, tuner |  |
| AFX-020 | native-effects |  |
| AFX-021 | native-effects |  |
| AFX-024 | native-effects |  |
| AFX-027 | native-effects |  |
| AFX-028 | spectrum, tuner |  |
| AFX-029 | native-effects |  |
| AFX-030 | native-effects |  |
| AFX-031 | spectrum, tuner |  |
| AFX-032 | spectrum, tuner |  |
| AFX-034 | native-effects |  |
| AFX-035 | native-effects |  |
| AFX-038 | spectrum, tuner |  |
| AFX-039 | spectrum, tuner |  |
| AFX-040 | utility |  |
| AFX-041 | spectrum, tuner |  |
| MFX-001 | midi-effects |  |
| MFX-002 | midi-effects, midi-monitor-device |  |
| MFX-003 | midi-effects |  |
| MFX-004 | midi-effects |  |
| MFX-005 | midi-effects |  |
| MFX-006 | midi-effects |  |
| MFX-007 | midi-effects |  |
| MFX-008 | midi-effects |  |
| INS-001 | instruments |  |
| INS-003 | instruments |  |
| INS-009 | instruments |  |
| INS-011 | sampler |  |
| INS-013 | instruments |  |
| RCK-001 | racks, ui |  |
| RCK-002 | racks, ui |  |
| RCK-003 | racks, ui |  |
| RCK-004 | racks, ui |  |
| MXL-006 | instruments |  |
| MXL-010 | spectrum, tuner |  |
| MXL-017 | midi-effects, midi-monitor, midi-monitor-device |  |
| MXL-019 | midi-effects, midi-monitor-device |  |
| MXL-021 | midi-tools, ui |  |
| MXL-022 | midi-tools, ui |  |
