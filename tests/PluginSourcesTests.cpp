#include "Session.h"
#include <iostream>
#include <stdexcept>

// Plug-In Sources and Rescan, after the reference's Plug-Ins settings: the sources' defaults and
// persistence, and a background rescan of a custom VST3 folder holding a copy of the generated
// fixture. A rescan adds the plug-in once, keeps it once, drops it when its file goes or its
// source is switched off, and Rescan with everything forgets the list first.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS plug-in sources: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
}

int runPluginSourcesTests (const juce::File& artifacts)
{
    try
    {
        const auto* configured = std::getenv ("HALFTRACK_TEST_PLUGIN");
        check (configured != nullptr && juce::File (configured).isDirectory(), "the generated VST3 fixture is supplied");
        const juce::File built (configured);
        const auto root = artifacts.getChildFile ("plugin-sources-" + juce::Uuid().toString());
        const auto folder = root.getChildFile ("Custom VST3");
        ok (folder.createDirectory(), "create a custom plug-in folder");
        const auto copy = folder.getChildFile (built.getFileName());
        const auto install = [&] { check (copy.isDirectory() || built.copyDirectoryTo (copy), "put the fixture in the custom folder"); };
        install();

        Session s (false);
        const Session::Preferences defaults;
        check (defaults.useAudioUnits && defaults.useVst3SystemFolders && ! defaults.useVst3CustomFolder && defaults.vst3CustomFolder.isEmpty(),
               "by default Audio Units and the VST3 system folders are used and the custom folder is not, as in the reference");
        auto p = s.preferences();
        p.useAudioUnits = false; p.useVst3SystemFolders = false; p.useVst3CustomFolder = true; p.vst3CustomFolder = folder.getFullPathName();
        ok (s.setPreferences (p), "use only the custom VST3 folder");
        {
            Session reopened (false);
            check (reopened.preferences() == p, "the plug-in sources are saved with the preferences");
        }
        s.knownPlugins().clear();
        const auto finish = [&]
        {
            for (int i = 0; i < 6000 && s.pluginScanStatus().scanning; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            return ! s.pluginScanStatus().scanning;
        };
        const auto inFolder = [&]
        {
            int n = 0;
            for (const auto& type : s.knownPlugins().getTypes()) if (juce::File (type.fileOrIdentifier).isAChildOf (folder)) ++n;
            return n;
        };
        const auto total = [&] { return s.knownPlugins().getNumTypes(); };

        check (! s.pluginScanStatus().scanning, "no scan runs at first");
        ok (s.rescanPlugins(), "Rescan");
        check (s.pluginScanStatus().scanning && s.rescanPlugins().failed(), "a scan runs in the background, and a second is refused meanwhile");
        check (finish(), "the scan finishes");
        check (inFolder() == 1 && total() == 1 && s.knownPlugins().getTypes()[0].pluginFormatName == "VST3", "the custom folder's plug-in is listed, and nothing else");
        ok (s.rescanPlugins(), "Rescan again"); check (finish(), "it finishes");
        check (inFolder() == 1 && total() == 1, "an unchanged plug-in is listed once");

        check (copy.deleteRecursively(), "remove the plug-in's file");
        ok (s.rescanPlugins(), "Rescan"); check (finish(), "it finishes");
        check (total() == 0, "a plug-in whose file has gone leaves the list");

        install();
        ok (s.rescanPlugins(), "put it back and Rescan"); check (finish(), "it finishes");
        check (inFolder() == 1, "it is listed again");
        p.useVst3CustomFolder = false; ok (s.setPreferences (p), "switch the custom folder off");
        ok (s.rescanPlugins(), "Rescan"); check (finish(), "it finishes");
        check (total() == 0, "the plug-ins of a source switched off leave the list");

        p.useVst3CustomFolder = true; ok (s.setPreferences (p), "switch it on again");
        ok (s.rescanPlugins (true), "Rescan everything"); check (finish(), "it finishes");
        check (inFolder() == 1 && total() == 1, "a full rescan lists the plug-in afresh");

        {
            // A Session closed during a scan waits for it rather than leaving it running.
            Session closing (false);
            closing.knownPlugins().clear();
            ok (closing.rescanPlugins (true), "start a scan in another Session");
        }
        check (true, "closing a Session during a scan returns");
        p = defaults; ok (s.setPreferences (p), "restore the default sources");
        root.deleteRecursively();
        std::cout << "Plug-in sources passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL plug-in sources: " << e.what() << std::endl; return 1; }
}
}
