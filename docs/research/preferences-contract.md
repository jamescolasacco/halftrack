# Preferences contract

Halftrack's preferences follow part of the reference's. They live in a versioned file beside the engine's settings and are edited in the Preferences window (the application menu on a Mac, Options elsewhere, Cmd/Ctrl+comma).

## The window (2026-10-24)

A research agent read the reference's Preferences window statically (Live 12.2.1, arm64 slice sha256 b37cb738…; findings in the ignored `artifacts/reverse-engineering/prefs/`). Seven of its nine pages are layouts in an encrypted resource, so only the Audio and Link, Tempo & MIDI pages have exact rows; the others give their sections and the defaults of the settings behind them.

- **A normal window, one at a time.** The reference shows Preferences as an ordinary window, never modal; opening it again brings the open one to the front. Halftrack's does the same.
- **Tabs.** The reference's tabs run down the left (141 pixels of a 556 by 589 window): Display & Input, Theme & Colors, Audio, Link, Tempo & MIDI, File & Folder, Library, Plug-Ins, Record, Warp & Launch, and Licenses & Updates. Halftrack shows those holding settings it has, in that order, and leaves out File & Folder, Library and Licenses & Updates.
- **The tab is remembered.** The reference keeps the chosen tab with its preferences (first open: Display & Input). Halftrack keeps it as `preferencesTab`, by the reference's tab ids (`audio`, `midi`, `Plugins`...), without marking the project changed (`Session::setPreferencesTab`). Its links open a tab: the control bar's audio button and Audio To's Configure... the Audio tab, the device chain's Manage the Plug-Ins tab.
- **Pages.**
  - Display & Input: Zoom Display (50 to 200%, default 100, as the reference's) and Follow Behavior.
  - Theme & Colors: Halftrack's own themes. The reference's has a default theme with Mode (Follow System, Light, Dark), Tone (Cool, Neutral, Warm) and a contrast switch.
  - Audio, in the reference's order: Driver Type, the input and output devices (one Audio Device for a driver without separate ones), Channel Configuration (Input Config and Output Config), In/Out Sample Rate, then Latency: Buffer Size, Input Latency, Output Latency, Driver Error Compensation and Overall Latency. The reference's sample rate conversion quality and test tone are left out.
  - Link, Tempo & MIDI: its MIDI section. A control surface, with its input and output; the reference's table has six, Halftrack one (Mackie Control). Then the ports: each input's Sync and MPE, each output's Track and Sync, as the reference's columns. Link, the tempo follower, takeover and the Remote switches are left out.
  - Plug-Ins: the plug-in sources and Rescan.
  - Record, Warp & Launch, in the reference's sections: Record (File Type, Bit Depth, Count-In, Exclusive Arm and Solo, Start Recording on Scene Launch, Start Playback with Record), Warp and Fades (Auto-Warp Long Samples, Default Warp Mode, Create Fades on Clip Edges) and Launch (Default Launch Mode and Quantization). Which page each of these sits on in the reference is inferred from its settings' names.
  - Reset to Defaults, Halftrack's own, sits under the tabs.
- **While Preferences has the focus**, the reference greys its transport commands; Halftrack does not yet.

## Settings

| Setting | Default | Effect |
| --- | --- | --- |
| Exclusive Arm, Exclusive Solo | on | A plain click releases every other armed or soloed track (and, for solo, group), and Cmd/Ctrl-click adds. Off, the two swap. |
| Default Warp Mode | Complex | The mode Session audio clips are warped in, and the mode Auto-Warp uses. Off is refused. |
| Auto-Warp Long Samples | off | Arrangement imports longer than 30 seconds are warped at their estimated tempo. |
| Default Launch Mode, Default Launch Quantization | Trigger, Global | What new Session clips (imported, created or recorded) launch with. |
| Create Fades on Clip Edges | on | New audio clips, and the edges a split makes, get 4 ms default fades, and touching default edges crossfade ([clip fades contract](clip-fades-contract.md)). |
| Start Playback with Record | on | Record pressed while stopped starts playback too; off, it waits for Play or a Session launch, and Shift reverses it ([transport contract](transport-contract.md)). |
| Start Recording on Scene Launch | off | Launching a scene also records into its empty slots on armed tracks, as Session Record does ([Session recording contract](session-recording-contract.md#start-recording-on-scene-launch)). |
| Plug-In Sources | Audio Units and the VST3 system folders on, the custom VST3 folder off | What Rescan scans, set in Plug-Ins Settings ([plug-in sources contract](plugin-sources-contract.md)). |
| Count-In | None | Bars recording waits for: None, 1, 2 or 4 ([metronome contract](metronome-contract.md#count-in)). |
| Metronome Sound | Classic | The metronome's ticks: Classic, Click or Wood ([metronome contract](metronome-contract.md)). |
| Theme, Zoom Display | Dark, 100% | The interface's theme (Dark, Light or High Contrast) and scale, 50-200% in steps of 25 ([themes contract](themes-contract.md)). |
| Lock Envelopes | off | Options > Lock Envelopes: automation stays where it is when clips move. The reference keeps it with its song preference data (static reading: `ASongPrefData::mpLockEnvelope`, off), not in Sets ([automation contract](arrangement-automation-contract.md#automation-follows-clips-and-lock-envelopes-2026-10-24)). |

Two lists are kept beside the settings: the browser's user folders ([Places](browser-places-contract.md)) and File > Open Recent Project's projects ([menu contract](main-menu-contract.md)). Reset to Defaults keeps them.

Preferences belong to the application, as the reference's do: changing one, from the window, a menu or a list, leaves the open project's unsaved state as it was.

An import that the preferences warp is still one undo step.

## The file

- **Saving.** Preferences are written atomically with a `version` attribute (currently 1).
- **Missing file.** The defaults apply.
- **Older file.** Its settings are kept, and settings it lacks get their defaults.
- **Newer file.** Its unknown settings are ignored.
- **Unreadable file.** A file that cannot be read, or that holds an out-of-range value, is moved aside as "Halftrack Preferences (unreadable).xml" unchanged. The defaults apply, and the app reports the reason when it starts.
- **Reset.** Reset to Defaults writes the defaults, keeping the user folders and recent projects.

## Evidence

`tests/PreferencesTests.cpp` checks:

- **The defaults** with no file, and a change saved with its version and read by the next Session; Off is refused as the warp mode.
- **New Session clips:** an imported Session audio clip warps in Beats and launches as Gate on the bar, the import is one undo step, and a new Session MIDI clip launches as Gate.
- **Auto-Warp:** a 40-second sample warps in Beats at its estimated tempo while a 10-second one does not.
- **Reset** persists.
- **Other files:**
  - an older file keeps its one setting and gains the defaults;
  - Follow Behavior Scroll, Start Recording on Scene Launch and Start Playback with Record off are read back;
  - a newer file's unknown setting is ignored;
  - an out-of-range value, and a file of junk, are set aside with the defaults kept and the reason given.

The UI suite opens the window: a normal window, not modal, with the tabs in the reference's order; a link opening the Audio tab, which it remembers; one window when opened again; and the tab chosen last when reopened. On the Audio page it finds the driver type, device, sample rate and buffer size, their rows in the reference's order, and sets the buffer size; on Link, Tempo & MIDI the control surface, and on a virtual port the ports' switches. Every preference fits on its page. It switches Exclusive Solo off and sees a plain click add a second soloed track. It also finds Start Recording on Scene Launch off and switches it on, and Start Playback with Record on and switches it off, each keeping the other settings, and then resets.

## Not yet covered

- The reference's other preferences (its theme model, File & Folder, Library, Licenses, the plug-in window options, and the rest of Record, Warp & Launch) are missing.
- Two defaults differ from the reference's: it auto-warps long samples and warps in Beats by default, and never offers Complex Pro as the default warp mode.
- Migration between platforms is untested on Windows and Linux machines.
