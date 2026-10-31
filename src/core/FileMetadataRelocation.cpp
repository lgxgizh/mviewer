#include "FileMetadataRelocation.h"

#include "RatingStore.h"
#include "SidecarStore.h"
#include "TagStore.h"
#include "core/filesystem/Utf8Path.h"

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
    RatingStore::instance().renamePath(oldPath, newPath);
    TagStore::instance().renamePath(oldPath, newPath);
    SidecarStore::instance().relocateSidecar(oldPath, newPath);
}

} // namespace mviewer::core
