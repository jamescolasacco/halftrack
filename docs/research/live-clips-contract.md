# Live Clips contract

Halftrack's Live Clips follow the reference's, as its manual describes them ([Managing Files and Sets](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/), read 2026-10-26): a clip saved to disk with its clip and envelope settings and its track's devices, for reuse in any project.

## Behaviour

- **Saving.** Dragging an Arrangement or Session clip onto the browser saves it as a Live Clip:
  - into the folder a place shows (the User Library, the Current Project or an added folder), or otherwise into the User Library;
  - named for the clip, numbered beside a file of the same name, so nothing is replaced.
  - The clip keeps its settings, loop, notes and envelopes (`Session::saveLiveClip`). The file holds its track's devices and mixer, as Save as Default Track keeps them.
  - An audio clip's sample is copied beside the file into a Samples folder, unless the same audio is there already. The file names it by its path from the clip's folder.
  - Grouped clips and audio clips with several takes are refused. The file is written beside its place and moved over it.
- **The browser.** Places list Live Clip files (`.halftrackclip`) with the sounds and projects. A Live Clip drags out like a sound.
- **Loading** (`Session::loadLiveClip`, `loadLiveClipToSlot`), as a dropped sound lands:
  - in the Arrangement at the beat under the pointer, snapped as a sound's drop is, or into the Session slot under it, replacing a clip there;
  - where no track is (below the Arrangement's tracks or in the Session's drop area), on a new track made from the saved track, with its devices and mixer;
  - onto a track with neither clips nor devices, its devices join the track, in place of the built-in synth when they hold an instrument;
  - onto any other track, the clip comes alone. Its envelopes for the saved track's mixer move to the track's own; those for devices the track lacks are left out.
  - MIDI clips go onto MIDI tracks and audio clips onto audio tracks. The sample is collected into the project.
  - A Session clip keeps its launch settings; an Arrangement clip in a slot takes the defaults. A MIDI clip keeps its length and content start in beats; a warped audio clip keeps its length in beats.
  - It is one undo step, and a damaged file is refused before anything changes.

## Tracks from other projects (2026-10-26)

As the manual describes, a project in the browser unfolds like a folder to show its tracks, and its tracks come into the open project whole.

- **Listing.** A project file's triangle unfolds its tracks, read from the file without opening it: groups, then what is in them one level deeper (`Session::listProjectTracks`). Returns are not listed.
- **Importing** (`Session::importProjectTracks`): dragging a track in, or pressing Return on it, brings it with its clips, devices and automation; dragging the project brings its top-level tracks and groups. A group comes with everything in it.
  - They land before the track or Session column under the pointer, or after the other tracks where no track is (as Paste places tracks); Return puts a track after the others.
  - The project is read with every check opening it makes, and a damaged one is refused before anything changes.
  - Media stays where it is until the next save collects it into this project, as any outside sample does.
  - Returns are not imported, and sends to returns are left out. It is one undo step.
- Opening a project from the browser still loads it.

## Evidence

The presets suite:
- saves a MIDI clip with a Saturator envelope and a volume envelope, and reads it back as a four-beat MIDI clip with two devices;
- loads it where no track is (a new track with the devices, the clip at beat 8, its envelope on the new Saturator), and undoes that in one step;
- loads it into an empty MIDI track's slot (the devices joining it, both envelopes kept);
- loads it onto its own track, which has devices (the clip alone, keeping its volume envelope on the track's mixer);
- refuses it on an audio track;
- saves an audio clip, its sample copied into Samples, and loads it, its sample collected into the project;
- refuses a damaged file, changing nothing.

The track-duplicate suite lists another project's tracks (its MIDI track at the top, its group followed by the audio track in it, and its return), imports its MIDI track whole (its clip and note, its Saturator and its volume automation) and undoes it, imports the whole project (the group with its audio track and audio, no return), imports a track before this project's first, and refuses a damaged project, changing nothing.

The UI suite drags a clip onto the browser showing the User Library, finds the file saved and listed there, dragging as a Live Clip, and the clip unmoved. It drops it on the track's lane at its second bar, and in the Session's drop area, where it lands in a slot of a new track. It also finds a project in the User Library folded, unfolds it with its triangle to a Keys track one level deeper that drags as that project's track, drops it on a track, where it comes in before it, and folds it again.

## Not yet covered

- Previewing a Live Clip from the browser, and hot-swapping one.
- Dragging an unfolded project's clips in, importing returns with their sends, and exporting Session clips as a new project by dragging them to the browser.
- The reference's Collect Files on Export choices: Halftrack always copies the sample beside the file.
- Defaults where the manual is silent are in the [open questions](../open-questions.md#live-clips).
