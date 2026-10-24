# Browser index contract

The reference's browser indexes the user's folders so that a search finds files anywhere under them while the index is still being built. Halftrack's Samples browser indexes its folder and every folder in it, and its search looks through that index.

## Indexing

- **When it runs.** Choosing a folder, or moving up, starts indexing it on a worker thread and cancels any index still running. Folders opened in place in the list are inside it already.
- **The worker.** It holds only standard-library data. A scan blocked inside the filesystem can therefore outlive the browser without touching its memory. At most four scans may be outstanding, cancelled ones included; beyond that the browser asks the user to try again shortly.
- **What is indexed.** Folders and audio files (WAV, AIFF, FLAC, Ogg, MP3) at any depth, up to 64 levels. Hidden entries (names starting with `.`) are skipped. Linked folders are not followed, so a link loop cannot recurse; linked audio files are listed.
- **Limits.** The index stops at 250,000 entries and says so ("First 250000 items").
- **Unreadable folders.** A folder that cannot be read is skipped and counted ("1 folder unreadable"). A folder that is missing or unreadable itself reports why.
- **Progress.** Entries arrive in batches of 512, so a search made while indexing sees what has been read so far. The browser's title row shows "Indexing N" until the index is done, and updates the results at most every 0.4 s.

## Search

- **Matching.** With text in the search box, the Samples browser lists the folders and audio files whose names contain every space-separated word, ignoring ASCII case. They are sorted by name and then path, and at most 2,000 are shown.
- **Where results are.** Each result shows its folder under the browser folder, so equal names in different folders can be told apart.
- **Empty search.** Clearing the search shows the folder's own items again.
- **Moved files.** Using a result whose file has moved or been deleted says so, and indexes the folder again. A moved file is found where it went once reindexed.

## Evidence

`tests/BrowserIndexTests.cpp` generates a library of 2,096 indexed entries: 40 folders of 50 samples with nested folders, Kicks in five folders, a sample ten folders down, and text files, hidden files, a hidden folder and a link back to the root. It checks:

- **Indexing.** It starts without waiting for the scan, and finds exactly the folders and audio files.
- **Duplicates.** Duplicate names are kept apart by their paths.
- **Matching.** Multi-word, case-insensitive matches; folders match too; an empty query finds nothing; result limits; hidden and other files are absent.
- **Moved files.** A moved file is found where it went.
- **Unreadable folders.** A locked folder is counted and its 51 entries are skipped (on macOS and Linux).
- **Failures.** A missing root reports why, and a limit of 100 truncates and says so.
- **Cancellation.**
  - Restarting on another folder mid-scan keeps only the new folder's entries.
  - An index destroyed while running is safe.
  - Cancelled scans end and free their slots.

The UI suite checks that the folder lists its own items, and that a search finds Kicks in the folders under it. It also checks that a deleted result is reported and searched again, and that clearing the search restores the listing.

## Not yet covered

- Indexing every place in the background: [Places](browser-places-contract.md) are indexed as they are opened.
- Watching folders for changes without reindexing.
- Searching outside the Samples browser, and finding projects, which folder listings show.
- Non-ASCII case folding.
- Tags and search by file attributes.
- Timing on real libraries of hundreds of thousands of files.
