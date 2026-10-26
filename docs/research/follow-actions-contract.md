# Follow actions contract

Halftrack's Follow Actions follow the reference's, for Session clips and scenes.

Sources:
- A static reading of the reference's code on 2026-10-25: Live 12.2.1, arm64 slice, sha256 b37cb738442bba7787457fe751b0e3f9fbdabc662d8d491d1d9aa46b120bd3fc. This was binary-assisted research, not a clean-room process. Nothing was launched or measured, and the raw notes stay in the ignored `artifacts/` folder.
- The reference's manual, read 2026-10-26: section 16.7 (Follow Actions) and the key table, section 42.15.

## Behaviour

- **Settings** (`Session::FollowSettings`, for each Session clip and each scene). The defaults are the reference's:

  | Setting | Default | Range |
  |---|---|---|
  | Follow Action switch | off | |
  | Time | 4 beats | a quarter beat up to 16384 beats |
  | Linked | on | |
  | Multiplier | 1 | 1 to 2^30 − 1 |
  | Action A | Next | |
  | Action B | No Action | |
  | Chance A / Chance B | 100 % / 0 % | 0 to 100 % each |
  | Jump target A / B | the second scene (row 1) | a scene row, or none |

  - The actions, in the chooser's order: No Action, Stop, Play Again, Previous, Next, First, Last, Any, Other and Jump.
  - Editing either chance rounds it half away from zero and sets the other to 100 minus it.
  - After a load, the chances become percentages: A is its rounded share of the two, and B the rest. Both at 0 load as A at 100 % with the switch off.
  - When choosing, the chances are used as weights.
- **When a clip's action is chosen.** It is drawn once, when the clip starts, from its track's own random draws (2026-10-25 reading):
  - Each start makes two draws from the reference's generator, a 32-bit linear congruential state stepped by 1664525 and 1013904223. The first draw picks A or B: B when r × (A + B) reaches A, in single precision with the reference's fused steps. The second serves Any and Other.
  - Picking No Action means nothing happens. The other action is not tried.
  - With the switch off, both actions count as No Action, but the draws are still made.
- **When it plays.**
  - Unlinked: the time after the clip was launched.
  - Linked: at the end of an unlooped clip, or at the end of the multiplier's passes round the clip's loop. The first pass ends at the first loop end after the launch.
  - Times round to the reference's tick of 1/3843840 beat.
  - The launch ignores the project's launch quantization. It keeps the target clip's own quantization, or the current clip's for Stop.
- **What it plays.** A clip's group is the run of consecutive slots on its track that hold clips:
  - Empty slots split groups.
  - A deactivated clip keeps its run joined but is left out of the group.
  - Previous and Next wrap round the group. First and Last play its ends.
  - Any may pick the current clip. Other never does, unless the clip is alone in its group.
  - Play Again restarts the clip. Stop stops the track.
  - Jump plays the slot on its scene's row, in any group. An empty slot there stops the track if it has a stop button; if it has none (or holds a deactivated clip without one), nothing happens.
- **The plan.**
  - The launch an action picks waits until a quarter second before its time, when it is queued on the engine's launch handles.
  - A launch on the track, by hand or by a scene, replaces the plan. So does Stop All.
  - Editing the clip's Follow Action after it has started leaves the plan as drawn, as in the reference.
  - Stopping the transport drops every plan.
- **Scenes.**
  - A scene's action is drawn when the scene launches, from the scenes' own draws.
  - A scene's group is the run of consecutive scenes holding any clip. A scene with no clips does nothing.
  - Linked, the time is the scene's longest clip: a looped clip's multiplier × loop length (a start marker inside the loop is not subtracted, as the reference's reading shows), or an unlooped clip's length. Without clips, or unlinked, the scene's own time is used.
  - The scene picked launches on every track at exactly that time, unquantized, replacing the tracks' clip plans. Stop stops every track. The launched scene then plans its own Follow Action.
  - Stop All and another scene launch end a scene's Follow Action; clip launches do not.
- **Enable Follow Actions Globally.**
  - A switch beside Back to Arrangement, on at first. It is not saved and not undone.
  - While it is off, starting clips and scenes plan nothing. A launch already planned still plays.
  - It dims while no clip, and no scene holding clips, has an active Follow Action (switch on, with an action other than No Action).
- **The interface.**
  - The clip's Launch box (for audio and MIDI clips) and the Scene View show the same Follow Action section:
    - the switch and Linked;
    - the multiplier ("x1") while linked, else the time in bars, beats and sixteenths;
    - the A and B choosers;
    - the two chances;
    - for Jump, the scene's number, which is its row plus one.
  - Fields drag and take typing as the other number fields do. Each change is one undo step; a drag is one step.
  - Shift+Return switches the selected clips' Follow Actions on or off, all on unless all are on already (Toggle Follow Actions for Selected Clips). With the scenes' column clicked, it switches the selected scenes'.
  - An active Follow Action stripes its clip's or scene's launch button.
  - While a clip's Follow Action is on and unlinked, the note and sample editors mark its time on the ruler, after the start marker (2026-10-26). Dragging the marker sets the time, on the grid unless the grid is bypassed, in one undo step. Where it and the start marker overlap, the nearer takes the press.
- **Saving.**
  - A `HALFTRACKFOLLOWACTION` child of the clip or scene holds the settings. Settings at their defaults are left out.
  - Projects saved before 2026-10-26 kept a clip's follow actions in the engine's own form. Loading converts them: switched on, unlinked, after their beats, with their weights as chances. Next and Previous now wrap round the group, and Cycle becomes Next.
  - Invalid values reject the load and keep the open project.
- **Seeds.** The reference's seeds were not determined. Each track's draws start from a value derived from the track's identity, and the scenes' from a random value. `Session::seedFollowActions` sets them all for tests.

## Evidence

The follow-actions suite checks:
- the draws from state 0 to the bit;
- the chance rule's edge cases;
- each action's pick, wrapping, Other's skip, Jump's stop and exclusion, and doing nothing without a group;
- the chance normalisation after a load, and editing one chance;
- groups split by empty slots, with deactivated clips left out;
- the defaults and the valid ranges;
- three sequences from the research model's executable copy of the draws, including the 16-step chain from seed 12345.

With rendered audio, the same suite checks:
- every action from a clip (Next, wrapping Next and Previous, First, Last, Stop, No Action, switched off, Jump to another group, Jump to an empty slot, and Next from a clip alone in its group);
- Play Again restarting the clip at a fractional beat;
- linked timing twice round a half-beat loop;
- the chain of clips following the seed-12345 sequence;
- a plan made at a clip's start surviving an edit, and a launch by hand replacing it;
- the global switch, off from the start and switched off after a plan was made;
- a scene's Follow Action launching the next scene over a clip's own plan, linked to three times its longest clip, and ended by Stop All;
- undo, invalid settings, scene settings, a duplicated scene's clips, saving and reopening, chances loading as percentages, invalid values rejecting loads, older projects converting, Arrangement clips refused, and export protection;
- a held MIDI note releasing at a follow launch at 60, 120 and 240 BPM.

The UI suite edits a Session clip's Follow Action through the Launch box: the switch, Linked, the time, action B, both chances (rounding and the 100 % limit), Jump's scene and undo. It also checks Shift+Return and the global switch, and drags the time's marker in the note editor a beat on.

## Not yet covered

- Create Follow Action Chain (Cmd+Shift+Return, and the clip's context menu) is missing. What it sets was not read.
- How the reference displays the multiplier and the time was not read. Neither was the base of Jump's display; the reference's internal default, row 1, shows as scene 2.
- Planning differences from the reference:
  - A scene's Follow Action launch replaces the tracks' clip plans when it is queued, a quarter second early, rather than when it plays.
  - Linked timing is computed when the clip starts, so a loop changed mid-way is not counted as the reference counts loop wraps.
  - A legato launch's linked time comes from the engine's played range.
- Not determined from the reading, with Halftrack's default in brackets:
  - the engine's launch mode 5;
  - the order of a clip's and a scene's launch on the same tick;
  - whether an unlooped clip ending before its unlinked time keeps its plan (Halftrack keeps it);
  - what happens with the transport stopped (Halftrack drops plans);
  - whether the two chance writes are one undo step (Halftrack's are one).
- The Launch box edits the shown clip only, not every selected clip.
