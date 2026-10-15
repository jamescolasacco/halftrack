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

Default: import PPQ-based type 0/1 files at the playhead in beats, into separate new MIDI tracks for each file-track/channel pair. Preserve notes, velocities, release velocities, CC, program changes, pitch bend, poly/channel pressure and SysEx; attach channel-independent SysEx once per source track. Keep the current project tempo/meter and expose imported notes through the existing synth/piano roll. Reject SMPTE/type 2, unmatched or overlapping same-pitch notes, and unsupported system events rather than silently repairing them. Limit imports to 16 MiB, 100,000 events and 64 resulting tracks. Export selected clip source events as a type 0 file at 9600 ticks per quarter, independent of Arrangement placement; clip loops, offset/groove rendering and project tempo maps are not baked into exported events. Metadata other than the track name and exported current tempo is outside this initial interchange path.

Export retains source events rather than printing clip playback transformations. Muted notes are omitted; shared timestamps order SysEx/controllers before notes. Notes shorter than one export tick become one tick. A source extent above 268,435,455 ticks at 9600 PPQ is rejected before writing; adaptive export resolution and long-gap event splitting remain future work.

### Arrangement MIDI clip clipboard

Default: copy one unlooped, ungrouped, single-take Arrangement MIDI clip at normal speed and zero source offset. Keep all native note/controller/SysEx settings and beat duration when pasting into a MIDI track, including after a project switch to a different tempo. Give each pasted/duplicated clip and its notes fresh identities. Cut/Paste/Duplicate each undo once; Copy does not dirty the project. The most recent successful audio or MIDI clip copy replaces the other clip type. Piano-roll note and system text clipboards remain independent. Looped/offset/comped/Session/multi-clip copies remain separate work.

- Moving a MIDI clip to another track uses that track's existing instrument and processing. It does not move or duplicate the source track's devices. The current project has one constant tempo; clip movement retains duration and source offset. Tempo-map-aware relocation remains separate future work.

- Track ordering applies to the single shared top-level list, including return tracks. Returns can appear before their sources because routing uses bus identity. Context menus offer up/down in Arrangement and left/right in Session; the Track menu offers earlier/later in either view. Nested groups and track-header drag gestures remain future work.

- Whole-track Duplicate copies ordinary unfrozen top-level audio/MIDI tracks immediately after the source, named with a “copy” suffix. It keeps mixer mute/solo, device state and sends to existing returns, shares immutable audio media, and starts without recording inputs or active Session launches. Return/group/frozen duplication is rejected until its bus/group/freeze behavior is implemented.

- Global project tempo now consistently supports 20–999 BPM, matching the facade's intended range. The text editor rejects out-of-range/incomplete input; finite programmatic requests clamp and nonfinite requests do nothing. Small edits must retain musical clip positions rather than use the native 1 ms remapping dead zone. Tempo maps, tap/nudge and signature changes remain separate work.

### Controller recording defaults

Default: one named native audio or MIDI input per track, with exclusive assignment. A newly discovered physical MIDI input opens only after explicit selection; choosing All MIDI Ins explicitly opens the currently available inputs. Saved enablement remains an application preference. Record into Arrangement, unquantized, with native merge overdub into overlapping clips. Keep the current non-MPE single clip-channel behavior and document it. Finish a take on Stop or seek; defer arm, monitoring, input reassignment and history operations until recording stops. Empty MIDI takes add no clip. MIDI channel filtering, duplicate-name disambiguation, record quantization, MPE, replace/take-lane selection and Session capture need separate controls and tests.


### Metronome and count-in defaults

Default: click Off, first-beat accent On, gain 60%, recording-only Off, count-in None. Expose zero/one/two 4/4 bars; keep the native half-beat preparation lead before the first count-in beat. Click and count-in use the main output and do not appear in offline exports. A count-in applies when recording begins from stopped; punching Record during playback does not rewind. Click settings belong to the project and undo together. Separate cue outputs, signature changes, subdivisions and custom samples require later controls and tests.


### Recording quantization

Default: Off. Optional quarter/eighth/sixteenth/thirty-second note grids and eighth/sixteenth triplets snap new recorded note starts to the nearest project beat grid, with exact half-grid ties going forward. Preserve note durations, velocities and controller timestamps. Bound the onset to the captured clip so a note is not moved outside the available source range. Existing overdub notes keep their timing. Apply on Stop within the same take undo transaction; input monitoring remains unquantized. Store the grid per project, reject changes during recording, and use raw native input capture to avoid a second device-specific quantizer. Loop recording and overdubbing into offset/looped clips with recording quantization enabled require separate work; reject those combinations before capture.


### Master output defaults

Default: use the native master-volume stage and fader law, from silence (-100 dB) through +6 dB, initially 0 dB. Expose a single stereo output-level fader beside the meters, including typed dB entry and double-click unity. Apply it to playback and export after return contributions. Master pan, master device insertion and separate cue output are future features.


### Tap tempo

Default: update after the second tap, average at most four recent intervals, ignore taps faster than the supported maximum, and restart after a pause over three seconds. Do not auto-start playback. Each resulting tempo change undoes once; tapping the same tempo and the first anchor do not dirty the document. Normal tempo edits, history and project replacement clear the transient estimate. No claimed match to an undocumented reference tap algorithm.


### Clip activation while playing

Default: deactivate a whole audio/MIDI clip without removing its contents or Session ownership. Send note release through the engine; existing synth/effect tails may decay. Re-enabling allows future Session events without relaunch; full held-note/controller chasing and reference-exact transition timing remain unverified. Frozen tracks must be unfrozen first. Unmodified 0 acts on whole clips in Arrangement/Session only; note activation is separate work.


### MIDI note split policy

Default: split crossing notes at the piano-roll marker, using the current selection or all notes when none are selected. Exact note edges do nothing; any sub-1/960-beat fragment rejects the group. Splits retrigger the note and preserve ordinary properties with fresh right-half identities. Per-note expression children are rejected until their envelopes can be cropped and shifted explicitly.
