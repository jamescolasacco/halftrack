#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace halftrack
{
// The sample browser's index of a folder and everything under it, built on a worker thread so the
// browser can search a large library while it is still being read. The worker holds only
// standard-library data: a scan blocked inside the filesystem can outlive the browser that started
// it without touching its memory. Hidden entries are skipped and links are not followed, so a link
// loop cannot recurse; a folder that cannot be read is counted and skipped. Entries arrive in
// batches, so searches made during indexing see what has been read so far.
class BrowserIndex
{
public:
    // A folder or audio file, by its UTF-8 path, its name, and its name in lower case for search.
    struct Entry
    {
        std::string path, name, folded;
        bool directory = false;
    };
    struct Status
    {
        bool indexing = false, truncated = false;
        std::size_t entries = 0, unreadableFolders = 0;
        std::string error; // Why the root itself could not be read, if it could not.
    };
    static constexpr std::size_t maximumEntries = 250000, maximumDepth = 64;
    // Scans that may still be running, including cancelled ones blocked in the filesystem.
    static constexpr int maximumScans = 4;

    BrowserIndex() = default;
    ~BrowserIndex();
    BrowserIndex (const BrowserIndex&) = delete;
    BrowserIndex& operator= (const BrowserIndex&) = delete;

    // Starts indexing a folder, keeping at most limit entries, and cancelling the index in progress.
    // Returns false, leaving no index, while too many earlier scans are still blocked.
    bool start (const std::string& rootUtf8, std::size_t limit = maximumEntries);
    void cancel();
    const std::string& root() const noexcept { return rootPath; }
    Status status() const;
    // Entries whose names contain every space-separated word of the query, ignoring ASCII case,
    // sorted by name and then path; at most limit of them. An empty query matches nothing.
    std::vector<Entry> search (const std::string& query, std::size_t limit) const;
    // Whether a name is one of the audio file types the browser lists.
    static bool isAudioName (const std::string& name);

private:
    struct Scan;
    std::shared_ptr<Scan> scan;
    std::string rootPath;
};
}
