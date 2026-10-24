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
- **Browsing a place.** Clicking a place lists its folder as Samples lists a folder: folders, audio files, which open, preview, import and take collections as they do there, and projects, which open ([backups contract](backups-contract.md#halftracks-behaviour)). The title row names the place. Its folders open in place, as the [folder tree](#the-folder-tree-2026-10-24) below describes, and Left at the top goes no higher than the place. A search looks through the place and every folder in it, from the [browser index](browser-index-contract.md). Samples keeps its own folder: leaving a place for another row returns Samples to the folder it was left at.
- **User Library.** The User Library's own folder.
- **Current Project.** The project's media folder: `<name> Assets` beside a saved project, or the unsaved project's temporary one. Its Backup folder holds the project's [backups](backups-contract.md). It is listed again after a recording, a save or another project. Before the project has any media it says so. Its menu has Show in Finder (Show in Explorer elsewhere).
- **Adding a folder.** File > Add Folder to Browser... and the sidebar's Add Folder... choose a folder. Folders dropped on the browser are added too. A folder joins Places last and opens; a folder already there just opens. Places are kept with the preferences, as absolute paths, and a preferences reset keeps them.
- **Missing folders.** A folder that is not where it was shows greyed. Its menu offers Locate Folder..., which points the place at the folder chosen, and Remove from Sidebar.
- **Removing a folder.** Remove from Sidebar takes the folder out of Places and leaves it on disk. If it was showing, the browser returns to Samples.
- **Show in Browser.** A clip's or slot's Show in Browser opens the place that holds its file, looking in the Current Project first, then the user folders and the User Library. Any other file opens in Samples, as before. Either way the folder is listed afresh and the file selected.
- **Scanning.** While a place's folder is being indexed, a wheel turns beside the PLACES label.

## The folder tree (2026-10-24)

The reference's manual: Pack folders "can be unfolded to reveal the included Pack contents", and Left and Right "close and open folders"; holding Cmd while opening a folder keeps the others open, which otherwise close ([Working with the Browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), sections 4.7.1 and 4.8, consulted 2026-10-24). Halftrack's file lists, in Samples and the places, do the same:
- **Opening.** A folder opens in place, its items listed under it, indented, with a triangle showing whether it is open. A click on the triangle, a double-click, Return or Right opens it. Opening a folder closes the others outside it, unless Cmd is held. Each open folder is read on a worker of its own, as the listed folder is.
- **Keys.** Right on an open folder goes to its first item, and on a sound previews it. Left closes an open folder, and on an item goes to the folder holding it. At the top, Left moves out to the sidebar, as the manual's arrows "move between the sidebar and content pane": there Up and Down show the row above or below, through the collections, the Library and Places (Add Folder left to clicks, a missing User Library skipped), and Right or Return move back to the list, its first row selected. The Up button goes up a folder in Samples, the folder left showing open in its parent.
- **Search.** Results are a flat list, as before. Opening a folder found by a search clears it and shows the folder open in the tree.
- **Show in Browser.** The folders down to the file open, with the file selected.

## History (2026-10-24)

The reference's manual: the arrow buttons left of the search bar, Browse Back and Browse Forward, go back and forward through the browser's previous searches and navigation, also with Cmd+[ and Cmd+] ([Working with the Browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), section 4.3, consulted 2026-10-24). Halftrack's browser has the two arrows and keys. A state is the category, collection or place shown, its listed and open folders, and the search. One joins the history when the browser leaves it for another category or folder or settles on one, and a search when Return is pressed or the field is left. Stepping back and forward restores a state; a new state drops the ones ahead, and the history keeps the last 100.

## Not yet

- Places are indexed when opened, not all in the background as the reference scans every added folder.
- Splice, Cloud and Push.
- Dragging a folder from the browser's list into Places, and reordering or renaming places.
- The reference's Current Project also lists the project's Live Sets and saved presets; Halftrack lists its media.
- Watching folders for changes.

See [open questions](../open-questions.md) for the defaults taken where the manual is silent.

## Evidence in Halftrack

`tests/PreferencesTests.cpp` reads user folders back in order, a missing one too, refuses a relative or repeated folder, keeps them through a reset, and finds the added folder in the next Session. `tests/PersistenceTests.cpp` checks the project's media folder before and after a save and through Save a Copy.

The UI suite checks:
- Browse Back left of the search bar, offered after moving about; Back returning to a search, Cmd+[ going back before it and to an earlier category, Cmd+] going forward, and a new category dropping the states ahead;
- the folder tree: Right opening a folder in place with its items indented under it, Right going into it, Left going back and closing it, opening another closing the first unless Cmd is held, a double-click closing a folder, Left at the top moving out rather than up, and Up going up a folder with the one left showing open;
- the sidebar's Down and Up showing the rows below and above, running on from the Library into Places, and Right moving to the list with its first row selected;
- the sidebar's order: Packs, the User Library, the Current Project, then Add Folder, after the Library;
- the User Library listing its folders without Choose Folder, a folder in it opening in place, Left going to it, closing it and going no higher than the place, and Samples returning to its own folder afterwards;
- that the File menu offers Add Folder to Browser after Install Pack;
- that adding a folder keeps it in the preferences and opens it, named for its folder, and that a search looks through its folders;
- that adding a folder already in Places opens it without adding it again, and that clicking a place opens it;
- a moved folder greyed, its menu offering Locate Folder and Remove from Sidebar, Locate Folder following it, and removal leaving it on disk;
- a folder dropped on the browser joining Places;
- the Current Project listing the project's media folder, and Show in Browser finding a project file there, selected, with Show in Finder in its menu.
