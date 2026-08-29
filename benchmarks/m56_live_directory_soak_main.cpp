// M56: deterministic 10k-entry snapshot/delta soak.  This is a cheap,
// machine-stable guard for diff correctness and allocation growth; the real
// QFileSystemWatcher/UI path is covered by test_m56_directory_monitor and
// test_m56_live_gallery.
#include "core/filesystem/DirectorySnapshot.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
mviewer::core::DirectoryEntry makeEntry(size_t index, const std::string &prefix = "image_")
{
    mviewer::core::DirectoryEntry entry;
    entry.path = "C:/m56-soak/" + prefix + std::to_string(index) + ".png";
    entry.filename = prefix + std::to_string(index) + ".png";
    entry.identity = "identity-" + std::to_string(index);
    entry.extension = "png";
    entry.size = 1024 + index;
    entry.modifiedEpochMs = 100000 + static_cast<int64_t>(index);
    return entry;
}
} // namespace

int main(int argc, char **argv)
{
    const size_t requested = argc > 1 ? static_cast<size_t>(std::stoul(argv[1])) : 10000;
    if (requested < 1000)
    {
        std::fprintf(stderr, "M56 soak requires at least 1000 entries\n");
        return 2;
    }

    mviewer::core::DirectorySnapshot before;
    before.path = "C:/m56-soak";
    before.available = true;
    before.generation = 1;
    before.entries.reserve(requested);
    for (size_t i = 0; i < requested; ++i)
        before.entries.push_back(makeEntry(i));

    auto after = before;
    after.generation = 2;
    const size_t removedCount = requested / 40;
    const size_t touchedCount = requested / 10 - removedCount;
    for (size_t i = 0; i < touchedCount; ++i)
    {
        const size_t index = i * 10;
        after.entries[index].size += 7;
        after.entries[index].modifiedEpochMs += 1;
    }
    for (size_t i = 1; i < touchedCount; i += 2)
    {
        const size_t index = i * 10;
        after.entries[index].path = "C:/m56-soak/renamed_" + std::to_string(index) + ".png";
        after.entries[index].filename = "renamed_" + std::to_string(index) + ".png";
    }
    after.entries.erase(after.entries.end() - static_cast<std::ptrdiff_t>(removedCount),
                        after.entries.end());
    for (size_t i = 0; i < requested / 20; ++i)
        after.entries.push_back(makeEntry(requested + i, "added_"));

    const auto started = std::chrono::steady_clock::now();
    const auto delta = mviewer::core::diffDirectorySnapshots(before, after);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    const size_t expectedRenamed = (touchedCount + 1) / 2;
    const size_t expectedModified = touchedCount - expectedRenamed;
    if (delta.modified.size() != expectedModified || delta.renamed.size() != expectedRenamed ||
        delta.added.size() != requested / 20 || delta.removed.size() != removedCount)
    {
        std::fprintf(stderr, "M56 soak classification mismatch: modified=%zu renamed=%zu added=%zu removed=%zu\n",
                     delta.modified.size(), delta.renamed.size(), delta.added.size(),
                     delta.removed.size());
        return 1;
    }
    std::printf("M56 live directory soak: entries=%zu modified=%zu renamed=%zu added=%zu removed=%zu elapsed_ms=%lld PASS\n",
                requested, delta.modified.size(), delta.renamed.size(), delta.added.size(),
                delta.removed.size(), static_cast<long long>(elapsed));
    return 0;
}
