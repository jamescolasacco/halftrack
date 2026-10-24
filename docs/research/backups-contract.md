# Project backups contract

Status: partial. This follows the reference's manual and static menu research. No reference output was measured.

## Evidence

- Live 12 manual, [Working with the Browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), section 4.7.7, consulted 2026-10-24: once a project has been saved at least twice, its Current Project shows a Backup folder holding its last ten saved versions, marked with timestamps. An unsaved Set has none.
- The reference's File menu builder, read statically for the [menu bar contract](main-menu-contract.md), adds a Backups submenu after Manage Projects. Its builder (0x1016d307c, Live 12.2.1 arm64) adds one entry per backup, each with open, enabled and name callbacks, then a separator only when there are entries, then Show in Browser (`bubr`, enabled by its own callback) and Reveal in Finder (`bueb`; Show in Explorer on Windows).

## Halftrack's behaviour

- **When.** Saving over a project file first keeps the version it replaces: Save, Save As onto an existing project, and Save a Copy onto one. A first save keeps nothing. The backup is made only once the new version is written in full, and the save goes ahead even if the backup cannot be written.
- **Where and how named.** `<name> [YYYY-MM-DD HHMMSS].halftrack`, stamped in local time with when the replaced version was saved, in the `Backup` folder of the project's Assets. A version with the same stamp is kept once.
- **Media.** A backup's media paths are rewritten relative to its folder, so it opens with the project's audio and samples.
- **How many.** The ten newest backups of a project are kept. Older ones are deleted. Other files in the folder, including other projects' backups, are left alone.
- **File > Backups.** The open project's backups, the newest first, each named by its file. When there are any, a separator, then Show in Browser and Reveal in Finder (Show in Explorer elsewhere), both greyed until there is a backup. Choosing a backup opens it, asking first to save changes.
- **In the browser.** The file browser lists projects beside audio, with their own icon, and a double-click or Return opens one, asking first to save changes. Show in Browser opens the Current Project's Backup folder with the newest backup selected.

## Not yet

- The browser's search finds audio only, not projects.
- Collections cannot tag projects, and projects cannot be dragged in from the browser.
- File Manager's management of backups.

See [open questions](../open-questions.md) for the defaults taken where the reference was not read.

## Evidence in Halftrack

`tests/PersistenceTests.cpp` checks:
- that a first save keeps no backup, and the next keeps the replaced version, named for when it was saved;
- that the backup opens as the earlier version with its audio found and nothing missing;
- that twelve more saves keep the ten newest, newest first, leaving other files in the folder alone;
- that a copy saved over a project keeps that project's version in its own Backup folder.

The UI suite checks the File > Backups submenu before and after a backup, Show in Browser selecting the newest backup in the Current Project, and a backup opening from the browser and from the menu.
