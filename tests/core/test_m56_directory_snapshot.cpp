// M56: deterministic, UI-independent snapshot/delta contracts.
#include "core/filesystem/DirectorySnapshot.h"

#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
        std::printf("PASS: %s\n", message);
    else
    {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

mviewer::core::DirectoryEntry entry(const char *path, const char *identity, uint64_t size,
                                    int64_t modified, const char *extension = "png")
{
    mviewer::core::DirectoryEntry result;
    result.path = path;
    result.filename = path;
    result.identity = identity;
    result.extension = extension;
    result.size = size;
    result.modifiedEpochMs = modified;
    return result;
}
} // namespace

int main()
{
    using mviewer::core::DirectorySnapshot;

    DirectorySnapshot before;
    before.path = "C:/m56";
    before.available = true;
    before.generation = 10;
    before.entries = {entry("C:/m56/a.png", "id-a", 10, 100),
                      entry("C:/m56/b.png", "id-b", 20, 200),
                      entry("C:/m56/old.png", "id-old", 30, 300)};
    before.sidecars = {entry("C:/m56/a.xmp", "side-a", 4, 400, "xmp")};

    DirectorySnapshot after = before;
    after.generation = 11;
    after.entries = {entry("C:/m56/a.png", "id-a", 11, 101),
                     entry("C:/m56/b-renamed.png", "id-b", 20, 200),
                     entry("C:/m56/new.png", "id-new", 40, 500)};
    after.sidecars = {entry("C:/m56/a.xmp", "side-a", 5, 401, "xmp"),
                      entry("C:/m56/b-renamed.xmp", "side-b", 3, 401, "xmp")};

    const auto delta = mviewer::core::diffDirectorySnapshots(before, after);
    check(delta.fromGeneration == 10 && delta.toGeneration == 11,
          "delta carries the committed snapshot generations");
    check(delta.modified.size() == 1 && delta.modified.front().path == "C:/m56/a.png",
          "same-path overwrite is classified as modified");
    check(delta.renamed.size() == 1 && delta.renamed.front().before.path == "C:/m56/b.png" &&
              delta.renamed.front().after.path == "C:/m56/b-renamed.png",
          "unique identity migration is classified as rename");
    check(delta.removed.size() == 1 && delta.removed.front().path == "C:/m56/old.png" &&
              delta.added.size() == 1 && delta.added.front().path == "C:/m56/new.png",
          "unrelated removal and addition remain explicit");
    check(delta.sidecarModified.size() == 1 && delta.sidecarAdded.size() == 1,
          "sidecar changes are separated from gallery image changes");

    DirectorySnapshot ambiguous = before;
    ambiguous.entries = {entry("C:/m56/new-a.png", "same", 99, 900),
                         entry("C:/m56/new-b.png", "same", 99, 900)};
    DirectorySnapshot ambiguousBefore = before;
    ambiguousBefore.entries = {entry("C:/m56/old-a.png", "same", 99, 900),
                               entry("C:/m56/old-b.png", "same", 99, 900)};
    const auto ambiguousDelta =
        mviewer::core::diffDirectorySnapshots(ambiguousBefore, ambiguous);
    check(ambiguousDelta.renamed.empty() && ambiguousDelta.added.size() == 2 &&
              ambiguousDelta.removed.size() == 2,
          "ambiguous fingerprints never invent a rename");

    DirectorySnapshot unavailable;
    unavailable.path = before.path;
    unavailable.generation = 12;
    const auto unavailableDelta =
        mviewer::core::diffDirectorySnapshots(before, unavailable);
    check(unavailableDelta.directoryUnavailable && unavailableDelta.removed.size() == 3 &&
              unavailableDelta.sidecarRemoved.size() == 1,
          "directory disappearance is explicit and does not become an empty folder");
    const auto repeatedUnavailable =
        mviewer::core::diffDirectorySnapshots(unavailable, unavailable);
    check(repeatedUnavailable.empty() && !repeatedUnavailable.directoryUnavailable,
          "repeated unavailable scans do not emit a busy-loop delta");
    const auto recovered = mviewer::core::diffDirectorySnapshots(unavailable, after);
    check(recovered.directoryRecovered && !recovered.directoryUnavailable,
          "directory recovery is distinguishable from an ordinary addition");

    std::printf("M56 directory snapshot tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
