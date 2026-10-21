#include "BrowserIndex.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
 #include <unistd.h>
#endif

// The browser index over a generated library: folders and audio files under the root are found at
// any depth, while hidden entries, other file types and linked folders are not; duplicate names in
// different folders are kept apart; searches match every word in any case; an unreadable folder
// is counted and skipped, and a missing root reports why; a limit truncates the index; restarting
// on another folder drops the first; and a moved file is found where it went after reindexing.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS browser index: " << text << std::endl; }
void touch (const juce::File& file) { if (! file.getParentDirectory().createDirectory().wasOk() || ! file.replaceWithText ({})) throw std::runtime_error ("could not create a fixture"); }
std::string path (const juce::File& file) { return file.getFullPathName().toStdString(); }
BrowserIndex::Status finish (BrowserIndex& index)
{
    for (int i = 0; i < 3000 && index.status().indexing; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (10));
    const auto status = index.status();
    if (status.indexing) throw std::runtime_error ("indexing did not finish within 30 s");
    return status;
}
}

int runBrowserIndexTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("browser-index-" + juce::Uuid().toString()).getChildFile ("Library");
        // 40 folders of 50 samples, a sixth of them in a nested folder; a Kick in five folders; one
        // sample ten folders down; and entries that are not listed.
        size_t folders = 0, audio = 0;
        for (int f = 0; f < 40; ++f)
        {
            const auto folder = root.getChildFile ("Folder " + juce::String (f)); ++folders;
            for (int n = 0; n < 50; ++n)
            {
                const auto nested = n % 6 == 0;
                touch ((nested ? folder.getChildFile ("Nested") : folder).getChildFile ("Sample " + juce::String (n) + (n % 3 == 0 ? ".aif" : ".wav"))); ++audio;
            }
            ++folders; // Nested.
            if (f % 8 == 0) { touch (folder.getChildFile ("Kick.wav")); ++audio; }
            touch (folder.getChildFile ("notes.txt"));
            touch (folder.getChildFile (".hidden.wav"));
        }
        auto deep = root;
        for (int d = 0; d < 10; ++d) { deep = deep.getChildFile ("Level " + juce::String (d)); ++folders; }
        touch (deep.getChildFile ("Deep Snare.FLAC")); ++audio;
        touch (root.getChildFile (".git").getChildFile ("ignored.wav"));
        std::error_code linked;
        std::filesystem::create_directory_symlink (std::filesystem::path (path (root)), std::filesystem::path (path (root.getChildFile ("Loop"))), linked);
        const bool posix = ! linked;

        BrowserIndex index;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        check (index.start (path (root)), "indexing starts");
        check (juce::Time::getMillisecondCounterHiRes() - started < 200, "without waiting for the scan");
        auto status = finish (index);
        check (status.entries == folders + audio && status.error.empty() && status.unreadableFolders == 0 && ! status.truncated,
               juce::String ((int) status.entries) + " entries: every folder and audio file at any depth, nothing hidden, other types or linked folders");
        const auto kicks = index.search ("kick", 100);
        check (kicks.size() == 5 && std::all_of (kicks.begin(), kicks.end(), [] (const auto& e) { return e.name == "Kick.wav" && ! e.directory; }), "five Kicks in five folders");
        check (std::is_sorted (kicks.begin(), kicks.end(), [] (const auto& a, const auto& b) { return a.path < b.path; }) && kicks.front().path != kicks.back().path,
               "duplicate names are kept apart by their paths");
        const auto deepSnare = index.search ("SNARE deep", 10);
        check (deepSnare.size() == 1 && deepSnare[0].name == "Deep Snare.FLAC" && juce::File (deepSnare[0].path).existsAsFile(), "every word matches in any case, ten folders down");
        check (index.search ("Nested", 100).size() == 40 && index.search ("", 10).empty() && index.search ("sample", 7).size() == 7, "folders match too; an empty query finds nothing; a limit caps results");
        check (index.search ("ignored", 10).empty() && index.search ("notes", 10).empty() && index.search ("hidden", 10).empty(), "hidden entries and other files are not indexed");

        // A file moved to another folder is found there once reindexed.
        const auto moved = root.getChildFile ("Moved").getChildFile ("Kick.wav");
        check (moved.getParentDirectory().createDirectory().wasOk() && juce::File (kicks.front().path).moveFileTo (moved), "move a Kick");
        check (index.start (path (root)), "reindex");
        finish (index);
        const auto afterMove = index.search ("kick", 100);
        check (afterMove.size() == 5 && std::any_of (afterMove.begin(), afterMove.end(), [&] (const auto& e) { return e.path == path (moved); })
                   && std::none_of (afterMove.begin(), afterMove.end(), [&] (const auto& e) { return e.path == kicks.front().path; }), "the moved Kick is found where it went");

        // An unreadable folder is counted and skipped.
       #if ! JUCE_WINDOWS
        if (posix)
        {
            const auto locked = root.getChildFile ("Folder 1");
            check (locked.setReadOnly (true, false), "lock a folder");
            ::chmod (path (locked).c_str(), 0);
            const bool effective = ! locked.getChildFile ("notes.txt").existsAsFile() || ::getuid() != 0;
            check (index.start (path (root)), "index with a locked folder");
            status = finish (index);
            ::chmod (path (locked).c_str(), 0755);
            if (effective)
                check (status.unreadableFolders == 1 && status.entries == folders + audio + 1 - 51 && status.error.empty(),
                       "the locked folder is counted unreadable and its contents skipped (" + juce::String ((int) status.unreadableFolders) + ")");
        }
       #endif
        // A missing root reports why; a limit truncates; a restart drops the previous index.
        check (index.start (path (root.getSiblingFile ("Missing"))), "index a missing folder");
        status = finish (index);
        check (! status.error.empty() && status.entries == 0, "a missing folder reports why (" + juce::String (status.error) + ")");
        check (index.start (path (root), 100), "index with a limit of 100");
        status = finish (index);
        check (status.truncated && status.entries == 100, "the index stops at its limit and says so");
        check (index.start (path (root)) && index.start (path (root.getChildFile ("Folder 2"))), "restart on one folder while the library is indexing");
        status = finish (index);
        check (status.entries == 51 && index.search ("kick", 10).empty() && index.root() == path (root.getChildFile ("Folder 2")),
               "only the new folder's entries remain");
        {
            BrowserIndex discarded;
            check (discarded.start (path (root)), "start an index that is destroyed while it runs");
        }
        for (int i = 0; i < 300; ++i)
        {
            BrowserIndex probe;
            if (probe.start (path (root.getChildFile ("Folder 3")))) { check (true, "cancelled scans end and free their slots"); break; }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            if (i == 299) check (false, "cancelled scans end and free their slots");
        }
        std::cout << "Browser index passed: " << root.getFullPathName() << std::endl; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL browser index: " << e.what() << std::endl; return 1; }
}
}
