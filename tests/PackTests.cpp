#include "Packs.h"
#include <juce_cryptography/juce_cryptography.h>
#include <iostream>
#include <stdexcept>

// Content packs, built and installed by the test in a folder of its own: installing and listing,
// requirements at their versions, refusing to put an older version over a newer one, carrying the
// user's files through an upgrade and keeping them on uninstall, and refusing damaged and unsafe
// archives without touching what was installed.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS packs: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
void put (const juce::File& file, const juce::String& text)
{ if (! file.getParentDirectory().createDirectory().wasOk() || ! file.replaceWithText (text)) throw std::runtime_error ("could not write " + file.getFullPathName().toStdString()); }
juce::File pack (const juce::File& root, const juce::String& id, const juce::String& version, std::vector<packs::Requirement> requirements = {}, const juce::String& kick = "kick")
{
    const auto contents = root.getChildFile ("build-" + id + "-" + version);
    contents.deleteRecursively();
    put (contents.getChildFile ("Samples/Kick.wav"), kick + " " + version);
    put (contents.getChildFile ("Presets/Warm.adv"), "preset " + version);
    packs::Manifest fields; fields.id = id; fields.name = id.fromLastOccurrenceOf (".", false, false); fields.version = version; fields.author = "Halftrack Tests";
    fields.requirements = std::move (requirements);
    const auto archive = root.getChildFile (id + "-" + version + ".halftrackpack");
    ok (packs::create (contents, fields, archive), "build " + id + " " + version);
    return archive;
}
juce::String versionOf (const juce::File& packsFolder, const juce::String& id)
{
    for (const auto& p : packs::installed (packsFolder)) if (p.manifest.id == id) return p.manifest.version;
    return {};
}
}

int runPackTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("packs-" + juce::Uuid().toString());
        const auto library = root.getChildFile ("User Library"), folder = library.getChildFile ("Packs");
        check (packs::compareVersions ("1.10", "1.9") > 0 && packs::compareVersions ("2", "2.0.0") == 0 && packs::compareVersions ("1.2", "1.2.1") < 0, "versions compare by their numbers");

        // Installing and requirements.
        const auto core10 = pack (root, "org.halftrack.core", "1.0"), core11 = pack (root, "org.halftrack.core", "1.1");
        const auto drums = pack (root, "org.halftrack.drums", "2.0", { { "org.halftrack.core", "1.1" } });
        const auto needsCore = packs::install (drums, folder);
        check (needsCore.failed() && needsCore.getErrorMessage().contains ("org.halftrack.core 1.1") && packs::installed (folder).empty(), "a pack whose requirement is missing is refused (" + needsCore.getErrorMessage() + ")");
        ok (packs::install (core10, folder), "install core 1.0");
        check (versionOf (folder, "org.halftrack.core") == "1.0" && folder.getChildFile ("org.halftrack.core/Samples/Kick.wav").loadFileAsString() == "kick 1.0", "it is listed and its files are in place");
        const auto tooOld = packs::install (drums, folder);
        check (tooOld.failed() && tooOld.getErrorMessage().contains ("1.0 is installed"), "a requirement at too old a version is refused");
        ok (packs::install (core11, folder), "upgrade core to 1.1");
        ok (packs::install (drums, folder), "drums installs now");
        check (packs::installed (folder).size() == 2, "two packs are installed");
        juce::File kept;
        const auto needed = packs::uninstall ("org.halftrack.core", folder, kept);
        check (needed.failed() && needed.getErrorMessage().contains ("needs") && versionOf (folder, "org.halftrack.core") == "1.1", "a pack another needs cannot be uninstalled");
        const auto older = packs::install (core10, folder);
        check (older.failed() && older.getErrorMessage().contains ("newer") && versionOf (folder, "org.halftrack.core") == "1.1", "an older version is not put over a newer one");

        // The user's files through an upgrade and an uninstall.
        const auto core = folder.getChildFile ("org.halftrack.core");
        put (core.getChildFile ("Samples/My Take.wav"), "mine");
        put (core.getChildFile ("Presets/Warm.adv"), "changed by me");
        ok (packs::install (pack (root, "org.halftrack.core", "1.2"), folder), "upgrade core to 1.2");
        check (core.getChildFile ("Samples/My Take.wav").loadFileAsString() == "mine", "a file the user added is carried over");
        check (core.getChildFile ("Presets/Warm.adv").loadFileAsString() == "preset 1.2", "the pack's own file is the new version's");
        check (library.getChildFile ("Kept from core/Presets/Warm.adv").loadFileAsString() == "changed by me", "and the user's changed copy is kept beside the packs");
        ok (packs::uninstall ("org.halftrack.drums", folder, kept), "uninstall drums");
        check (kept == juce::File() && ! folder.getChildFile ("org.halftrack.drums").exists(), "it is removed, with nothing of the user's to keep");
        ok (packs::uninstall ("org.halftrack.core", folder, kept), "uninstall core");
        check (! core.exists() && kept == library.getChildFile ("Kept from core") && kept.getChildFile ("Samples/My Take.wav").loadFileAsString() == "mine",
               "uninstalling keeps the user's file rather than deleting it");
        check (packs::installed (folder).empty(), "nothing is installed");

        // Damaged and unsafe archives leave what was installed as it was.
        ok (packs::install (core10, folder), "install core 1.0 again");
        const auto contents = root.getChildFile ("damaged");
        put (contents.getChildFile ("Samples/Kick.wav"), "kick");
        packs::Manifest fields; fields.id = "org.halftrack.core"; fields.name = "core"; fields.version = "3.0";
        fields.files = { { "Samples/Kick.wav", juce::String::repeatedString ("0", 64) } };
        const auto writeArchive = [&] (const juce::File& archive, const juce::String& json, std::vector<std::pair<juce::String, juce::String>> entries)
        {
            juce::ZipFile::Builder builder;
            builder.addEntry (new juce::MemoryInputStream (juce::MemoryBlock (json.toRawUTF8(), json.getNumBytesAsUTF8()), true), 9, "pack.json", juce::Time::getCurrentTime());
            for (const auto& [name, text] : entries)
                builder.addEntry (new juce::MemoryInputStream (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()), true), 9, name, juce::Time::getCurrentTime());
            juce::FileOutputStream out (archive); out.setPosition (0); out.truncate();
            check (out.openedOk() && builder.writeToStream (out, nullptr), "write " + archive.getFileName());
        };
        const auto damaged = root.getChildFile ("damaged.halftrackpack");
        writeArchive (damaged, packs::toJson (fields), { { "Samples/Kick.wav", "kick" } });
        const auto broken = packs::install (damaged, folder);
        check (broken.failed() && broken.getErrorMessage().contains ("damaged"), "a file that is not the one its manifest names is refused");
        check (versionOf (folder, "org.halftrack.core") == "1.0" && core.getChildFile ("Samples/Kick.wav").loadFileAsString() == "kick 1.0", "and the installed version is untouched");
        const auto extra = root.getChildFile ("extra.halftrackpack");
        fields.files = { { "Samples/Kick.wav", juce::SHA256 (juce::MemoryBlock ("kick", 4)).toHexString() } };
        writeArchive (extra, packs::toJson (fields), { { "Samples/Kick.wav", "kick" }, { "Samples/Hidden.wav", "sneaky" } });
        check (packs::install (extra, folder).getErrorMessage().contains ("does not list"), "a file the manifest does not list is refused");
        auto escaping = packs::toJson (fields).replace ("Samples/Kick.wav", "../Escaped.wav");
        writeArchive (extra, escaping, { { "../Escaped.wav", "kick" } });
        const auto outside = packs::install (extra, folder);
        check (outside.failed() && outside.getErrorMessage().contains ("outside") && ! library.getChildFile ("Escaped.wav").exists(), "a path leaving the pack is refused");
        for (const auto* path : { "/abs.wav", "a/../b.wav", "C:\\\\x.wav", "a//b.wav", "./a.wav" })
        {
            packs::Manifest m;
            check (packs::parse (packs::toJson (fields).replace ("Samples/Kick.wav", path), m).failed(), juce::String ("the manifest path ") + path + " is refused");
        }
        const auto notZip = root.getChildFile ("notes.halftrackpack"); put (notZip, "not a zip");
        check (packs::install (notZip, folder).failed(), "a file that is not a pack is refused");
        check (folder.findChildFiles (juce::File::findDirectories, false).size() == 1, "no unpacked leftovers remain beside the installed pack");
        std::cout << "Packs passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL packs: " << e.what() << std::endl; return 1; }
}
}
