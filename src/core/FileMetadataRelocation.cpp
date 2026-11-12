#include "FileMetadataRelocation.h"

#include "RecentStore.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/rawplain/PlainRawFile.h"

#include <filesystem>

namespace mviewer::core
{

namespace
{

bool movePlainRawOverride(const std::string &oldPath, const std::string &newPath)
{
    try
    {
        namespace raw = mviewer::core::rawplain;
        // Same kind keeps the override; .raw <-> .gray drops it (the params
        // do not carry over). A destination that is not a plain dump (e.g. a
        // trash name) leaves the store alone, as before ADR 034.
        if (!raw::isPlainRawPath(oldPath) || !raw::isPlainRawPath(newPath))
            return false;
        auto &store = raw::PlainRawParamStore::instance();
        const std::string fromKey = raw::plainRawParamKey(oldPath);
        if (raw::dumpKindForPath(oldPath) == raw::dumpKindForPath(newPath))
        {
            store.moveOverride(fromKey, raw::plainRawParamKey(newPath));
            return true;
        }
        if (store.override(fromKey).has_value())
            store.clearOverride(fromKey);
        return false;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

void relocateFileMetadata(const std::string &oldPath, const std::string &newPath)
{
    if (oldPath.empty() || newPath.empty() || oldPath == newPath)
        return;
    // Case-only renames share one store key on Windows. Relocating would
    // rewrite the sidecar onto itself and then delete it.
    if (normalizeKey(oldPath) == normalizeKey(newPath))
        return;
    movePlainRawOverride(oldPath, newPath);
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
