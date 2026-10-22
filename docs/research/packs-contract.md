# Packs and content management

Status: partial, PRJ-016. Halftrack's own pack format. The reference distributes content in packs that install into its library and browse from its sidebar; Halftrack follows that shape, not its format.

## The format

A pack is a zip archive (`.halftrackpack`) holding `pack.json` and the pack's files. The manifest has these fields:

- `format` (1);
- `id`: letters, digits, dots and dashes, such as `org.example.drums`;
- `name`;
- `version`: dotted numbers, compared number by number, so 1.10 is newer than 1.9;
- an optional `author`;
- `requires`: other packs by id and minimum version;
- every file with its SHA-256.

A file path must stay inside the pack. It has to be relative, use forward slashes and contain no empty, `.` or `..` parts, no drive, no root and none of `\ : * ? " < > |`. Each path may appear once.

`packs::create` builds a pack from a folder of files, computing each file's hash.

## Installing

`packs::install` installs into `<User Library>/Packs/<id>`. It refuses, with the reason, in these cases:

- an archive that is not a pack;
- a manifest that does not parse;
- a file the manifest does not list, or a listed file that is missing;
- a file whose SHA-256 is not the one listed (a damaged pack);
- a requirement that is not installed, or installed at an older version than required;
- an older version over a newer one, unless asked to downgrade.

Files are unpacked and checked in a hidden staging folder beside the packs. That folder then replaces any installed version in one move, and the old version is restored if the move fails. A refused install therefore leaves what was installed as it was, and leaves nothing behind.

Upgrading carries over the user's files:

- a file the user added to the installed version is copied into the new one;
- a pack file the user changed is kept in `<User Library>/Kept from <name>/`, because the new version replaces it;
- a user file where the new version now has a file of its own is kept there too.

## Uninstalling

`packs::uninstall` refuses while another installed pack requires the pack. Otherwise it removes the pack's folder. Before that, every file not in the manifest, or changed since it was installed, moves to `Kept from <name>/` at its relative path, never over an existing file. If a file cannot be kept, the pack is not removed.

## The UI

- The File menu has Install Pack…, enabled while a User Library is set.
- The browser's sidebar has a Packs category listing installed packs with their version and author.
- Opening a pack browses its files in the sample browser.
- A pack's right-click menu has Uninstall Pack, which reports where any kept files went.

## Evidence

`tests/PackTests.cpp` builds packs in a folder of its own and checks:

- version comparison;
- a missing requirement, and one at too old a version, refused; upgrading the requirement lets the dependent pack install;
- uninstalling a required pack refused, and an older version over a newer one refused;
- an upgrade carrying a user's added file, replacing a pack file and keeping the user's changed copy;
- uninstalls with and without user files;
- a damaged file and an unlisted file refused, with the installed version untouched;
- a path leaving the pack refused, with nothing written outside;
- five unsafe manifest paths refused;
- a non-zip refused;
- no leftover staging folders.

The UI suite installs a pack through the browser's code path, lists it with its version and author, opens it, and uninstalls it.

## Not yet covered

- Signed packs and a download or store.
- Installing several packs or resolving requirements automatically.
- Presets inside packs appearing in the device browser.
- Disk-space checks before unpacking.
- The reference's own `.alp` packs, which Halftrack does not read.
