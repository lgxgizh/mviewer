#include "FileMetadataRelocation.h"

#include "RecentStore.h"
#include "core/filesystem/Utf8Path.h"

#include <filesystem>

namespace mviewer::core
{

void relocateFileMetadata(const std::string &oldPath, const std::string &newPath)
{
    if (oldPath.empty() || newPath.empty() || oldPath == newPath)
        return;
    // Case-only renames share one store key on Windows. Relocating would
    // rewrite the sidecar onto itself and then delete it.
    if (normalizeKey(oldPath) == normalizeKey(newPath))
        return;
    RecentStore::instance().renamePath(oldPath, newPath);

    // Full image name + suffix only. "<stem>.xmp" can be a third-party XMP.
    namespace fs = std::filesystem;
    for (const char *suffix : {".mviewer.json", ".xmp"})
    {
        const std::string src = oldPath + suffix;
        const std::string dst = newPath + suffix;
        std::error_code ec;
        const bool srcFile = fs::is_regular_file(pathFromUtf8(src), ec);
        if (ec || !srcFile)
            continue;
        const bool dstExists = fs::exists(pathFromUtf8(dst), ec);
        if (ec || dstExists)
            continue;
        fs::rename(pathFromUtf8(src), pathFromUtf8(dst), ec);
    }
}

} // namespace mviewer::core
