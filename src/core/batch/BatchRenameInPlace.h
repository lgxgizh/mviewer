#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mviewer::core
{

// In-place batch rename (Tools → Batch Rename). Every path is UTF-8. A plan
// entry renames `sourcePath` to `targetName` inside the same directory.
struct InPlaceRenameEntry
{
    std::string sourcePath;
    std::string targetName; // file name only, including the extension
};

enum class InPlaceRenameOutcome : std::uint8_t
{
    Renamed,
    Unchanged, // target name equals the current name
    Failed
};

struct InPlaceRenameItemResult
{
    std::string sourcePath;
    std::string targetPath;
    InPlaceRenameOutcome outcome = InPlaceRenameOutcome::Failed;
    bool replacedExisting = false; // an unrelated file with the target name went to the trash
    std::string errorMessage;
};

struct InPlaceRenameReport
{
    std::vector<InPlaceRenameItemResult> items; // same order as the plan
    int renamed = 0;
    int unchanged = 0;
    int failed = 0;
    int replaced = 0;
};

// Moves one file to the recycle bin; false when the file must stay in place.
using TrashFunction = std::function<bool(const std::string &path)>;

// QFile::moveToTrash (the system recycle bin).
bool moveFileToSystemTrash(const std::string &path);

// Paths that already exist under a planned target name and are not themselves
// renamed by this plan. renameInPlace() replaces them (they go to the trash).
std::vector<std::string>
inPlaceRenameExternalCollisions(const std::vector<InPlaceRenameEntry> &plan);

// Renames every entry without one rename clobbering another: sources move to
// unique temporary names first, then to their targets, so swaps (a→b, b→a)
// and case-only renames work. A target that collides with a file outside the
// plan is replaced: that file is moved to the trash first. When the trash
// refuses, the entry is skipped and reported as Failed with the source kept.
// Plans with duplicate (case-insensitive) or invalid target names are
// rejected entry by entry; callers validate with validateBatchRename() first.
InPlaceRenameReport renameInPlace(const std::vector<InPlaceRenameEntry> &plan,
                                  const TrashFunction &trash = moveFileToSystemTrash);

} // namespace mviewer::core
