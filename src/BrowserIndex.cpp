#include "BrowserIndex.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>
#include <utility>

namespace halftrack
{
namespace
{
std::string folded (std::string text)
{
    std::transform (text.begin(), text.end(), text.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return text;
}
std::string utf8 (const std::filesystem::path& path)
{
    const auto bytes = path.u8string();
    return { reinterpret_cast<const char*> (bytes.data()), bytes.size() };
}
std::filesystem::path fromUtf8 (const std::string& text)
{
    return std::filesystem::path (std::u8string (reinterpret_cast<const char8_t*> (text.data()), text.size()));
}
std::atomic<int> runningScans { 0 };
}

struct BrowserIndex::Scan
{
    std::atomic<bool> cancelled { false }, finished { false };
    mutable std::mutex mutex;
    std::vector<Entry> entries;
    std::size_t unreadable = 0;
    bool truncated = false;
    std::string error;
};

BrowserIndex::~BrowserIndex() { cancel(); }

bool BrowserIndex::isAudioName (const std::string& name)
{
    const auto dot = name.find_last_of ('.');
    if (dot == std::string::npos) return false;
    const auto extension = folded (name.substr (dot));
    return extension == ".wav" || extension == ".wave" || extension == ".aif" || extension == ".aiff"
        || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
}

void BrowserIndex::cancel()
{
    if (scan) scan->cancelled.store (true);
    scan.reset();
}

bool BrowserIndex::start (const std::string& rootUtf8, std::size_t limit)
{
    cancel();
    rootPath = rootUtf8;
    if (runningScans.fetch_add (1) >= maximumScans) { runningScans.fetch_sub (1); return false; }
    auto next = std::make_shared<Scan>();
    try
    {
        std::thread ([next, rootUtf8, limit]
        {
            // Depth-first over folders, publishing entries in batches.
            std::vector<std::pair<std::filesystem::path, std::size_t>> folders { { fromUtf8 (rootUtf8), 0 } };
            std::vector<Entry> batch;
            std::size_t total = 0;
            const auto publish = [&]
            {
                const std::lock_guard<std::mutex> lock (next->mutex);
                for (auto& entry : batch) next->entries.push_back (std::move (entry));
                batch.clear();
            };
            bool first = true;
            while (! folders.empty() && ! next->cancelled.load())
            {
                auto [folder, depth] = std::move (folders.back());
                folders.pop_back();
                std::error_code ec;
                std::filesystem::directory_iterator iterator (folder, ec), end;
                if (ec)
                {
                    const std::lock_guard<std::mutex> lock (next->mutex);
                    if (first) next->error = ec.message(); else ++next->unreadable;
                    first = false; continue;
                }
                first = false;
                while (iterator != end && ! next->cancelled.load())
                {
                    const auto name = utf8 (iterator->path().filename());
                    if (! name.empty() && name.front() != '.')
                    {
                        std::error_code typeError;
                        const auto link = iterator->is_symlink (typeError);
                        const auto directory = ! typeError && ! link && iterator->is_directory (typeError);
                        if (! typeError && (directory || isAudioName (name)))
                        {
                            if (total >= limit)
                            {
                                const std::lock_guard<std::mutex> lock (next->mutex);
                                next->truncated = true; folders.clear(); break;
                            }
                            batch.push_back ({ utf8 (iterator->path()), name, folded (name), directory });
                            ++total;
                            if (directory && depth + 1 < maximumDepth) folders.push_back ({ iterator->path(), depth + 1 });
                            if (batch.size() >= 512) publish();
                        }
                    }
                    iterator.increment (ec);
                    if (ec) { const std::lock_guard<std::mutex> lock (next->mutex); ++next->unreadable; break; }
                }
            }
            publish();
            next->finished.store (true);
            runningScans.fetch_sub (1);
        }).detach();
    }
    catch (const std::exception& e)
    {
        runningScans.fetch_sub (1);
        next->error = e.what(); next->finished.store (true);
    }
    scan = std::move (next);
    return true;
}

BrowserIndex::Status BrowserIndex::status() const
{
    Status result;
    if (! scan) return result;
    const std::lock_guard<std::mutex> lock (scan->mutex);
    result.indexing = ! scan->finished.load();
    result.entries = scan->entries.size();
    result.unreadableFolders = scan->unreadable;
    result.truncated = scan->truncated;
    result.error = scan->error;
    return result;
}

std::vector<BrowserIndex::Entry> BrowserIndex::search (const std::string& query, std::size_t limit) const
{
    std::vector<std::string> words;
    for (std::size_t start = 0; start < query.size();)
    {
        const auto space = query.find (' ', start);
        const auto word = query.substr (start, space == std::string::npos ? std::string::npos : space - start);
        if (! word.empty()) words.push_back (folded (word));
        if (space == std::string::npos) break;
        start = space + 1;
    }
    std::vector<Entry> found;
    if (words.empty() || ! scan) return found;
    {
        const std::lock_guard<std::mutex> lock (scan->mutex);
        for (const auto& entry : scan->entries)
            if (std::all_of (words.begin(), words.end(), [&] (const auto& w) { return entry.folded.find (w) != std::string::npos; }))
                found.push_back (entry);
    }
    std::sort (found.begin(), found.end(), [] (const Entry& a, const Entry& b) { return a.folded != b.folded ? a.folded < b.folded : a.path < b.path; });
    if (found.size() > limit) found.resize (limit);
    return found;
}
}
