# Open questions and working defaults

- The supplied phase roadmap was superseded by the instruction to ignore phases. The preview has been delivered, and the latest instruction authorizes further research and implementation. No preview gate currently blocks work.
- The newer scope excludes programmable patching, branded hardware and cloud work. Default: remove those targets from the active product scope.
- A strict callback ban cannot be asserted for arbitrary external plug-ins or all upstream internals by inspecting our code alone. Default: enforce it for owned DSP, document engine thread boundaries, add executable offline checks, and keep whole-engine/dependency guarantees explicitly unverified.
- The previous no-inspection rule was explicitly reversed by the owner. Default: permit local read-only inspection and decompilation with evidence and coverage records; preserve original implementation/third-party artifact boundaries.
- New projects currently start with two empty audio tracks. Default: preserve that behavior and expose a clear MIDI-track command; do not silently put instruments into empty audio projects.
- Exact cross-format plug-in state translation is not specified. Default: persist native identity/state and show a missing-format error; do not guess replacements.
- The phrase one CMake command means a reproducible configure/build entry point. Default: provide CMake workflow presets, retaining standard configure/build commands for contributors.

## Reverse-engineering direction (2026-10-13)

The owner now authorizes local binary analysis/decompilation and prioritizes the closest possible match over the earlier clean-room process. Original Halftrack code remains under the existing AGPL license; this does not relicense extracted third-party material. Default: keep binary/decompiler artifacts local and ignored, publish original tooling and factual summaries only. The absolute-zero-difference target needs explicit coverage of inputs, versions, hardware and nondeterministic behavior; no finite test run establishes every possible state. Hardware/account/cloud access is not expanded by this instruction.

## Sends and returns: implementation defaults

Use native auxiliary buses with return tracks feeding the main mix. Start with no returns; creating one selects Post, unity return gain, and every source send Off. Pre sends follow source effects but precede source pan, gain and explicit mute; Post follows those controls. Return-to-return sends, feedback and separate hardware output remain outside this first routing increment.

The public mixer description does not fully enumerate combined source/return solos. Default: keep a feed when nothing is soloed, its source is soloed, or its destination return is soloed, then apply explicit source mute to Post feeds. Keep required returns audible for a soloed source. This combined rule must pass Halftrack's audio tests and remains unverified against the reference application. Return identity is a stable track ID, never its name or position. See the [primary workflow description](https://www.ableton.com/en/live-manual/12/mixing/).

## Scene command concurrency: required semantics

The existing per-track command submission is not atomic across a concurrent processing callback. The next correction must accept an entire scene at one common pre-graph boundary; commands published after that boundary's snapshot take effect on the next outer callback, including Return and cancellation. Preserve native late-boundary catch-up as a whole-scene behavior. Do not add a shared flag around the existing independently locked handle queues and call it atomic.

The local source audit identified the playback context's existing continuity callback as a candidate integration seam. A concrete implementation still needs bounded whole-command publication, an ordered cancellation path that remains available under queue saturation, target lifetimes through deletion/undo, and delayed ownership publication that cannot cancel a newer scene. These are implementation requirements, not a runtime fix or a completed concurrency proof. The existing serialized-command launcher tests remain valid within their documented scope.

A further native-source audit found that the node player can acquire a different prepared graph for each tempo/loop subrange within one outer callback. Freezing only the command queue boundary is therefore insufficient to prove stable target membership. Retained pointers prevent premature destruction but do not alone prevent deleted targets from relaunching after undo. A safe fix needs coordinated graph-generation retirement or an explicitly narrower structural-edit policy. Timed Stop Track is a silent takeover and cannot be indiscriminately coalesced into global Return/cancellation. No concurrency implementation is claimed.

## Multiple-note editing defaults

The current editor draws on ordinary empty-space clicks. Keep that behavior while adding Shift-click toggles, Shift-drag additive marquee selection, Cmd/Ctrl+A and Escape. A Shift-click on empty space clears selection; a drag instead combines its rectangle with the selection present at mouse-down. Group motion uses a common bounded time/pitch delta, preserving spacing at the clip and MIDI boundaries; group resizing uses a common length delta. Velocity explicitly sets every selected note to the displayed value. Selection is local editor state, pruned against stable note IDs, and is not itself an undoable musical edit. A separate draw/select mode, relative-velocity editing, time-span selection and multi-clip editing remain separate work. These defaults are not claims of measured reference equivalence.

## Note clipboard defaults

Copy/Cut/Paste/Duplicate act on the selected notes only. Keep a typed in-application clipboard across clip changes, preserving relative timing, pitch, length and velocity. Use a visible insertion cursor set from the ruler and moved by arrows when the selection is empty; a new clip starts at beat zero. A successful paste advances the cursor to the pasted group's end. Duplicate places the copied group after its selected extent, using the current grid. Reject a whole group that does not fit rather than truncating notes, overwriting unrelated notes or extending the clip silently. Insertions receive fresh stable identities and form one undo step. These are explicit working defaults, not measured reference equivalence. System clipboard interchange, time-span editing and automatic clip extension remain separate work.


## Built-in instrument pitch-wheel default

The instrument has no specified bend-range control. Use a fixed range of two semitones down/up, center 8192, minimum 0 and maximum 16383. Normalize each side separately so both endpoints reach exactly two semitones. Bend is transient per-channel controller state, applies to existing and subsequent voices without restarting their envelopes or phases, and retains original note identity for note-off. CC121 centers that channel; CC120/123 preserve the wheel, while full host reset clears it. Ignore invalid channels/values. Range editing, RPNs, MPE, aftertouch, controller lanes and external MIDI-device UI remain separate work. These are explicit Halftrack defaults, not measured reference-instrument equivalence.


## Oscillator expansion defaults

Retain saved waveform values 0 (saw) and 1 (square at default width), then add 2 (sine) and 3 (white noise). Pulse width is a normalized 0–1 patch parameter, default 1; map it to 0–50% duty so 100% remains square, consistent with the public workflow description. Use an original zero-mean band-limited pulse; width zero has zero oscillator output. Preserve phase/envelope/filter state when changing controls, and use deterministic per-voice noise for repeatable offline renders. Noise sequence, exact width mapping between endpoints, DC treatment and filter/output response are Halftrack defaults pending controlled reference measurements. Existing projects lacking the field keep their previous square sound. This increment does not claim reconstruction of an entire reference instrument or its dual-oscillator/filter/modulation architecture.

## Channel utility behavior (2026-10-14)

The public controls are specified, but paired reference renders do not yet establish smoothing time, balance law, low-frequency filter topology, DC cutoff, or exact processing order. Default implementation: 5 ms linear ramps for gain/balance/width/mid-side weights; constant-power balance normalized to unity at center; a third-order Butterworth high-pass on side and low-pass audition; a 5 Hz second-order DC blocker with Q=0.5 and unity Nyquist gain. Input polarity precedes channel selection and unweighted mid/side filter history. Bass shaping then receives width/mid-side/mono weights, output gain/balance, DC blocking and final mute. Audition only applies while bass mono is enabled. The balance law and filter orders follow a later binary-assisted trace in the device contract. The numerical realization and remaining defaults are explicit Halftrack choices, not sample-identical recovery claims. Preserve all disabled controls' values. Stable device IDs identify settings and undo operations.

## Native effects and note transformations

Public descriptions do not determine exact DSP equations or every editing edge case. The current working defaults and missing controls are explicit in the [effects contract](research/native-effects-contract.md) and [transformation interface](architecture.md#midi-transformations). Delay sync follows the single project tempo through bounded control packets; sample-accurate tempo maps remain future work. Note transforms reject a whole operation that cannot fit the clip. Exact-output comparisons are now requested by the owner, superseding the old OUT-003 exclusion; no zero-difference result is inferred from these defaults or independent mathematical tests.

## Audio clip editing defaults

Expose clip gain (-100 to +24 dB), independent fade durations and four existing engine curves: linear, convex sine, concave sine and S-curve. Fades must fit without overlap inside a clip; setting them disables automatic crossfade on that clip. Audio media stays immutable. Trimming is explicit start/length/source-offset editing for unwarped, unlooped, normal-speed arrangement clips; reject source overrun and fades that no longer fit. Splits use the native engine operation, require at least 2 ms on each side and reject cuts through an existing fade to preserve its envelope. Duplicates retain processing settings, share owned immutable media, receive a fresh identity and start immediately after the original. Slot clips receive gain controls only; structural editing cannot silently put a second clip into a slot. These defaults are implemented and tested before being marked working.

Native source inspection confirms the launcher graph omits the Arrangement fade node. Halftrack therefore disables and rejects manual slot fade edits; Session gain has an actual playback test. Arrangement fades and the separate reference Session edge-fade toggle are distinct workflows; that toggle remains unimplemented.

Direct fade-duration drags use continuous seconds without grid snapping. They clamp against the opposite fade instead of modifying it. At less than 20 displayed pixels, use the numeric editor; no ambiguous overlapping hit targets are introduced. This is the simplest explicit interaction default pending user feedback.

### Arrangement audio clipboard and gestures

Default: a typed, application-local clipboard holds one unwarped, unlooped, ungrouped normal-speed Arrangement audio clip. It survives project switches within this running Session; it does not overwrite the system text clipboard. Copy retains an independent media file before Cut can remove the clip; Paste adds a clip on the selected audio track at the playhead and never overwrites overlapping material. Each paste creates new identities. Native state preserves clip properties. Range edges snap to the timeline grid, Alt bypasses snapping, and Shift+Alt-drag in the clip body slips the source while holding the timeline range fixed. Fade lengths must still fit the retained clip. Multi-clip/time-range clipboard and warped range gestures remain separate work.

### Arrangement locators

Default: named point locators use the engine's marker track and absolute timeline seconds; changing project tempo does not move them. Each native marker has a 1 ms storage extent, but appears as a point and never extends an audio export. Add at playhead, click to seek, edit name/position, drag with grid snapping (Alt bypasses), and navigate previous/next without wrap. Each mutation uses the engine undo manager. Locator edits may be made during playback/recording, but not during export. Region markers, beat-locked locators and locator-triggered scene launches remain separate work.

### Audio reversal

Default: reverse the currently exposed source region of a single-source, normal-speed, unlooped Arrangement clip while retaining timeline placement, duration, gain and fades. One undo restores direction and source offset. The engine reverses its cached playback file; source media stays unchanged and projects retain the original source plus native direction metadata. Warped, pitched, looping, Session, grouped, frozen, comped and clip-effect/ARA sources are not accepted by this initial control. Reverse can be toggled back before trimming, slipping, splitting or copying a reversed clip.

### Standard MIDI files

Default: import PPQ-based type 0/1 files at the playhead in beats, into separate new MIDI tracks for each file-track/channel pair. Preserve notes, velocities, release velocities, CC, program changes, pitch bend, poly/channel pressure and SysEx; attach channel-independent SysEx once per source track. Keep the current project tempo, and its meter unless the file's time signatures are imported as markers (asked when the file has them), and expose imported notes through the existing synth/piano roll. Reject SMPTE/type 2, unmatched or overlapping same-pitch notes, and unsupported system events rather than silently repairing them. Limit imports to 16 MiB, 100,000 events and 64 resulting tracks. Export selected clip source events as a type 0 file at 9600 ticks per quarter, independent of Arrangement placement; clip loops, offset/groove rendering and project tempo maps are not baked into exported events. Metadata other than the track name, exported tempo and time signatures is outside this initial interchange path.

Export retains source events rather than printing clip playback transformations. Muted notes are omitted; shared timestamps order SysEx/controllers before notes. Notes shorter than one export tick become one tick. A source extent above 268,435,455 ticks at 9600 PPQ is rejected before writing; adaptive export resolution and long-gap event splitting remain future work.

### Arrangement MIDI clip clipboard

Default: copy one unlooped, ungrouped, single-take Arrangement MIDI clip at normal speed and zero source offset. Keep all native note/controller/SysEx settings and beat duration when pasting into a MIDI track, including after a project switch to a different tempo. Give each pasted/duplicated clip and its notes fresh identities. Cut/Paste/Duplicate each undo once; Copy does not dirty the project. The most recent successful audio or MIDI clip copy replaces the other clip type. Piano-roll note and system text clipboards remain independent. Looped/offset/comped/Session/multi-clip copies remain separate work.

- Moving a MIDI clip to another track uses that track's existing instrument and processing. It does not move or duplicate the source track's devices. The current project has one constant tempo; clip movement retains duration and source offset. Tempo-map-aware relocation remains separate future work.

- Track ordering applies to the single shared top-level list, including return tracks. Returns can appear before their sources because routing uses bus identity. Context menus offer up/down in Arrangement and left/right in Session; the Track menu offers earlier/later in either view. Arrangement headers drag tracks and groups into and out of groups, and returns among the returns ([moving tracks](research/groups-contract.md#moving-tracks-2026-10-23)). Session column-header drags remain future work.

- **Empty groups.** A static reading found nothing in the reference that deletes an emptied group or refuses the move or deletion (medium), so it seems to keep the empty group. Only its Group Tracks rule refuses to empty another group. Default: Halftrack keeps an emptied group, showing its header lane and Session column, until a measurement says otherwise.

- Whole-track Duplicate copies unfrozen audio/MIDI tracks and groups as one block after the last selected item, as the reference's Duplicate places them; copies keep the source's name, or its automatic one. It keeps mixer mute/solo, device state and sends to existing returns, shares immutable audio media, and starts without recording inputs or active Session launches. The reference duplicates returns (adding returns) and frozen tracks too; Halftrack refuses them until their bus and freeze copies are implemented. Whether the reference's colour picker recolours copies was not determined, so copies keep the source's colour.

- Global project tempo now consistently supports 20–999 BPM, matching the facade's intended range. The text editor rejects out-of-range/incomplete input; finite programmatic requests clamp and nonfinite requests do nothing. Small edits must retain musical clip positions rather than use the native 1 ms remapping dead zone. Tempo maps, tap/nudge and signature changes remain separate work.

### Controller recording defaults

Default: one named native audio or MIDI input per track, with exclusive assignment. A newly discovered physical MIDI input opens only after explicit selection; choosing All MIDI Ins explicitly opens the currently available inputs. Saved enablement remains an application preference. Record into Arrangement, unquantized, with native merge overdub into overlapping clips. Keep the current non-MPE single clip-channel behavior and document it. Finish a take on Stop or seek; defer arm, monitoring, input reassignment and history operations until recording stops. Empty MIDI takes add no clip. MIDI channel filtering, duplicate-name disambiguation, record quantization, MPE, replace/take-lane selection and Session capture need separate controls and tests.


### Metronome and count-in defaults

Default: click Off, first-beat accent On, gain 60%, recording-only Off, count-in None. Expose zero/one/two 4/4 bars; the first count-in click is at the count-in's start, as the reference's. Click and count-in use the main output and do not appear in offline exports. A count-in applies when recording begins from stopped; punching Record during playback does not rewind. Click settings belong to the project and undo together. Separate cue outputs, signature changes, subdivisions and custom samples require later controls and tests.


### Recording quantization

Default: Off. The reference's grids: 1/4, 1/8, 1/8T, 1/8 + 1/8T, 1/16, 1/16T, 1/16 + 1/16T and 1/32 snap new recorded note starts to the nearest project beat grid, with exact half-grid ties going forward. A combined grid snaps to the nearer of its straight and triplet points, preferring the straight one when they are as near. Preserve note durations, velocities and controller timestamps. Bound the onset to the captured clip so a note is not moved outside the available source range. Existing overdub notes keep their timing. Apply on Stop within the same take undo transaction; input monitoring remains unquantized. Store the grid per project, reject changes during recording, and use raw native input capture to avoid a second device-specific quantizer. Loop recording and overdubbing into offset/looped clips with recording quantization enabled require separate work; reject those combinations before capture.


### Master output defaults

Default: use the native master-volume stage and fader law, from silence (-100 dB) through +6 dB, initially 0 dB. Expose a single stereo output-level fader beside the meters, including typed dB entry and double-click unity. Apply it to playback and export after return contributions. Master pan, master device insertion and separate cue output are future features.


### Tap tempo

Default: update after the second tap, average at most four recent intervals, ignore taps faster than the supported maximum, and restart after a pause over three seconds. Do not auto-start playback. Each resulting tempo change undoes once; tapping the same tempo and the first anchor do not dirty the document. Normal tempo edits, history and project replacement clear the transient estimate. No claimed match to an undocumented reference tap algorithm.


### Clip activation while playing

Default: deactivate a whole audio/MIDI clip without removing its contents or Session ownership. Send note release through the engine; existing synth/effect tails may decay. Re-enabling allows future Session events without relaunch; full held-note/controller chasing and reference-exact transition timing remain unverified. Frozen tracks must be unfrozen first. Unmodified 0 acts on whole clips in Arrangement/Session only; note activation is separate work.


### MIDI note split policy

Default: split crossing notes at the piano-roll marker, using the current selection or all notes when none are selected. Exact note edges do nothing; any sub-1/960-beat fragment rejects the group. Splits retrigger the note and preserve ordinary properties with fresh right-half identities. Per-note expression children are rejected until their envelopes can be cropped and shifted explicitly.


### Clip names and media

Default: clip names are trimmed, nonblank display labels. Renaming never changes an audio filename or moves source media, even if a native engine preference requests it. Audio and MIDI clips in both views use the same stable-ID command; cancelling or confirming a stale dialog leaves any replacement clip untouched.


Scene-copy default: insert immediately after the source and append ` copy` to its name. Duplicate audio/MIDI clip state with fresh scene/slot/clip/note IDs, share immutable media, and leave the current performance running without launching the copy. Empty rows are valid; the existing 256-scene limit applies. Copying frozen/populated return tracks or additional clip types awaits an explicit supported model.


Scene-order default: Up/Down moves an existing scene and all its track cells by one row in the UI; the facade also accepts any valid final row index. Movement preserves currently playing and queued clip identities and their launch boundary. Recording/export block structural row edits. No scene drag/drop gesture is claimed yet.


Note-activation default: toggle the selected notes only; an empty selection does nothing and a mixed selection activates all selected notes. This preserves the distinction from whole-clip activation. Muted notes retain pitch, timing, velocity, identity and editable appearance; copied notes preserve activation.


Audio export defaults: keep the full stereo Arrangement mix and existing WAV defaults; add lossless AIFF/FLAC with the pinned writers' supported 16/24-bit depths. WAV retains 32-bit. FLAC uses the renderer's default compression setting. Normalization, configurable dither, range/stem export and lossy encoders remain separate features. Never silently down-convert a requested model depth; the UI explicitly selects 24-bit when changing away from 32-bit WAV.


Follow-action default: Off. The first active configuration uses four project beats. Expose one deterministic action per Session clip: Stop, Play Again, Previous, Next, First, Last, or Cycle to Next. Previous/Next stop at the ends; Cycle wraps and all track actions skip empty slots. Off clears the action and timing; choosing an active action again starts with four beats. Weighted/random choices, contiguous-group targeting, loop-linked timing and scene follow actions require their own controls and tests.


Export mix options default to Stereo and normalization Off. Mono means the arithmetic average of the full rendered channels, including both sides of the stereo mix. Peak normalization offers -1/-3/0 dB sample-peak ceilings using the pinned renderer's conservative headroom and 40 dB maximum boost. It does not modify faders. True-peak/LUFS normalization, dither selection and range/stem rendering remain separate features.

Bitcrusher defaults: implement deterministic hard quantization and sample holding before adding filter, jitter or dither modes. Use signed PCM-style clipping/rounding, a shared stereo capture clock and independent channel amplitudes. Fractional depth/interval controls are supported as continuous reductions; no proprietary quantizer algorithm or sound identity is implied.

Noise Gate default: linked peak detection with linear, frame-counted attack/hold/release and separate close threshold. Use exact silence at -100 dB Floor and invert the opening envelope in Duck mode. Sidechains, lookahead and envelope displays require separate behavior/tests.

Peak Limiter default: fixed one-millisecond lookahead rounded up to whole frames, linked stereo sample peaks, immediate attenuation and one-pole release. Expose gain, ceiling and release only; native plugin bypass handles the unprocessed path. True-peak/oversampling, adaptive release and selectable lookahead remain separate work.

Pending device insertion belongs to the project instance and track that opened the menu. Track reordering/selection changes retain that target; successful New/Load invalidates it even if saved track IDs repeat. Save As and failed loads retain the instance. The transient generation token is not a project-schema field.


- Initial automation scope: provide editable arrangement-time track volume and pan curves through the native mixer before adding write/touch/latch recording or third-party parameter automation. Default interpolation follows native fader coordinates for volume and linear pan; disabled lanes retain their points, and empty lanes use manual settings. Beat positions follow tempo edits. The UI has its own resizable editor, with a stable project identity. This is a scoped working feature, not a claim of complete automation parity.


### Selected-range export default

An explicit range exports exactly its chosen seconds, rounded to the nearest output-sample boundaries. Render the arrangement prefix first so held MIDI notes, automation and stateful effects reach the start naturally. Extra tail is unavailable in this initial range mode: select a later endpoint to include the continuing mix. Whole-arrangement export retains its tail options. This avoids silently including later clips as an alleged isolated effect tail. Normalization measures only the exported interval; source project state and history remain unchanged.


### Master-volume envelope defaults

Use the existing arrangement-time volume-lane semantics on the native master output stage. Master is a separate destination with Volume only; no synthetic track ID or undocumented master-pan control. Protect the manual fader while any points exist, even with Read off. Reject obsolete/conflicting master serialization before loading rather than silently allowing native legacy conversion to erase an envelope. Existing modern Halftrack master levels remain supported.

### Clip launch mode defaults

Releasing a Repeat clip stops it on the clip's grid, like Gate; the reference manual describes only the retriggering while held. Gate and Repeat releases are quantized to the clip's own grid rather than stopping immediately. Repeat with no grid (None, or Global set to None) launches once without retriggering. A second Toggle press while the launch is still queued cancels it. Legato follows the clip already playing on the same track; with none playing it starts normally. Scene launches always trigger. Triplet grids and launch velocity are not exposed. These are simplest-default choices pending reference measurement.

### Sampler defaults

Volume 0 dB plays the source at unity (the synthesizers keep quarter-scale headroom); velocity to amp defaults to 35%, voices to 8, release to 50 ms and filter off. Pan uses a balance law with unity at the centre. A missing or unreadable sample rejects the project load, matching missing clip audio, rather than opening with a silent Sampler. Samples are limited to ten minutes and decoded fully into memory. Slicing, warping and crossfaded loops are not implemented. These are simplest-default choices pending reference measurement.

### Time signatures

The project meter is fixed at 4/4. The pinned engine counts beats in the time signature's denominator unit, while Halftrack's model, UI and tests treat a beat as a quarter note. An editable meter therefore needs a beat-unit decision across notes, clips, grids, warping and count-in before it is exposed. The reference application's tempo is assumed to count quarter notes; this is unmeasured.

### Group slot launching

A group slot launches its members' clips in that row on the global grid, and members with an empty slot there stop, as in a scene launch. Per-clip launch modes and grids are not applied through a group slot. Folding hides member columns and their mixer strips in Session view. These are simplest-default choices pending reference measurement.

### Session recording defaults

Takes start on the global launch grid (immediately from a stopped transport) and end on it; per-clip grids do not apply to recording. Pressing another filled slot on a recording track ends the take without playing it and launches that slot. Track and global stops and transport stop keep the take silent. An empty MIDI take still creates a clip of its length. Audio takes follow tempo at the tempo they were recorded at. Recording while the transport loops is not handled specially. These are simplest-default choices pending reference measurement.

### Audio-device changes during takes

Default: any audio-device stop while a take records ends Arrangement and Session takes where the device stopped. It keeps them and stops the transport, as Stop does. This covers an Audio Settings change, a driver-initiated restart and a removed device. Audio Settings stays available during recording; refusing it would not protect against driver restarts. A device change is not deferred until the take ends, because a new sample rate or buffer size cannot continue the open recording. This is the simplest safe choice, not a measured match with the reference application, whose behavior on device changes during recording has not been observed. Stops reported on a non-message thread end the takes asynchronously. An interleaved engine device query on the message thread could still run the rebuild first; this is unverified and not covered by tests. Physical drivers and hot-unplug are not tested.

### Drum Sampler defaults

Pads are one-shot at unity level and velocity scales linearly; length cuts fade over 5 ms and choke groups release over 5 ms. Sixteen pads cover C1 to D#2 with no bank switching. The kit shares 32 voices with oldest-voice stealing. Browser samples load into the selected pad. These are simplest-default choices pending reference measurement.

### Device automation defaults

Device lanes interpolate linearly in each control's knob position (its editor skew), and integer and choice controls step to the nearest value. Automation is applied at audio-block rate, without per-sample smoothing beyond each processor's own. Third-party plug-in parameters are not exposed. These are simplest-default choices pending reference measurement.

### Freeze defaults

Only Arrangement clips are frozen; Session clips keep playing live. Freeze renders stay in the engine's temporary folder, so a reopened project on another machine plays live. Undoing a Flatten returns to the unfrozen track rather than the frozen state. These are simplest-default choices pending reference measurement.

### Stem export defaults

Member tracks of a group are rendered through their group's mixer, and muted tracks export silent stems. Stems use the export window's format, range, tail and normalization for every file, normalizing each file on its own. Master processing applies to every stem through the shared render path. These are simplest-default choices pending reference measurement.

### Crossfader defaults

The crossfader uses the dipless curve (a side stays at 0 dB until the fader crosses the centre toward the other side) and is not selectable. Groups have no crossfade side. Crossfader moves are ordinary undoable edits and are not automatable yet. These are simplest-default choices pending reference measurement.

### Time edit defaults

Insert Silence and Delete Time use the Arrangement time selection, or the loop range when nothing is selected. MIDI clips crossing an Insert Silence or Delete Time edit are extended or shortened in place, not split. A note crossing the inserted point keeps its length, and Delete Time gives automation no boundary points at the cut, so a ramp across deleted time joins its neighbours; Insert Silence holds a crossing lane's value across the gap. Cut, Paste and Duplicate Time copy every track but the returns: locators inside copied time are not copied, and a return's automation moves but is not copied ([contract](research/time-commands-contract.md)). Tempo is constant, so tempo events are not moved. These are simplest-default choices pending reference measurement.

### Follow action chance defaults

The follow-action time is one value shared by both actions, and a No action choice is final for that launch (it is not evaluated again on later loops). Random choice uses the engine's unseeded generator, so tests check deterministic chances exactly and an even chance statistically. These are simplest-default choices pending reference measurement.


### Track Delay defaults

Track Delay runs from −1000 to +1000 ms in milliseconds only. It applies after the track's devices and before its sends, so pre- and post-fader sends both carry it, and MIDI is not delayed separately because native instruments come before it. A positively delayed track can replay up to the delay time of pre-seek audio after a jump. These are simplest-default choices pending reference measurement.

### MIDI controller lane defaults

Controller lanes are step-wise (a value holds until the next point) rather than ramped envelopes, and drawing writes one point per grid step. Channel mode messages (CC 120–127) are not editable. Deleting time keeps the last value set inside the deleted range at the cut. These are simplest-default choices pending reference measurement.

### Input changes during Session takes

While a Session slot take records, input assignment is refused on every track, as during Arrangement recording. Enabling a device would discard the take, and replacing or disconnecting the recording track's input would leave the take without the input that stops it. A narrower rule could still allow assigning already-enabled inputs to other tracks; the reference application's behavior is unmeasured. Arming and monitoring stay available during a take. A MIDI device that appears during a take is discovered when the take ends. Audio-device changes during any recording are covered under [audio-device changes during takes](#audio-device-changes-during-takes). Other edits during a take are covered under [edits during Session takes](#edits-during-session-takes).

### Edits during Session takes

Some edits a take cannot survive are refused while any Session take records, as during Arrangement recording: undo and redo, audio and stem export, freeze and flatten. Rendering frees the playback context, and with it every input recording. Undo can reach the recording track's arm or input, and flattening removes a MIDI track's input. Edits that would delete or replace the recording slot are refused for that slot only: removing the recording track, deleting its scene, and adding or importing a clip into it. Other track, scene and slot edits, MIDI import and saving keep the take and stay available. New Project ends the take in the replaced project, as Open does.

The reference application may handle these differently, and that is unmeasured. For example, it may let a delete or an undo discard the take. Halftrack refuses instead because the engine would otherwise lose the take or leave it without its slot or input. Refusing every edit during a take would be simpler, but it would also block safe edits.

Seeking during a take, including jumping to a locator, ends the take where the play head was and stops the transport, as it does during Arrangement recording. Without that, the take kept recording across the jump. Its length grew by about the jumped time, and a key played after the jump was recorded about five beats too long. Ending only the take and keeping the transport playing would be the other simple choice. Disarming the recording track is refused ("End this track's Session recording before disarming it."). The engine kept the take after a disarm, but the track then reported itself unarmed while its slot recorded. The reference application may instead end the take when its track is disarmed, and that is unmeasured. These keep the take and stay available: monitoring and instrument changes on the recording track, Arrangement clip and time edits, track, device and mixer automation, follow actions, count-in and recording quantization. Count-in and recording quantization do not apply to Session takes. Loop and tempo changes during a take are not yet audited.

### Arrangement selection defaults

Duplicate Selection copies the selected tracks' automation with one-millisecond edge points, and Delete Selection clears it inside the selection while keeping the edge values; Split and Consolidate leave automation alone. Consolidated audio is rendered at the audio device's rate, without the track's devices, and plays unwarped. Duplicate replaces the material in the time after the selection, and the selection moves onto the copy. Notes crossing a MIDI split become two notes. These are simplest-default choices pending reference measurement.

### Rack defaults

An empty effect rack passes its input and an empty Instrument or Drum Rack is silent. Zone fades are linear: key and velocity fades scale the note's velocity; selector fades scale an Instrument or Audio Effect Rack chain's output and a MIDI Effect Rack chain's velocity. The chain selector moves in whole steps from 0 to 127. Gains ramp over 5 ms. Chain mute and solo act on a chain's output, except in MIDI Effect Racks, where they stop new notes. A choke fades the choked pad's instrument voices over 5 ms, and audio effects after the instrument keep ringing. Chain pan uses the track mixer's law. Macro mappings interpolate in knob position, so a volume mapping follows the volume law. A macro's first mapping takes the control's current position, and removing a mapping leaves the control where the macro put it. Macro automation applies at block rate. Grouping copies built-in devices into a one-chain rack; third-party plug-ins cannot go in racks yet. Only one-chain Instrument, Audio Effect and MIDI Effect Racks can be ungrouped. Chain Selector MIDI controller filtering, macro variations, randomize, hot-swap, chain colors, Drum Rack pad routing to the project's returns, and extracting chains are not implemented. These are simplest-default choices pending reference measurement.

### Time signature defaults

Beats are quarter notes at every meter, so the tempo counts quarter notes and 6/8 has three beats per bar. Time signature markers start bars where they are, and the bar before one inside it is fragmentary, as the reference's manual describes. The metronome clicks the meter's note value and accents each bar. Importing a MIDI file asks whether to bring its time signatures in as markers; its tempo stays out. A marker within 1e-5 of a bar's length after a bar line leaves no fragmentary bar, as the engine rounds such slivers away; the reference's handling of them is unmeasured. These are simplest-default choices pending reference measurement.

### MIDI note tool defaults

The note tools use Halftrack's own algorithms and parameter sets (for example, Strum keeps each note's end, and Connect interpolates passing-note pitches linearly). They apply on demand rather than previewing live. Generators add notes across the whole clip and replace only chosen notes. These are simplest-default choices pending reference measurement.

### Timeline navigation defaults

Follow pages the view (the playhead starts each new page) rather than scrolling continuously, and scrolling by hand while playing switches Follow off until it is turned back on. Z adds 5% margins around the selection. These are simplest-default choices pending reference measurement.

### Clip scale defaults

Scales belong to clips (there is no project-wide current scale), the piano roll highlights rather than folds, and Fit to Scale breaks ties downward. Sixteen common scales are offered. These are simplest-default choices pending reference measurement.

### Crash recovery defaults

There is one recovery copy, written every 30 seconds while there are unsaved changes. A recovered project opens untitled rather than reattaching to its saved file, so the user chooses where to save it. These are simplest-default choices pending reference measurement.

### Device preset defaults

The user library lives in `~/Music/Halftrack/User Library`. Presets store control values only (no samples, automation or rack contents), and loading always adds a new device rather than replacing a selected one. The default set stores media by absolute path. These are simplest-default choices pending reference measurement.

### Tempo automation defaults

Tempo segments ramp linearly (applied by the engine in quarter-beat steps) and there are no curved segments. Locators keep their times when the tempo changes. Insert Silence and Delete Time are refused while the tempo is automated, and MIDI export writes tempo points as steps. These are simplest-default choices pending reference measurement.

### Exclusive solo and arm defaults

Solo and arm are always exclusive, with Cmd/Ctrl-click adding, matching the reference's default preferences; there is no preference to change it yet.

### Groove defaults

These are simplest-default choices pending reference measurement:

- **Placement.** A note is moved by the offset of the groove event on its nearest Base line (a step per line, not a continuous warp). Lines without an event leave the note's timing and velocity alone.
- **Velocity.** Accents are relative to the groove's loudest event.
- **Random.** Random reaches half a Base step at 100%. It is fixed per note (clip, pitch and beat) rather than drawn again on each pass.
- **Alignment.** Grooves align to the clip's own beats, not the song position.
- **Frozen tracks.** Pool edits are allowed while frozen tracks use the groove, and are heard once the track is unfrozen.
- **MIDI export.** MIDI export writes the clip's own, ungrooved notes.

### Channel EQ defaults

The reference does not document these, so they are simplest-default choices pending reference measurement:

- the 80 Hz high-pass and the High cut's low-pass are second-order Butterworth filters;
- the High shelf's corner is 8 kHz;
- the Mid peak's Q is 0.7;
- Output runs from −12 to +12 dB.

### Tuner defaults

These are simplest-default choices pending reference measurement:

- **In tune.** The Tuner counts as in tune within 3 cents.
- **Analysis.** It analyses the mono mix of a stereo input over 4,096 frames.
- **Range.** It detects 30 Hz–4.2 kHz with YIN's 0.1 threshold.
- **Held reading.** It keeps showing its last reading while no audio is processed.

### Automation capture defaults

These are simplest-default choices pending reference measurement:

- **Touch.** A held control records a touch pass and the earlier automation resumes 1/960 beat after release. Moves without a gesture latch until recording stops.
- **Density.** Recorded points are at least 1/64 beat apart.
- **Persistence.** Automation Arm is saved with the project and is not undoable.
- **Read switch.** An override and the automation editor's Read switch are the same state.

### Punch defaults

Halftrack records the whole pass and trims new takes to the brace when recording stops. Recording starts where playback starts, with no pre-roll tied to the punch point. Existing clips are never trimmed. These are simplest-default choices pending reference measurement.

### Spectrum defaults

The manual gives no numbers, so these are simplest-default choices pending reference measurement:

- **Defaults.** Block 4,096, L+R, a 60 ms refresh, Avg 1, a line graph, Max off, a log scale and Auto range.
- **Ranges.** Refresh runs 10–500 ms. Min runs −120 to −1 dB and Max level −100 to +12 dB.
- **Auto range.** The axis spans 96 dB below the loudest bin rounded up to 6 dB, plus 6 dB of headroom.

### Preview defaults

These are simplest-default choices pending reference measurement:

- **Playback.** Preview plays files once at their own tempo, like the reference's Raw mode.
- **Volume.** The preview volume is the cue volume ([cue](#cue)): −70 to +6 dB, −6 dB at first, kept with the project.
- **Project changes.** Opening another project does not stop a preview.

### MIDI mapping defaults

These are simplest-default choices pending reference measurement:

- **Inputs.** Every enabled MIDI input acts as a remote, with no per-port Remote switch.
- **Absolute values.** Controllers are absolute, with no takeover mode.
- **Undo.** A controller's moves are one undo step until it rests for half a second.
- **Learning.** Learning moves nothing.
- **Deleted devices.** A deleted device's mappings stay stored but inert.

### Pedal defaults

The manual does not give these, so they are simplest-default choices pending reference measurement:

- **Curves:** the drive curves and the +6 to +42 dB drive range.
- **EQ:** ±12 dB bands, the resonance slope, and a +6 dB Sub shelf.
- **Output:** −36 to +12 dB, defaulting to −12 dB.

### Beat Repeat defaults

These are simplest-default choices pending reference measurement:

- **Slices.** The first slice is the live input. Gate mode includes it.
- **Pitch.** Pitch Decay lowers each further repeat by up to 12 semitones.
- **Variation.** It moves the grid by up to Variation/2.5 steps.
- **Timing.** Beats are counted at the first tempo in 4/4.
- **Randomness.** Random choices are seeded at reset.
- **Defaults.** 1 Bar, Gate 8/16, Grid 1/16, Mix, filter off.

### Filter Delay defaults

These are simplest-default choices pending reference measurement:

- **Pan:** a linear pan law.
- **Filters:** 12 dB/oct sections.
- **Ranges:** 1–2,000 ms, ±33% swing, −36 to +6 dB line volume, and Dry down to −70 dB, which is off.
- **Defaults:** lines 1 and 3 on at 1/8 and 1/4 with 50% feedback.

### Erosion defaults

These are simplest-default choices pending reference measurement:

- **Depth:** a 0.5 ms maximum.
- **Noise level:** the band-noise scaling.
- **Ranges:** Freq from 800 Hz to 18 kHz and Filter Width from 0.1 to 6 octaves.
- **Defaults:** 4 kHz and Amount 30%.

The Live 12.4 redesign ("Erosion Legacy" for older Sets) is not distinguished.

### Resonators defaults

These are simplest-default choices pending reference measurement:

- **Mode B:** a half-period negative comb.
- **Decay:** 0.05–20 s; without Const it scales by C3 / pitch.
- **Color:** a low-pass from 500 Hz to 20 kHz.
- **Defaults:** a 50 Hz high-pass input filter, a C3 root, and II–V at +12, +7, +19 and +4 semitones.

### Vinyl Distortion defaults

These are simplest-default choices pending reference measurement:

- **Distortion:** the polynomial gains (8 and 16) and the tanh used for Soft.
- **Crackle:** clicks of about 1 ms at a density of 0–50 per second.
- **Defaults:** Tracing at 1.2 kHz and Pinch at 3 kHz.

### CC Control defaults

Controls are sent on change and again at transport start or a jump back. Custom B–M default to CCs 20–31. These are simplest-default choices pending reference measurement.

### Dynamic Tube defaults

These are simplest-default choices pending reference measurement:

- **Curve:** the model thresholds and the tanh tube curve.
- **Tone:** a ±12 dB, 1 kHz tilt.
- **Ranges:** Drive from −12 to +36 dB and Output from −36 to +12 dB.

### Note Echo timing and velocity

The reference presets give the controls, ranges and defaults; the device's patch is encrypted and was not opened. These are simplest-default choices pending reference measurement:

- **Offset** scales every synced delay by (1 + Offset) and does nothing in Time mode. The reference may instead alternate long and short delays to swing them.
- **Velocity:** echo k plays at v · Delay · Feedback^(k−1), rounded, and the echoes end at the first one below 1.
- **Pitch** moves each echo one more step; the echoes end at the first one outside 0–127.
- **Settings in flight:** each note keeps the delay, Pitch and levels it was played with; changes apply to later notes.
- **Endless echoes:** at Feedback 100% the echoes continue until All Notes Off, a transport stop, or Pitch takes them out of range.

### MIDI Monitor display

The device's patch is encrypted and was not opened, and the manual describes only its displays. These are simplest-default choices pending a comparison with the reference:

- **The flow** is a timed text list of the last 64 messages, not a graphical stream.
- **Chord names** come from a table of 20 chord types. They are named from the lowest note when it can be the root, otherwise as a slash chord.
- **Freeze and Clear** belong to the display; they are not saved or automatable.

### Insert marker display and scrubbing

These are simplest defaults pending a comparison with the reference:

- **The marker** flashes across every lane. The reference shows it on the track that was clicked.
- **Clicks** snap the marker to the grid, unless Alt is held.
- **Scrubbing.** Clicks in the ruler jump immediately. Quantized scrub jumps and holding to loop a section are not implemented.

### Missing media matching

Locate Missing Media matches files by name only, case-insensitively, taking the first readable audio file found. The reference also compares size and checksums; Halftrack does not yet store them. A file moved while the project is open may keep playing from the engine's open handle until the project is reopened, even though it is reported missing.

### Plug-in parameter lanes

These are simplest defaults pending a comparison with the reference:

- **Bypass.** The parameter list includes any Bypass parameter a plug-in wrapper exposes; the reference ties that to the device's on switch.
- **Switching a lane off** leaves the plug-in where its last value put it; nothing restores an earlier manual value.
- **Moves in a plug-in's own editor** neither override its lanes nor record with Automation Arm yet.

### Automation Mode lanes

These are simplest defaults pending a comparison with the reference:

- **Saving.** The envelope a track shows, and its extra lanes, belong to the window and are not saved with the project.
- **Lanes** appear only in Automation Mode.
- **The main lane** shows None after the lane button moves its envelope into a lane.
- **Clips** stay movable outside the envelope line.
- **A double-click on the line** adds, deletes and re-adds a breakpoint, leaving one, in three undo steps.

### Performance capture scope

Only launches and stops made through Halftrack's launch commands are captured. These are not:

- **Engine-driven changes:** follow-action transitions, Gate and Repeat retriggers, and legato phase.
- **Loop wrapping.** With the loop brace on, the clips land at unlooped times.
- **Mixer moves.** They record only through Automation Arm.

Replaced Arrangement material is split at the recorded range when it can be. A clip that cannot be split stays underneath.

### Capture MIDI boundaries

These are simplest defaults pending a comparison with the reference:

- **The phrase** is everything since the last pause of over ten seconds, up to 4096 notes per track.
- **Tempo.** With the transport stopped, the current tempo is kept rather than detected from the playing.
- **Placement.** Clips start on the bar the first note fell in and last whole bars. In Session View they go into the first empty slot.

### Warp marker grid

Dragging a warp marker snaps to the coarsest power-of-two fraction of a beat that is at least 12 pixels wide at the current zoom. This is the simplest default pending a comparison with the reference's adaptive grid.

### Take lane scope

These are simplest defaults pending a comparison with the reference:

- **Storage.** Take lanes are kept as saved clip states on their track, so they play only while auditioned. Auditioning swaps them in for the main lane, and any edit, undo, save, export or recording ends it, where the reference lets take lanes be edited while auditioned.
- **Loop recording.** Each pass round the loop becomes a take lane, and the main lane ends with the last pass on top, as the reference's Comping chapter describes. Unmeasured choices:
  - From a stop, the take begins at the loop's start, as the engine's does.
  - With either punch switch on, a loop recording is not split.
  - A last pass under 10 ms is dropped.
- **Comp edges** get no automatic crossfades.
- **Showing lanes** is a per-track view choice and is not saved.

### Track output scope

These are simplest defaults pending a comparison with the reference:

- **Monitoring.** A track routed into another is heard through the destination whatever the destination's Monitor setting; the reference manual does not say.
- **Destinations.** Audio tracks are offered, as in the reference, which never offers returns. Group tracks and devices' sidechain inputs, which the reference offers through device inputs, are not.
- **Loops.** The reference's choosers do not filter routes that loop, and its engine has feedback units for them. Halftrack refuses them, as its engine would drop such a route, and refuses to open a project whose routes loop.
- **Leaving a group** ([routing contract](research/routing-contract.md)). A grouped track or group may play past its group, as the reference's Audio To offers. How the reference's group mute and solo act on a member playing past its group was not determined. Default: the group's mute and solo still reach it, as they did.
- **Group mute and its tracks' sends.** Muting a group silences the group's own post-fader sends. Its tracks' sends stay open, since only their own activators gate them. The reference's propagation of a group's mute to its members was not determined. Default: keep the members' sends open.

### Scene tempo and time signature

These are simplest defaults pending a comparison with the reference:

- **Timing.** A quantized launch changes the tempo and meter at its launch boundary, not on the click.
- **History.** Each change a launch makes is one undo step named "Launch scene", since the tempo is project content; the reference's handling is unknown.
- **Exceptions.** An automated tempo or a meter with several changes is left as it is, and nothing changes while the Arrangement records.

### Audio From scope

These are simplest defaults pending a comparison with the reference:

- **Pre FX on an instrument track** is taken after the instrument, so it carries audio; the reference manual does not say where its audio Pre FX tap sits.
- **One destination.** As with hardware inputs, a source feeds one track at a time; choosing it for another track moves it.
- **Exports** include a routed input while it is monitored In, or Auto and armed, as playback hears it.

### Note chance scope

These are simplest defaults pending a comparison with the reference:

- **Passes.** Chance and velocity range vary over 64 deterministic passes that repeat, decided from each note's identity, so a project plays the same way each time it is opened.
- **Unlooped clips** play their first pass every time until their graph is rebuilt.

### Re-Pitch warp markers

Warp markers rest while a clip is in Re-Pitch and new ones are refused, since the engine's warp reader always stretches. The reference lets Re-Pitch clips keep their markers. This is the simplest default until a resampling warp reader exists.

### Playback start ramp

Starting playback ramps the first few dozen samples in (39 at 48 kHz in the agreement suite), which an offline export does not. This is the engine's behaviour, kept to avoid clicks; whether the reference ramps its start is unmeasured.

### Clip envelope scaling

These are simplest defaults pending a comparison with the reference:

- **Volume modulation** scales the fader position, as the engine's scaling curve does. Whether the reference scales the position, the gain or decibels is unmeasured.
- **Device modulation** adds up to half a control's range each way. The reference's modulation range per control is unmeasured.
- **Session clip automation** applies only while the clip plays; the control then returns to its own value. Whether the reference keeps the last automated value is unmeasured.

### Warp mode defaults and scaling

These are simplest defaults pending a comparison with the reference:

- **Default warp mode.** Warping a clip chooses Complex. The reference's default (a preference) is Beats.
- **Beats.**
  - Transient Loop defaults to Loop Off.
  - Transients are found as the audio plays, half a second ahead, rather than from the whole file.
  - Preserve's grid divisions are not offered.
- **Grain Size** is 4 ms a step for both Tones and Texture (defaults 30 and 65); the reference's scale is unmeasured.
- **Complex Pro** offers Formants only; the reference's Envelope setting is missing.
- **Transposition** is refused for unwarped and Re-Pitch clips; how the reference transposes unwarped clips is unmeasured.

### MPE scope

These are simplest defaults pending a comparison with the reference:

- **Zones.** MPE plays and records in a fixed lower zone of 15 member channels with a 48-semitone bend range.
- **Instruments.** The native instruments respond to MPE pitch bend but not yet to slide or pressure.
- **Ramps.** Expression ramps between drawn points in 1/64-beat steps.

### Input configuration

A pair of input channels is a stereo input or two mono inputs, not both at once as the reference allows. This is the engine's device model; offering both would need a second device over the same channels.

### Linux CI recording stop

On Linux CI, a MIDI recording through a just-created virtual controller has occasionally stopped within its first two blocks, so the punch test's take went missing. It has not happened on macOS or Windows, or locally. The punch test now lets the routing settle and retries such a recording once, saying so in its log; the cause is not yet found.

### Preference defaults

Auto-Warp Long Samples is off by default and treats samples over 30 seconds as long. The reference's default is on, and its threshold is unmeasured. Turning it on by default would warp every long test fixture.

### A rare heap corruption

One run of the audio clip suite in a full local gate aborted on heap corruption that malloc detected in the engine's audio-file cache thread (creating a LoopInfo). That suite then passed:
- 35 ordinary runs, 32 of them under load;
- one AddressSanitizer pass over every suite;
- 16 more AddressSanitizer runs of the suite.

The AddressSanitizer pass found only a test's use-after-free read, now fixed. A data race, which AddressSanitizer does not detect, is the likeliest remaining cause. A ThreadSanitizer run is the next step.

### Linked-track matching

The reference's manual lists the edits that apply across linked tracks but not how it finds the corresponding clips. Halftrack's defaults:
- moves, trims, deletion and activation follow clips that start and end within 1 ms of the edited one;
- splits cut whatever crosses the split time on each linked track;
- take lanes and comps follow lane position, and comping skips a linked lane with no material in the range.

A move to another track, a duplicated track and a MIDI clip beside a trimmed audio clip are left out of the link. Auditioning stays on one track. All of these are unmeasured against the reference.

### Computer MIDI keyboard defaults

The keyboard starts off in each session and is not remembered. Its velocity steps of 20 and its octave limits are taken from the reference's documented behaviour, not measured. The reference may start with it on in a new installation; Halftrack keeps it off so its letter shortcuts work until it is turned on.

### A second virtual MIDI input and test recordings

When a Session made with the synthetic test device has a second virtual MIDI input, an Arrangement MIDI take in the punch suite starts about 1.3 seconds late and loses its first note. That second input was the computer keyboard's, and an unused virtual input added in the test does the same.

On record, the engine waits up to 100 ms for the audio thread to process a block. The synthetic device runs only when a test pumps it, so the wait times out and the punch-in is computed from an older sync point. A running audio device processes a block within that wait. On a MOTU M4 a MIDI take with a second virtual input started at 0.000 s with both notes, so real recordings are not affected.

Test Sessions therefore make the Computer Keyboard input only when the keyboard is first turned on. Whether the stale sync point can affect a stalled real device is open.

### Auto-Arm and single-track inputs

The reference gives every MIDI track All Ins and arms the selected one. In Halftrack, each input feeds one track, so Auto-Arm lends All MIDI Ins to the selected track only while no other track holds it, and takes it back on release. Whether the reference auto-arms tracks whose input is a specific port has not been compared.

### Audio device chosen while another is missing

While the chosen interface is missing, choosing a different device by hand is not yet told apart from the system's fallback. So the chosen interface is still reopened when it returns. Whether the reference switches back in that case is unmeasured.

### Signing, notarization and updates

Release builds are unsigned (ad-hoc on macOS) and Halftrack never checks for updates. Developer ID signing, notarization and Windows Authenticode need accounts the project does not hold. An update service would contact the network, which Halftrack does not do. Default: manual updates, by installing a newer package; settings and the User Library carry over. Whether to add an opt-in update check is open.

### Plug-in sidechain controls and default tap

The reference's plug-in sidechain has Gain, Mix and Mute controls that Halftrack does not have yet. The manual names no default tap; Halftrack uses Post FX because every track-sourced sidechain in the reference's demo sets reads it, which is evidence of use rather than a measured default. Whether the reference's sidechain switch turns on by itself when a source is chosen is unmeasured; Halftrack switches it on.

### MIDI ports on Windows and clock after rebuilds

Windows cannot make virtual MIDI ports, so the MIDI ports suite skips there and Windows MIDI output is checked only by building; Linux runs it only where the ALSA sequencer is available. The engine's clock restarts when the playback graph is rebuilt (after a settings change) and names its position again at the next beat, so a slave device can miss up to a beat of clocks; the reference's behaviour in that case is unmeasured.

### Device insertion

The reference appends a device to the end of the chain when none is selected, and places it after the selected device otherwise. Halftrack does not yet insert beside a selected device: every new device, built-in or plug-in, joins the end of the chain (MIDI effects ahead of the instrument), whatever is selected. Default: append.

### Audio input permission on real hardware

The permission handling is tested with a stand-in for macOS's permission and prompt. Whether a device opened before a grant delivers sound without the reopen Halftrack does, and how Windows' microphone privacy setting shows up (a device open error), are unmeasured. Default: reopen the device after a grant; report Windows' refusal as the device's own error.

### Where an auto-warped long sample starts

The reference's Auto-Warp of a long sample places its first warp marker on the first downbeat. Halftrack also moves the clip's start onto the first beat it finds, so leading silence or a pickup is outside the clip and the beat lands where the clip was dropped. It finds beats, not bars, so the first beat may not be bar 1. Whether the reference moves the start marker in every case, and how it chooses the downbeat, were not measured. Simplest default: start on the first beat, with the audio before it outside the clip.

### Clip starts before a loop, and unlooping a long clip

The reference lets a clip's Start marker sit before its loop, so the content before the loop plays once. The engine counts a looped clip's offset from its loop's start, so Halftrack requires the start to lie in the loop. Turning a long looped MIDI clip's loop off keeps its length, playing silence after the content; a warped audio clip is shortened to end with its audio. The reference's handling of a long clip's unlooping was not measured. Simplest default: as described, with the start inside the loop.

### CPU overloads during takes

Under heavy machine load (load average about 16, with four suites at once), the separation stress suite once found a gap in a take: the first wrong sample at 236544 of 375040, a whole number of 256-sample blocks. The engine's device manager silenced any callback once its load meter passed 0.98. A silenced callback skipped the input, rendering and stream time together, so the take lost that block and the transport stopped for a block. Halftrack now keeps rendering at any load, and `tests/RecordingStallTests.cpp` drives the meter past 0.98 with injected stalls. Before the fix that suite lost 6 blocks in 450; after it, every sample was kept and the output never fell silent.

Still open:
- The gap seen once under real load was not reproduced again under that load. Generating it would have slowed another session's test runs on the same machine. Its block-aligned shape matches the silenced callbacks, and no other path in the engine was found to drop a block the driver delivered.
- What the reference does at 100% CPU is unmeasured. It shows an overload indicator, but whether it ever skips input or rendering was not checked. Simplest default: never skip; a callback that runs late shows up as the driver's own dropout.

### Moving and duplicating Session clips

The reference's manual says clips move around the Session grid by drag-and-drop and that the Edit menu's commands work on them. It does not say:

- what Duplicate does when the slot below holds a clip;
- whether a playing clip dragged elsewhere keeps playing;
- which modifier copies a drag.

Defaults:

- Duplicate inserts a new scene below for the copy rather than replacing the clip there. A forum report says duplicating from the last scene makes a new scene.
- A clip moved within its track keeps playing, its slot changing places with the destination's; moved to another track it stops.
- Option (Mac) or Ctrl (Windows, Linux) copies, as in the reference's Arrangement.
- Dropping or pasting onto a clip replaces it.

### The editing grid's state, tuplets and rounding

The grid follows a static reading of the reference's code ([contract](research/edit-grid-contract.md)). Defaults for the gaps:

- **Adaptive step.** The reference keeps one per time-signature section and moves it with hysteresis. Halftrack chooses the step nearest the spacing afresh at each zoom. Unmeasured: 1 Bar against 1/2 in 3/4, where the two can differ.
- **Rounding.** Every snapped edit goes to the nearest line. The reference also rounds down for some gestures, and which ones is not known.
- **Tuplet commands.** Quintuplets and septuplets are named but have no command. A feature flag gates them in the reference, and whether it is on by default is unknown.
- **Saving.** A grid change is not an unsaved change in Halftrack. Whether the reference marks the Set modified for it is unmeasured.
- **Clip grid defaults.** A new clip's grid is the reference's adaptive default, not the Arrangement's current grid. Whether the reference copies the Arrangement grid into new clips is unmeasured.

### Selecting several Arrangement clips

The reference's Shift-click extends a time selection, which can take in parts of clips. Halftrack's Shift-click selects whole clips: the clicked one and every clip wholly within the span. Default: copy, cut, duplicate and delete act on the whole clips selected. A group drag snaps the dragged clip and moves the others by the same amount, which is unmeasured against the reference.

### Consolidate Time to New Scene

The manual says the command makes one new clip per track from the selection in a scene below the selected one. Defaults where it is silent:

- The clips loop their whole length.
- Audio is warped at the tempo it was rendered at.
- Automation stays in the Arrangement.
- With no scene selected, the scene goes at the end.

### Automation curve shape

The reference's curved automation segments are drawn and played with a shape that is unmeasured. Halftrack uses the engine's quadratic Bézier, with a curve from -1 to 1. Halftrack draws the form the engine plays: `AutomationIterator`'s Bézier of the stored curve, not `AutomationCurve::getValueAt`'s, which doubles it.

### Scroll follow

With Follow Behavior Scroll, the view keeps the play head at its middle once the play head has reached the middle, and pages at the start of the song (it never scrolls before zero). Whether the reference's Scroll keeps it centered or at another fraction of the view is unmeasured.

### Step recording

Steps stop at the end of the clip's content, where the insert marker stops; whether the reference extends the clip when stepping past its end is unmeasured.

### MIDI editor Preview

Defaults the reference's manual does not give:

- A ruler key sounds at velocity 100.
- Notes transposed with the arrow keys sound for 200 ms.
- At most eight notes sound at once.
- A plain click on a key only sounds it; Shift-click selects that pitch's notes, as the manual says.
- Moving a note sideways does not sound it again.

### Start Recording on Scene Launch

With the option on, a track already recording into a slot when another scene launches only ends that take; no new take begins in the launched scene's slot. Whether the reference starts recording there is unmeasured.

### Default clip fades

Halftrack's default fades follow a static reading of the reference's code ([contract](research/clip-fades-contract.md)). Defaults where it is incomplete:

- **Material before the start.** A crossfade needs the later clip to have at least 4 ms of material before its start. Otherwise the two clips each fade on their own, leaving a short dip at the boundary. What the reference plays with less material is unresolved.
- **Session clips.** The Clip Fade switch, and the launch fade-ins and stop fade-outs it gives, are not implemented. Session clips carry the default flags only for when they reach the Arrangement.
- **Loop wraps.** Warped, looped clips do not overlap their loop wrap by min(half the loop, 4 ms) as the reference's do.
- **Sample alignment.** The reference evaluates its curve at points at most 32 samples apart and interpolates linearly, and rounds edges to 1/3843840 beat. Halftrack evaluates every sample. A split crossfade is exact either way, but single edges may differ from the reference's by up to about 0.02 of full scale. Unmeasured.

### Metronome

- **The sounds.** The reference's Classic, Click and Wood are its own samples, so Halftrack synthesises its own ticks to their character; they are not the same sounds.
- **Count-in start.** As the reference's (static reading), a count-in starts on the bar line nearest to its bars before the start, from the timeline's start since Halftrack has one meter. The reference measures from the time-signature section's start. Its block-boundary delay of the start is not reproduced.
- **Rhythm timing.** The reference snaps ticks to its tick grid from the time-signature section start, and while stopped times them in whole samples. Halftrack places them on the beat grid from the start; it has one meter per project.
- **Punch-In and the click.** The reference's manual says that with Enable Only While Recording and Punch-In the click is heard only after the punch-in point, and Halftrack does so; a static reading of its code finds no punch term in the click, so this is unmeasured.

### The default project and track names

The reference's built-in default Set (read 2026-10-22) holds two MIDI tracks without instruments and two audio tracks, named 1-MIDI, 2-MIDI, 3-Audio and 4-Audio. It also has two returns, A-Reverb (with Reverb) and B-Delay (with Delay), eight scenes, 120 BPM and 4/4. Its tracks name themselves from their position and their first device until renamed, as Halftrack's now do ([contract](research/track-names-contract.md)). The app now starts, and makes new projects without a saved default set, from a built-in one with those tracks, devices, scenes, tempo and meter ([contract](research/presets-contract.md)). Its returns show A-Room Reverb and B-Stereo Delay after Halftrack's devices. The Session's own new project, which the suites build on, keeps two audio tracks. Insert MIDI Track now makes an empty MIDI track, as the reference's does. MIDI tracks made by MIDI file import, Slice to New MIDI Track and the Session API (which the suites build on) still get the built-in synth.

### Mixer sections

The reference's Performance Impact section is not offered: there is no performance meter. The Arrangement's own mixer sections (its `Arranger*` view states) are not switchable either ([contract](research/mixer-sections-contract.md)).

### Song start

A locator or the loop brace can be the song's start, as in the reference. Whether the reference's song start follows a loop brace moved afterwards is unknown (no update was found in its code); Halftrack's does, since it is the loop's start whenever Play starts. The ruler's play triangle marking the song's start is Halftrack's own sign.

### Track order

Return tracks stay after every other track, as the reference keeps them. Insert puts a new audio or MIDI track after the last selected track, or after the other tracks and before the returns, as the reference's does ([contract](research/track-names-contract.md)); an unfolded selected group and selected clips do not take part. Moves cannot cross the returns. A project from an earlier version that holds a return among the other tracks opens with its returns moved last, outside the undo history.

### Cue

Defaults where the reference's cue is not followed exactly ([contract](research/cue-contract.md)):

- **Groups.** A group cannot be cued; its switch is disabled in Cue mode. The reference cues a group from after its devices.
- **Undo.** Cue Out and the Solo/Cue mode are kept with the project without undo steps, as Halftrack's Master Out is. The solo and cue switches are not undo steps, as in the reference; the reference probably makes Cue Out one.
- **The level's ramp.** A cue level change ramps across one audio block; the reference ramps its cue gates over a length set elsewhere.
- **MIDI.** A cued MIDI track is heard through its instrument; what the reference cues of a track's MIDI is unexamined.

### Output routing

Defaults where the reference does not map onto the engine ([contract](research/output-routing-contract.md)):

- **Ext. Out to the master's output.** The reference lets a track play with Ext. Out to the same output as the master, bypassing the master's devices and volume. In the engine everything on the default output is the master, so that is refused.
- **Names.** Outputs are named as the engine names them ("Left + Right", "3 + 4"), not the reference's "1/2" and "3/4".
- **Undo.** Changing the Master Out is kept with the project and marks it changed, but is not an undo step.
- **Cue.** See [the cue](#cue).

### MIDI and key mappings

Defaults where the reference's remote control is not followed ([MIDI mapping](research/midi-mapping-contract.md), [key map](research/key-map-contract.md)):

- **Remote switches.** Every MIDI input acts as a remote, except the computer keyboard and All MIDI Ins (whose sources already do). The reference has a Remote switch per input port.
- **Note ranges.** A note maps alone. The reference can map a range of keys to a slot to play it chromatically, or spread a control's values across a range.
- **Launch velocity.** How the reference handles a velocity change while a launch plays (whether it ramps), before any launch, in Arrangement playback and for follow-action launches is not read. Halftrack's starts at the launch, is full before one, and keeps the last launch's for follow actions ([contract](research/launch-modes-contract.md)).
- **Switch ranges.** A controller mapped to a switch control (a device's two-state control) takes the nearest step along its range rather than turning on inside Min–Max.
- **Learning over a mapping.** Learning a note or controller already mapped elsewhere moves it without the reference's confirmation.
- **Solo and arm timing.** The reference gathers solo and arm presses from remote controls on a short timer and applies them together; Halftrack applies each at once.

### The loop brace

The reference's manual says Up and Down move a selected loop brace "in steps equal to its length" without naming a direction; Halftrack moves it earlier with Up and later with Down, and doubles it with Cmd/Ctrl+Up. A click on the brace selects the loop's time across every track as Select Loop; whether the reference's also selects the clips inside is not checked. Dragging a handle past the other end stops a grid step short ([contract](research/transport-contract.md)).

### Session count-in

A Session take that starts the transport counts in, as the reference's does. Where the reference starts clips launched with a recording scene, and Session Record's overdubs, during a count-in is not read; Halftrack starts them with the count-in ([contract](research/session-recording-contract.md)).


### Mixer laws

Volume and sends follow the reference's level scales ([contract](research/mixer-laws-contract.md)). Defaults where the rest is not followed:

- **Pan modes.** The reference's menu wording ("Split Stereo Pan Mode", "Stereo Pan Mode") is inferred from its message keys and a test phrase. The English text is in its encrypted language file, which was not decrypted. The Main's pan mode is not built, because Halftrack's Main pan is not yet in the interface. Automation of the pans of the other mode keeps playing, inaudibly, as the reference's probably does.
- **Automation timing.** The reference plays volume, send and pan automation in ramps of 1/128 of the range, at least 256 samples long, and smooths over 4 ms. Halftrack's engine updates automation per block and smooths on its own schedule. Its end levels and its straight path along the travel match the reference.
- **Display.** The reference's dB text reads −inf from −69.7 dB (medium confidence). Halftrack reads −inf only for silence, so a level just above it reads −69.9 dB.
- **Rack chains.** Chain volume and chain sends take the track laws. That the reference's chains do too is inferred from their shared mixer, not traced.
- **Volume modulation.** A clip's volume modulation scales the fader's position, now along the reference's law. Whether the reference's modulation scales the position or the gain is not read.
- **Cue.** The cue volume stays in decibels, with −70 dB as silence, rather than the reference's 0.00032 gain floor.
- **Selection linking.** How the reference groups linked moves into undo steps is not read; Halftrack makes each move one step with its linked moves. Groups, the Main, rack chains and device XY editors are linked in the reference but not yet in Halftrack. In the Session view a selection of several tracks is made in the Arrangement's headers.
- **Older projects.** Projects saved before the change keep every level when they open. Levels below −70 dB become silence, and segments between automation points take the new law's shape.

### Send automation

Send lanes follow the reference's automatable Send parameter ([contract](research/arrangement-automation-contract.md#send-lanes-2026-10-23)). Defaults where its details are not read:

- **Names.** The automation chooser names a send for its return (for example A-Echo). How the reference's choosers name sends is not read.
- **Frozen tracks.** Editing a frozen track's send lane is refused, as for its volume and pan lanes. Whether the reference allows mixer automation edits on a frozen track is not read.
- **Off sends.** Points on a send that is Off make the send in the same undo step; the reference has every send all along.

Track header menu ([contract](research/track-header-menu-contract.md)). Defaults where the reference's details are not read:

- **Right-click selection.** Right-clicking an unselected header selects its track or group alone, and right-clicking a selected one keeps the selection. The reference's menu code only marks the clicked track.
- **Labels.** The items are named plainly for their commands (Duplicate, Delete, Rename, Freeze Track, Select Content and so on), as the reference's English text is in its encrypted language file.
- **Colours.** The colour grid uses Halftrack's own palette, in the reference's 5 by 14 layout.
- **Freeze.** Freezing several tracks from the menu makes an undo step for each track.
- **Insertion.** Insert Audio, MIDI and Return Track insert after the selected track, as Halftrack's Create menu does; where the reference's insert was not decoded.
- **Default tracks.** Save as Default Audio/MIDI Track saves to the User Library only. The reference saves into the current project instead when it already has a default for the kind, and loads a project's defaults first; Halftrack has no per-project defaults. Whether its overwrite prompt's Cancel stops the save was not determined; Halftrack's does.

Main menu bar ([contract](research/main-menu-contract.md)). Defaults where the reference's details are not read:

- **Windows and Linux.** Only the reference's macOS build was read. Elsewhere Halftrack puts About Halftrack in a Help menu and Preferences at the end of Options, and takes Redo as Ctrl+Y and Fullscreen as F11, as the reference's platform switch does; other Option shortcuts stay on Alt.
- **Focus.** Edit's commands act on the area last clicked, standing in for the reference's focused view; typed text always comes first.
- **Metronome While Recording.** Taken as Halftrack's metronome's only-while-recording switch.
- **Labels.** Items are named plainly for their commands, as the reference's English text is in its encrypted language file. Undo and Redo do not name the step to undo.
- **Preferences.** Preferences is one window with the reference's tabs (see the [preferences contract](research/preferences-contract.md)).

Warp defaults ([warp contract](research/warp-contract.md#defaults-not-yet-the-references)):

- **Default warp mode and Auto-Warp.** The reference's are Beats and on; Halftrack keeps Complex and off until its Beats plays a slowed constant clip without silences between segments and a constant level without overshoot. Which transient loop mode the reference's Beats uses by default was not read.

