#pragma once

// M56: value-style directory state used by the live-folder monitor.  The
// header deliberately contains only standard-library types so filesystem
// observation and diffing do not depend on QWidget or Qt model state.

#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::core
{

struct DirectoryEntry
{
    std::string path;       // normalized absolute path
    std::string filename;
    std::string identity;   // stable file id when the platform exposes one
    std::string extension;  // lower-case, without the leading dot
    uint64_t size = 0;
    int64_t modifiedEpochMs = 0;

    bool operator==(const DirectoryEntry &other) const
    {
        return path == other.path && filename == other.filename && identity == other.identity &&
               extension == other.extension && size == other.size &&
               modifiedEpochMs == other.modifiedEpochMs;
    }
};

struct DirectorySnapshot
{
    std::string path;
    std::vector<DirectoryEntry> entries;  // supported image files only
    std::vector<DirectoryEntry> sidecars; // .xmp files, kept out of the gallery
    uint64_t generation = 0;
    bool available = false;
};

struct DirectoryRename
{
    DirectoryEntry before;
    DirectoryEntry after;
};

struct DirectoryDelta
{
    std::string path;
    uint64_t fromGeneration = 0;
    uint64_t toGeneration = 0;
    std::vector<DirectoryEntry> added;
    std::vector<DirectoryEntry> removed;
    std::vector<DirectoryEntry> modified;
    std::vector<DirectoryRename> renamed;
    std::vector<DirectoryEntry> sidecarAdded;
    std::vector<DirectoryEntry> sidecarRemoved;
    std::vector<DirectoryEntry> sidecarModified;
    bool directoryUnavailable = false;
    bool directoryRecovered = false;

    bool hasImageChanges() const
    {
        return !added.empty() || !removed.empty() || !modified.empty() || !renamed.empty();
    }

    bool hasSidecarChanges() const
    {
        return !sidecarAdded.empty() || !sidecarRemoved.empty() || !sidecarModified.empty();
    }

    bool empty() const
    {
        return !hasImageChanges() && !hasSidecarChanges() && !directoryUnavailable &&
               !directoryRecovered;
    }
};

// Capture the top-level state of a directory.  The implementation is safe to
// call from a worker thread and returns an unavailable snapshot instead of
// throwing when the path disappears or cannot be read.
DirectorySnapshot snapshotDirectory(const std::string &path, uint64_t generation = 0);

// Diff two committed snapshots.  Renames are inferred by stable identity when
// available, then by a unique size/mtime/extension fingerprint as a portable
// fallback.  Ambiguous matches remain add+remove so identity is never guessed.
DirectoryDelta diffDirectorySnapshots(const DirectorySnapshot &before,
                                      const DirectorySnapshot &after);

} // namespace mviewer::core
