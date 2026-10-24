# Plug-in sources contract

Halftrack's Plug-Ins settings follow the reference's, on Preferences' Plug-Ins page (or Manage in the device chain). The reference's manual, consulted 2026-10-22, describes:

- the Plug-In Sources section;
- "Use Audio Units";
- a VST custom folder with its switch and a Browse button;
- Rescan, which with Alt/Option held "will delete your plug-in database altogether" before scanning afresh;
- a scan at each launch, skipped while Alt/Option is held.

Its labels read "Use Audio Units v2" and "Use VST2 Plug-In System Folders". Halftrack hosts VST3 and Audio Units v2, so it shows the VST3 rows in the same wording.

## Behaviour

- **Sources** (`Session::Preferences`). These are kept with the preferences:
  - `useAudioUnits`, on (macOS only);
  - `useVst3SystemFolders`, on;
  - `useVst3CustomFolder`, off;
  - `vst3CustomFolder`.
- **Rescan** (`Session::rescanPlugins (everything)`). It scans the enabled sources on a thread of its own. Each file goes through the plug-in list's scanner, which uses the separate scanner process wherever the engine has one, so a crashing plug-in cannot take the app down.
  - New and changed plug-ins are added, and an unchanged plug-in is not described again.
  - Plug-ins whose file has gone, that lie outside the enabled VST3 folders, or whose source is switched off are dropped.
  - With `everything`, the list's VST3 and Audio Unit plug-ins and its blacklist are forgotten first.
  - A second rescan is refused while one runs.
- **Status.** `pluginScanStatus` reports whether a scan runs, its progress and the file being scanned. The list is finished on the message thread.
- **Stopping.** `cancelPluginScan` stops a scan, and closing a Session waits for its scan to stop.
- **The panel.**
  - Toggles for the sources, the custom folder with Browse, Rescan (Alt/Option-click for everything), and a status line ("Scanning … (n%)", or how many plug-ins there are).
  - Changing a source, or choosing a folder, saves it and rescans, once any scan running has stopped.
  - It replaces the generic plug-in list dialog.
- **At launch.** The app rescans its sources each time it starts, unless Alt (Option on a Mac) is held, as the reference's manual describes. Test sessions never scan by themselves.

## Evidence

`tests/PluginSourcesTests.cpp` (`plugin-sources-acceptance`, given the generated VST3 fixture) checks:

- the default sources, and that they are saved;
- a background rescan of a custom folder holding a copy of the fixture, with a second rescan refused meanwhile;
- the plug-in listed once, and still once after another rescan;
- dropped when its file is removed, and listed again when it is put back;
- dropped when the custom folder is switched off;
- listed afresh by a full rescan;
- that closing a Session during a scan returns.

The UI suite checks:

- the panel's labels and state;
- that Rescan finds the test plug-in in its folder and shows the count;
- that switching the custom folder off drops its plug-ins and switching it on finds them again.

The launch-time scan, and Audio Units and system folders on a real machine, are not automated, since they would scan the tester's own plug-ins.
