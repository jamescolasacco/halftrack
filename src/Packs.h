#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace halftrack::packs
{
// Halftrack's own content packs: a zip holding pack.json and the pack's files. The manifest names
// the pack (id, name, version, author), the packs it needs and every file with its SHA-256:
//   { "format": 1, "id": "org.example.drums", "name": "Drums", "version": "1.2.0", "author": "...",
//     "requires": [ { "id": "org.example.core", "version": "1.0.0" } ],
//     "files": [ { "path": "Samples/Kick.wav", "sha256": "..." } ] }
// Versions compare by their dotted numbers. A pack installs into <packs folder>/<id>.
struct Requirement { juce::String id, version; };
struct Manifest
{
    static constexpr int format = 1;
    juce::String id, name, version, author;
    std::vector<Requirement> requirements;
    struct Entry { juce::String path, sha256; };
    std::vector<Entry> files;
};
// Reads and checks a manifest: an id of letters, digits, dots and dashes; a dotted version; file
// paths that stay inside the pack (relative, no "..", no drive or root) and are named once.
juce::Result parse (const juce::String& json, Manifest&);
juce::String toJson (const Manifest&);
// -1, 0 or 1 as a is older than, equal to or newer than b.
int compareVersions (const juce::String& a, const juce::String& b);

struct Installed { Manifest manifest; juce::File folder; };
// The packs installed in a folder, by id.
std::vector<Installed> installed (const juce::File& packsFolder);

// Installs a pack archive. It refuses a damaged or unsafe archive (a file missing, altered or not
// in the manifest, or a path leaving the pack), a pack whose requirements are not installed at
// their versions, and an older version over a newer one unless allowed. Files are unpacked and
// checked in a temporary folder beside the pack's, which then replaces any installed version in
// one move; a refused install leaves what was installed as it was. Files the user added to an
// installed version are carried over.
juce::Result install (const juce::File& archive, const juce::File& packsFolder, bool allowOlder = false);
// Removes an installed pack, refusing while another installed pack requires it. Files the user
// added to the pack's folder (not in its manifest, or changed since) move to
// <packs folder>/../Kept from <name>/ rather than being deleted. Returns where they went, if any.
juce::Result uninstall (const juce::String& id, const juce::File& packsFolder, juce::File& keptIn);

// Writes a pack archive from a folder of files: every file under it, with a manifest made from
// the given fields. For tests and for packing one's own content.
juce::Result create (const juce::File& contents, Manifest fields, const juce::File& archive);
}
