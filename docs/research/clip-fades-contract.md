# Default clip fades contract

The reference fades audio clips at their edges unless told not to. Halftrack follows a static reading of the reference's code: Ableton Live 12.2.1, arm64. The raw material and the full findings are in the ignored `artifacts/reverse-engineering/fades/`. Its manual, consulted 2026-10-22, says:

- with Create Fades on Clip Edges on, "adjacent audio clips will automatically get 4 ms crossfades";
- "pressing Delete returns the fade handle to a default length of 4 ms";
- "These default fades help prevent pops or clicks at clip edges".

## What the reference does (static findings)

- **The setting.** `AudioClipFadeButtonDefault` (shown as Create Fades on Clip Edges) defaults to on. A new audio clip takes it as its `Fade` flag. On reaching the Arrangement, the clip's two edges become default fades (`IsDefaultFadeIn/Out`, with no stored length).
- **The default edge fade.**
  - Its length is 4 ms, fixed in time: 192 samples at 48 kHz, 176 at 44.1 kHz. It does not depend on the signal.
  - Its shape is the smoothstep 3x² − 2x³. The fade-out is its mirror.
  - When a clip is shorter than both default fades, each takes half the clip. A default edge beside an ordinary fade takes what remains.
- **Crossfades.** Two clips on a track whose default edges touch exactly crossfade, and nothing about the crossfade is stored.
  - The later clip starts early by the earlier clip's fade-out, playing the material before its start, and fades in over that span.
  - The earlier clip fades out over its last 4 ms and plays nothing past its end.
  - The smoothstep pair sums to one, so a split plays back as the unsplit clip.
- **Splits.** A split's two new edges become default fades, by the setting; the outer edges keep theirs.
- **Editing a fade.** Dragging a default fade, or changing its curve, makes it an ordinary fade. Delete returns it to the default.

## Halftrack

- **The setting.** `Preferences::createFadesOnClipEdges` is on by default, and set in Preferences as "Create Fades on Clip Edges".
- **Clips that get default fades.** By the setting, both edges are default fades on:
  - an imported audio clip;
  - an Arrangement take;
  - a flattened or consolidated clip;
  - a clip imported to, or recorded into, a Session slot (where they wait for the Arrangement).
- **Split edges.** Every split of an audio clip, whether a Split, a time-selection edit or a cut at a recorded take's edges, makes the two new edges default fades, or hard edges with the setting off.
- **Storage.** An edge's default state is a clip property (`halftrackDefaultFadeIn/Out`), kept through copies, undo and saving. The edge keeps no length of its own.
- **Reporting.** `AudioClipSettings::defaultFadeIn/Out` mark the default edges. Their fades read as the lengths they play, with the new Smooth curve (`FadeCurve::smoothstep`).
- **Editing.**
  - Setting a length or curve on a default edge makes it an ordinary fade.
  - Setting the flag makes it default again; the audio clip editor's Default switches do this.
  - Clicking a fade handle in the Arrangement selects it, drawn filled. Delete then returns it to the default fade, or, with the setting off, removes it, as the reference's manual describes.
  - Each is one undo step, as before.
- **Playback.** An engine patch (`cmake/PatchClipFades.cmake`) adds:
  - the smoothstep to the engine's fade curves;
  - two behaviour hooks, which Halftrack answers when a clip's node is built, giving the clip's derived fades and how long before its start it plays;
  - node-builder changes that read each kind of clip's source, and run the clip's node, from that much earlier.

  Every render path (playback, export, stems, freeze) goes through the node builder. The derived values are recomputed at each graph build, so trims, moves and tempo changes need no upkeep.
- **Crossfade conditions.** A crossfade needs both touching edges to be default and the later clip to have at least 4 ms of material before its start, with no loop. Otherwise each clip fades on its own.

## Evidence

`tests/ClipFadeTests.cpp` (`clip-fades-acceptance`) renders a constant level and a 997 Hz sine, sample by sample, at 48 kHz. It checks:

- the default setting;
- that an imported clip's edges are reported as 4 ms smoothstep defaults;
- that the clip renders silence before its start, the smoothstep over its first and last 192 samples, and its level between (all exact);
- that a split's edges are defaults, and the split renders as the unsplit sine within 1e-7, half way through the crossfade included;
- that a 100 ms linear fade-in set on a default edge becomes an ordinary fade, rendering linearly from the boundary with no crossfade;
- the Default switch's return to the default, undo, and saving and reopening;
- that a 6 ms clip splits its default fades 3 ms each;
- that with the setting off, a new clip plays at full level from its first sample to its last.

The audio clip editor's checks (in `audio-clips-acceptance`) switch an edge to Default and back, and undo both. The UI suite checks, with the setting on:

- that a new clip's fade-in handle sits 4 ms into the clip;
- that dragging it 40 pixels makes an ordinary 14 ms fade;
- that clicking the handle selects it, and Delete returns it to the 4 ms default, keeping the clip;
- that undo steps back through both.

It also finds Create Fades on Clip Edges on in Preferences, and switches it off.

## Not yet

These are recorded in the [open questions](../open-questions.md#default-clip-fades):

- The Session Clip Fade switch, and the reference's Session launch and stop fades.
- The overlap at a warped, looped clip's loop wrap.
- What plays when the later clip has less than 4 ms of material before its start.
- Exact alignment with the reference's renders: the reference evaluates its curve at points up to 32 samples apart and interpolates linearly between them, and rounds to a beat grid. Neither is measured.
