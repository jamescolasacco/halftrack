# Audio clip editing contract

Halftrack adds independently authored controls over its pinned native engine model. This implements useful editing workflows, not measured reference-equivalent DSP or complete clip-editor parity.

## Public workflow evidence

The reference [Arrangement documentation](https://www.ableton.com/en/live-manual/12/arrangement-view/) describes non-destructive clip positioning, splitting, edge fades and overlaps. Its [Clip View documentation](https://www.ableton.com/en/manual/clip-view/) distinguishes clip gain and the Session audio Fade toggle from adjustable Arrangement fades. These pages inform workflow scope only; no manual text or UI assets are included in Halftrack. The independently designed modal editor exposes exact seconds and gain with an envelope display.

The implementation uses pinned Tracktion source at `07e2d674884293f1a6c8f35fb8d1b9f469ffa41f`: AudioClipBase for gain/fades, ClipOwner split and native duplicate operations, and the graph builder's Arrangement fade node. The launcher graph omits that fade node, so manual fade controls are disabled and rejected for Session clips. Session gain is implemented; its separate edge-fade toggle remains missing.

## Parameters and invariants

- Gain is finite, -100 through +24 dB. Fade durations are finite, nonnegative seconds whose sum cannot exceed clip duration. Native curve IDs 1–4 mean linear, convex, concave and S-curve. All five fields form one undoable setting change; slider drags group their intermediate updates.
- For normalized rising position `x` in [0,1], the independent test expectations are `x`, `sin(pi*x/2)`, `1-cos(pi*x/2)`, and `(1-x)*(1-cos(pi*x/2))+x*sin(pi*x/2)`. Falling edges use reversed position. These are explicit Halftrack/native-engine curves, not claims about another product's curve arithmetic.
- Range editing accepts unwarped, unlooped, forward, ungrouped, normal-speed Arrangement audio. Start is nonnegative, length is at least 1 ms, source offset is nonnegative and the complete region must fit the source and the engine's 48-hour timeline. Existing fades must still fit. Actual native warp metadata disables this path even if tempo synchronization is off.
- Split requires an interior point, at least 2 ms on each side, outside both existing edge fades. The right half receives a fresh native identity and retains the correct source offset. Duplicate uses a fresh identity, keeps source/settings and begins at the selected clip's end. Both are one undo transaction.
- Source files are never rewritten. Save As collects source references needed by current clips and redo history using existing project ownership. Invalid edits preserve state/history; malformed saved gain/fade values reject before replacing the current edit.

## Startup gain regression

The synthetic launcher test originally found a 0.125 opening signal despite a saved -6 dB clip (expected approximately 0.0626484), with gain becoming correct later. Source tracing identified that the stretch reader primes inside its constructor, before the first `processSection` call sets resampler gain. `cmake/PatchClipGain.cmake` initializes that gain before construction, preserving the established processing path. No additional callback allocation, lock, logging or model access is introduced. Dependency internals still have the separately documented real-time limitations.

The patch accepts exact original SHA256 `1b92d35da33ca2495f3a2627f4a985c54c651285370e63f036491de953cdd980` and produces `b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76`. Validation checks fresh application, repeat application, exact output bytes and rejection of unknown source. All dependency copyright/license notices remain intact.

## Automated evidence

`audio-clips-acceptance` uses generated 48 kHz stereo media and compares every rendered sample to independently calculated expectations within 2e-6 absolute error. It covers four curves at positive/negative gain, trim/source offsets, complete split preservation, duplicate placement, overlapping complementary fades, save/reopen, Save As with retained redo, unchanged source bytes, invalid values and native warp rejection. The actual Session graph uses a synchronous generated reader to isolate scheduling from accelerated disk-cache timing. Its first block may contain native startup fade but must never exceed the saved gain; all subsequent samples through a source-loop boundary must match the expected level.

Production component-handler checks exercise grouped gain drag, full range submission, undo, missing-clip disable/recovery and rendered editor layout. The main-window suite invokes actual split/duplicate commands, verifies selection of the fresh identities, and opens/closes the editor across project replacement. These are not physical mouse/device tests.

## Remaining boundaries

Linked/automatic crossfades, consolidation, multi-clip/time-range clipboard, Session edge-fade toggle, warp marker editing, native pitch/reverse controls and arbitrary tempo-map comparisons are unfinished. Current range controls do not alter warp or loop geometry. No paired reference render, subjective listening study or physical-interface latency measurement is claimed.

## Direct Arrangement gestures

Selecting an audio clip exposes its actual fade envelope and two orange duration handles. Dragging them changes seconds without grid snapping, clamps at zero and against the other fade, and groups intermediate changes into one undo transaction. The handles use different vertical positions so meeting fades remain selectable. Narrow clips under 20 pixels retain the numeric editor. A drag resolves the original stable clip identity on every update; a replacement at the same track/index is never edited accidentally.

Cmd/Ctrl+E splits selected Arrangement audio at the playhead; Cmd/Ctrl+D duplicates it. These commands preserve focused text editing and do not override Session/piano-roll contexts. Production pointer/keyboard tests cover both ends, clamping, undo/redo, meeting handles, disappearing source clips, new identities, and real text-editor focus. Native OS pointer delivery and listening remain separate checks.

## Clipboard, range gestures and track placement

Cmd/Ctrl+C/X/V now supports one single-source Arrangement audio clip across audio tracks and project switches. Copy retains independent media before Cut removes the source; Paste collects a new file into the destination project, remaps native IDs and inserts without overwriting overlaps. Copy creates no undo action. Failed Cut preserves the existing clipboard. Multi-take comping and warped/looped/grouped clips are rejected for this clipboard path. State and every-sample render tests cover retained fades/gain/offsets, original media removal, destination save/reopen and one-step cut/paste undo.

Selected clip edges now trim directly; Alt bypasses the existing grid and Shift+Alt-body drag slips the source without moving timeline bounds. Source boundaries and retained fades constrain the range. Cross-track audio body dragging retains identity and media while changing its native owner and start in one undo transaction. Destination volume applies through the normal mixer; an independent full-file PCM oracle verifies the moved excerpt and gain. MIDI/return/frozen targets and timeline overflow reject before mutation. UI tests cover multi-event gestures, repeated ownership resolution, keyboard focus, and replacements at stale indices.
