# Browser Places contract

Status: partial. This follows the reference's manual and static menu research. No reference output was measured.

## Evidence

- Live 12 manual, [Working with the Browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), consulted 2026-10-24:
  - The sidebar has three sections in this order: Collections, Library and Places.
  - Places lists Packs, the User Library, the Current Project and the user folders, with Add Folder... last. Splice, Cloud and Push sit there too when their services are on.
  - A folder joins Places through Add Folder, or by being dragged into Places from Finder or Explorer. Live then scans it, with a spinning wheel beside the Places label until the scan is done.
  - A folder that has moved, been renamed or is on a disconnected drive shows greyed. Its context menu offers Locate Folder, and Remove from Sidebar takes any folder out of Places without deleting it.
  - The Current Project shows the files of the open project; an unsaved project's files are in a temporary location. Its context menu offers Show in Finder (Show in Explorer on Windows).
  - The User Library holds saved presets, defaults, clips, grooves and samples.
- The reference's File menu builder, read statically for the [menu bar contract](main-menu-contract.md), adds Add Folder to Browser (`af2b`, no shortcut) after Install Pack, in the same group.
- Symbol names in the reference binary (12.2.1, arm64, read with `strings`) include a Locate command for user folders, a sidebar delegate's `AddFolders` and `OnDrop`, and a user-folder model. These agree with the manual; their behaviour was not read further.

## Halftrack's behaviour

- **Sidebar.** The browser's sidebar shows COLLECTIONS, then LIBRARY (Instruments, Audio Effects, MIDI Effects, Plug-Ins, Samples, Grooves), then PLACES: Packs, User Library, Current Project, each user folder in the order added, and Add Folder.... It scrolls when its rows are taller than the browser.
- **Browsing a place.** Clicking a place lists its folder as Samples lists a folder: folders, audio files, which open, preview, import and take collections as they do there, and projects, which open ([backups contract](backups-contract.md#halftracks-behaviour)). The title row names the place at its top. Up and Left go back up a folder but no higher than the place. A search looks through the place and every folder in it, from the [browser index](browser-index-contract.md). Samples keeps its own folder: leaving a place for another row returns Samples to the folder it was left at.
- **User Library.** The User Library's own folder.
- **Current Project.** The project's media folder: `<name> Assets` beside a saved project, or the unsaved project's temporary one. Its Backup folder holds the project's [backups](backups-contract.md). It is listed again after a recording, a save or another project. Before the project has any media it says so. Its menu has Show in Finder (Show in Explorer elsewhere).
- **Adding a folder.** File > Add Folder to Browser... and the sidebar's Add Folder... choose a folder. Folders dropped on the browser are added too. A folder joins Places last and opens; a folder already there just opens. Places are kept with the preferences, as absolute paths, and a preferences reset keeps them.
- **Missing folders.** A folder that is not where it was shows greyed. Its menu offers Locate Folder..., which points the place at the folder chosen, and Remove from Sidebar.
- **Removing a folder.** Remove from Sidebar takes the folder out of Places and leaves it on disk. If it was showing, the browser returns to Samples.
- **Show in Browser.** A clip's or slot's Show in Browser opens the place that holds its file, looking in the Current Project first, then the user folders and the User Library. Any other file opens in Samples, as before. Either way the folder is listed afresh and the file selected.
- **Scanning.** While a place's folder is being indexed, a wheel turns beside the PLACES label.

## Not yet

- Places are indexed when opened, not all in the background as the reference scans every added folder.
- Splice, Cloud and Push.
- Unfolding folders in place, as the reference's content pane does; Halftrack's browser opens a folder in place of the list.
- Dragging a folder from the browser's list into Places, and reordering or renaming places.
- The reference's Current Project also lists the project's Live Sets and saved presets; Halftrack lists its media.
- Watching folders for changes.

See [open questions](../open-questions.md) for the defaults taken where the manual is silent.

## Evidence in Halftrack

`tests/PreferencesTests.cpp` reads user folders back in order, a missing one too, refuses a relative or repeated folder, keeps them through a reset, and finds the added folder in the next Session. `tests/PersistenceTests.cpp` checks the project's media folder before and after a save and through Save a Copy.

The UI suite checks:
- the sidebar's order: Packs, the User Library, the Current Project, then Add Folder, after the Library;
- the User Library listing its folders without Choose Folder, a folder in it opening, Left returning to the place and no further, and Samples returning to its own folder afterwards;
- that the File menu offers Add Folder to Browser after Install Pack;
- that adding a folder keeps it in the preferences and opens it, named for its folder, and that a search looks through its folders;
- that adding a folder already in Places opens it without adding it again, and that clicking a place opens it;
- a moved folder greyed, its menu offering Locate Folder and Remove from Sidebar, Locate Folder following it, and removal leaving it on disk;
- a folder dropped on the browser joining Places;
- the Current Project listing the project's media folder, and Show in Browser finding a project file there, selected, with Show in Finder in its menu.
