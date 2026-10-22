#include "Packs.h"
#include <juce_cryptography/juce_cryptography.h>
#include <map>
#include <set>

namespace halftrack::packs
{
namespace
{
bool validId (const juce::String& id) { return id.isNotEmpty() && id.length() <= 128 && id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") && ! id.startsWith ("."); }
bool validVersion (const juce::String& v)
{
    if (v.isEmpty() || v.length() > 32 || ! v.containsOnly ("0123456789.") || v.startsWith (".") || v.endsWith (".") || v.contains ("..")) return false;
    return true;
}
// A path that stays inside the pack: relative, forward slashes, no empty, "." or ".." parts.
bool safePath (const juce::String& path)
{
    if (path.isEmpty() || path.length() > 1024 || path.containsAnyOf ("\\:*?\"<>|") || path.startsWithChar ('/')) return false;
    for (const auto& part : juce::StringArray::fromTokens (path, "/", ""))
        if (part.isEmpty() || part == "." || part == ".." || part.trim() != part) return false;
    return path != "pack.json";
}
juce::String hashOf (const juce::File& file)
{
    juce::FileInputStream in (file);
    return in.openedOk() ? juce::SHA256 (in).toHexString() : juce::String();
}
juce::File packFolder (const juce::File& packsFolder, const juce::String& id) { return packsFolder.getChildFile (id); }
// Moves a file to the same relative place under another folder, never over a file already there.
bool keep (const juce::File& file, const juce::File& root, const juce::File& into)
{
    auto target = into.getChildFile (file.getRelativePathFrom (root).replaceCharacter ('\\', '/'));
    if (! target.getParentDirectory().createDirectory().wasOk()) return false;
    if (target.exists()) target = target.getNonexistentSibling (true);
    return file.moveFileTo (target);
}
}

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto x = juce::StringArray::fromTokens (a, ".", ""), y = juce::StringArray::fromTokens (b, ".", "");
    for (int i = 0; i < std::max (x.size(), y.size()); ++i)
    {
        const auto p = x[i].getLargeIntValue(), q = y[i].getLargeIntValue();
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

juce::Result parse (const juce::String& json, Manifest& out)
{
    juce::var root;
    if (juce::JSON::parse (json, root).failed() || ! root.isObject()) return juce::Result::fail ("The pack's manifest is not valid JSON.");
    if (! root["format"].isInt() || (int) root["format"] != Manifest::format) return juce::Result::fail ("The pack is in a format this version of Halftrack does not read.");
    Manifest m;
    m.id = root["id"].toString(); m.name = root["name"].toString().trim(); m.version = root["version"].toString(); m.author = root["author"].toString().trim();
    if (! validId (m.id)) return juce::Result::fail ("The pack's id is missing or not made of letters, digits, dots and dashes.");
    if (m.name.isEmpty() || m.name.length() > 128) return juce::Result::fail ("The pack has no name.");
    if (! validVersion (m.version)) return juce::Result::fail ("The pack's version is not a dotted number.");
    if (const auto* list = root["requires"].getArray())
        for (const auto& r : *list)
        {
            Requirement req { r["id"].toString(), r["version"].toString() };
            if (! validId (req.id) || ! validVersion (req.version) || req.id == m.id) return juce::Result::fail ("The pack names a requirement it cannot have.");
            m.requirements.push_back (req);
        }
    const auto* files = root["files"].getArray();
    if (files == nullptr || files->isEmpty()) return juce::Result::fail ("The pack lists no files.");
    std::set<juce::String> seen;
    for (const auto& f : *files)
    {
        Manifest::Entry e { f["path"].toString(), f["sha256"].toString().toLowerCase() };
        if (! safePath (e.path)) return juce::Result::fail ("The pack names a file outside itself: " + e.path);
        if (e.sha256.length() != 64 || ! e.sha256.containsOnly ("0123456789abcdef")) return juce::Result::fail ("The pack's file " + e.path + " has no valid SHA-256.");
        if (! seen.insert (e.path.toLowerCase()).second) return juce::Result::fail ("The pack names " + e.path + " twice.");
        m.files.push_back (e);
    }
    out = std::move (m);
    return juce::Result::ok();
}

juce::String toJson (const Manifest& m)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("format", Manifest::format);
    root->setProperty ("id", m.id); root->setProperty ("name", m.name); root->setProperty ("version", m.version);
    if (m.author.isNotEmpty()) root->setProperty ("author", m.author);
    juce::Array<juce::var> requirements, files;
    for (const auto& r : m.requirements) { juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("id", r.id); o->setProperty ("version", r.version); requirements.add (juce::var (o.get())); }
    for (const auto& f : m.files) { juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("path", f.path); o->setProperty ("sha256", f.sha256); files.add (juce::var (o.get())); }
    if (! requirements.isEmpty()) root->setProperty ("requires", requirements);
    root->setProperty ("files", files);
    return juce::JSON::toString (juce::var (root.get()));
}

std::vector<Installed> installed (const juce::File& packsFolder)
{
    std::vector<Installed> result;
    for (const auto& folder : packsFolder.findChildFiles (juce::File::findDirectories, false))
    {
        Manifest m;
        if (folder.getFileName().startsWithChar ('.')) continue;
        if (parse (folder.getChildFile ("pack.json").loadFileAsString(), m).wasOk() && m.id == folder.getFileName()) result.push_back ({ m, folder });
    }
    std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.manifest.name.compareNatural (b.manifest.name) < 0; });
    return result;
}

juce::Result install (const juce::File& archive, const juce::File& packsFolder, bool allowOlder)
{
    juce::ZipFile zip (archive);
    if (zip.getNumEntries() == 0) return juce::Result::fail ("This is not a Halftrack pack.");
    const auto* manifestEntry = zip.getEntry ("pack.json");
    if (manifestEntry == nullptr) return juce::Result::fail ("The pack has no manifest.");
    Manifest m;
    {
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*manifestEntry));
        if (in == nullptr) return juce::Result::fail ("The pack's manifest cannot be read.");
        if (auto r = parse (in->readEntireStreamAsString(), m); r.failed()) return r;
    }
    // Every entry is the manifest, a folder or a listed file, and every listed file is there.
    std::map<juce::String, const juce::ZipFile::ZipEntry*> entries;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry (i);
        const auto name = entry->filename;
        if (name == "pack.json" || name.endsWithChar ('/')) continue;
        entries[name] = entry;
    }
    std::set<juce::String> listed;
    for (const auto& f : m.files) listed.insert (f.path);
    for (const auto& [name, entry] : entries)
        if (listed.count (name) == 0) return juce::Result::fail ("The pack holds a file its manifest does not list: " + name);
    for (const auto& f : m.files)
        if (entries.count (f.path) == 0) return juce::Result::fail ("The pack is missing " + f.path + ".");
    const auto present = installed (packsFolder);
    for (const auto& req : m.requirements)
    {
        const auto found = std::find_if (present.begin(), present.end(), [&] (const auto& p) { return p.manifest.id == req.id; });
        if (found == present.end()) return juce::Result::fail ("\"" + m.name + "\" needs the pack " + req.id + " " + req.version + " or newer. Install it first.");
        if (compareVersions (found->manifest.version, req.version) < 0)
            return juce::Result::fail ("\"" + m.name + "\" needs " + found->manifest.name + " " + req.version + " or newer; " + found->manifest.version + " is installed.");
    }
    const auto existing = std::find_if (present.begin(), present.end(), [&] (const auto& p) { return p.manifest.id == m.id; });
    if (existing != present.end() && compareVersions (m.version, existing->manifest.version) < 0 && ! allowOlder)
        return juce::Result::fail ("\"" + m.name + "\" " + existing->manifest.version + " is installed, newer than " + m.version + ".");

    if (auto r = packsFolder.createDirectory(); r.failed()) return r;
    const auto staging = packsFolder.getChildFile ("." + m.id + ".installing-" + juce::Uuid().toString());
    const auto fail = [&staging] (const juce::String& why) { staging.deleteRecursively(); return juce::Result::fail (why); };
    for (const auto& f : m.files)
    {
        const auto target = staging.getChildFile (f.path);
        if (! target.getParentDirectory().createDirectory().wasOk()) return fail ("The pack could not be unpacked into " + packsFolder.getFullPathName() + ".");
        {
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*entries[f.path]));
            juce::FileOutputStream out (target);
            if (in == nullptr || ! out.openedOk() || out.writeFromInputStream (*in, -1) < 0) return fail ("The pack's file " + f.path + " could not be unpacked.");
        }
        if (hashOf (target) != f.sha256) return fail ("The pack is damaged: " + f.path + " is not the file its manifest names.");
    }
    if (! staging.getChildFile ("pack.json").replaceWithText (toJson (m))) return fail ("The pack's manifest could not be written.");
    // Files the user added to the installed version carry over; ones the new version also names
    // stay as the pack's, with the user's copy kept beside the packs.
    const auto destination = packFolder (packsFolder, m.id);
    if (existing != present.end())
    {
        std::map<juce::String, juce::String> oldListed;
        for (const auto& f : existing->manifest.files) oldListed[f.path] = f.sha256;
        const auto kept = packsFolder.getSiblingFile ("Kept from " + m.name);
        for (const auto& file : destination.findChildFiles (juce::File::findFiles, true))
        {
            const auto relative = file.getRelativePathFrom (destination).replaceCharacter ('\\', '/');
            if (relative == "pack.json") continue;
            const auto old = oldListed.find (relative);
            // The pack's own file as installed: the new version replaces it.
            if (old != oldListed.end() && hashOf (file) == old->second) continue;
            // The user's file, or a pack file they changed: carried over where the new version has
            // nothing of its own there, kept aside where it has.
            if (old != oldListed.end() || listed.count (relative) > 0) { if (! keep (file, destination, kept)) return fail ("Your file " + relative + " could not be kept."); }
            else { const auto target = staging.getChildFile (relative); target.getParentDirectory().createDirectory(); file.copyFileTo (target); }
        }
    }
    // Swap in the new version in one move, restoring the old one if the move fails.
    const auto aside = packsFolder.getChildFile ("." + m.id + ".replaced-" + juce::Uuid().toString());
    if (destination.exists() && ! destination.moveFileTo (aside)) return fail ("The installed version of \"" + m.name + "\" could not be replaced.");
    if (! staging.moveFileTo (destination))
    {
        if (aside.exists()) aside.moveFileTo (destination);
        return fail ("\"" + m.name + "\" could not be installed into " + packsFolder.getFullPathName() + ".");
    }
    aside.deleteRecursively();
    return juce::Result::ok();
}

juce::Result uninstall (const juce::String& id, const juce::File& packsFolder, juce::File& keptIn)
{
    keptIn = juce::File();
    const auto present = installed (packsFolder);
    const auto pack = std::find_if (present.begin(), present.end(), [&] (const auto& p) { return p.manifest.id == id; });
    if (pack == present.end()) return juce::Result::fail ("That pack is not installed.");
    for (const auto& other : present)
        for (const auto& req : other.manifest.requirements)
            if (req.id == id) return juce::Result::fail ("\"" + other.manifest.name + "\" needs \"" + pack->manifest.name + "\". Uninstall it first.");
    // The user's own files, or the pack's files they changed, are kept rather than deleted.
    std::map<juce::String, juce::String> listed;
    for (const auto& f : pack->manifest.files) listed[f.path] = f.sha256;
    const auto kept = packsFolder.getSiblingFile ("Kept from " + pack->manifest.name);
    for (const auto& file : pack->folder.findChildFiles (juce::File::findFiles, true))
    {
        const auto relative = file.getRelativePathFrom (pack->folder).replaceCharacter ('\\', '/');
        if (relative == "pack.json") continue;
        const auto known = listed.find (relative);
        if (known == listed.end() || hashOf (file) != known->second)
        {
            if (! keep (file, pack->folder, kept)) return juce::Result::fail ("Your file " + relative + " could not be kept, so the pack was not removed.");
            keptIn = kept;
        }
    }
    if (! pack->folder.deleteRecursively()) return juce::Result::fail ("\"" + pack->manifest.name + "\" could not be removed.");
    return juce::Result::ok();
}

juce::Result create (const juce::File& contents, Manifest fields, const juce::File& archive)
{
    fields.files.clear();
    juce::ZipFile::Builder builder;
    auto files = contents.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, true);
    files.sort();
    for (const auto& file : files)
    {
        const auto relative = file.getRelativePathFrom (contents).replaceCharacter ('\\', '/');
        if (relative == "pack.json") continue;
        fields.files.push_back ({ relative, hashOf (file) });
        builder.addFile (file, 9, relative);
    }
    Manifest checked;
    if (auto r = parse (toJson (fields), checked); r.failed()) return r;
    // The builder keeps the stream until it writes; the manifest's bytes go in as a memory stream.
    const auto json = toJson (fields);
    builder.addEntry (new juce::MemoryInputStream (juce::MemoryBlock (json.toRawUTF8(), json.getNumBytesAsUTF8()), true), 9, "pack.json", juce::Time::getCurrentTime());
    juce::TemporaryFile temporary (archive);
    {
        juce::FileOutputStream out (temporary.getFile());
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr)) return juce::Result::fail ("The pack could not be written.");
    }
    return temporary.overwriteTargetFileWithTemporary() ? juce::Result::ok() : juce::Result::fail ("The pack could not be written to " + archive.getFullPathName() + ".");
}
}
