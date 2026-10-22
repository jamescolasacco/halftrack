# Packaging, version reporting and updates

Status: partial, UX-011. Original Halftrack behavior; reference DAWs are only the subject of comparison.

## Version

One number, `project(Halftrack VERSION …)` in CMakeLists.txt, names the release everywhere:

- the macOS bundle's `CFBundleShortVersionString` and `CFBundleVersion`, and the Windows version resource (JUCE writes both from the gui app's VERSION);
- `JUCEApplication::getApplicationVersion()`;
- the `--version` line;
- the About Halftrack panel.

Every build also records the source commit it came from. `cmake/WriteBuildInfo.cmake` runs at each build and writes `generated/HalftrackBuildInfo.h`, rewriting the header only when the version or commit changes, so an unchanged tree rebuilds nothing.

The commit takes one of three forms:

- the 7-character short hash;
- that hash followed by `-modified` when tracked files had uncommitted changes;
- `unknown` without Git history. A source-archive build can name its commit with `-DHALFTRACK_COMMIT=…`.

`Halftrack --version` prints one line and quits without opening audio devices or a window, for example `Halftrack 0.1.0 (ad1f541, macOS arm64)`. The line names the version, the commit, and the operating system with the architecture of the running process.

On macOS, About Halftrack sits in the application menu, as in other Mac applications. On Windows and Linux it sits in a Help menu. The panel shows:

- the mark and the version line, in read-only selectable text so it can be pasted into a report;
- the license: AGPL-3.0-only for Halftrack, AGPLv3 for JUCE 8 and GPLv3 for Tracktion Engine;
- where settings and the User Library live.

## Packages

`scripts/package_preview.py` packages a Release build of a clean tree. On macOS and Linux it writes `Halftrack-preview-<system>-<machine>-<commit>.tar.gz`; on Windows it writes a `.zip`. The package holds:

- the application;
- Halftrack's license and the dependency licenses;
- `SOURCE.json` with the exact commit;
- the corresponding source of Halftrack, JUCE and Tracktion;
- a short `START HERE.txt`.

`scripts/verify_package.py` installs a package into a fresh temporary folder (a clean install), checks what was installed, then deletes the folder (the uninstall). It checks that:

- every required file is present;
- the packaged source's CMake version equals the bundle's Info.plist versions, on macOS;
- the bundle identifier is `org.halftrack.studio`;
- the installed application's `--version` names the same version, the commit SOURCE.json records (unmodified), and this operating system;
- the packaged Halftrack source equals the commit file for file, when the repository holds that commit.

It writes the findings, with the archive's size and SHA-256, to a JSON report. CI packages and verifies on macOS, Windows and Linux after the native suites. `tests/test_verify_package.py` checks the verifier against synthetic installs: one that agrees passes, and each disagreement is reported. The disagreements are a missing license, a stale bundle version, another commit, a modified build, another system, and an unreadable version line.

## Install and uninstall

Halftrack has no installer. Installing means extracting the package and keeping `Halftrack.app`, `Halftrack.exe` or `Halftrack` anywhere. Uninstalling means deleting it. The application writes nothing inside its own folder.

Per-user data lives outside the application folder and survives an uninstall or an upgrade. This is how reference DAWs treat preferences and the user library. The locations are:

- settings, preferences, plug-in scan results, recovery files and temporary files, in the user application-data folder's `Halftrack` folder: `~/Library/Halftrack` on macOS, `%APPDATA%\Halftrack` on Windows, `~/.config/Halftrack` on Linux;
- the User Library, in the user's music folder: `Music/Halftrack/User Library`.

Deleting these resets Halftrack completely. The About panel names the settings folder.

## Updates and signing

Halftrack does not check for updates and does not contact the network. JUCE is built with `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0`, with no update service. To update, install a newer package in place of the old one. Settings and the User Library carry over, and projects keep the versioned `<HALFTRACK version="1">` format ([architecture](../architecture.md)).

The macOS build is ad-hoc signed by the linker and is not notarized. Gatekeeper asks before the first launch of a downloaded copy. The Windows build is unsigned. Developer ID signing, notarization and Windows Authenticode are recorded as future work in docs/open-questions.md; they need accounts this project does not hold.

## Not covered

- An installer or package-manager formats: `.dmg` or `.pkg`, MSI, `.deb` or Flatpak.
- Signed or notarized builds.
- Automatic updates.
- Uninstall cleanup of per-user data.
- Independent reproducible builds.

The macOS check runs in CI and locally. The Windows and Linux checks run on the CI runners only.
