#include "FileMetadataRelocation.h"

#include "RatingStore.h"
#include "SidecarStore.h"
#include "TagStore.h"

namespace mviewer::core
{

void relocateFileMetadata(const std::string &oldPath, const std::string &newPath)
{
    if (oldPath.empty() || newPath.empty() || oldPath == newPath)
        return;
    RatingStore::instance().renamePath(oldPath, newPath);
    TagStore::instance().renamePath(oldPath, newPath);
    SidecarStore::instance().relocateSidecar(oldPath, newPath);
}

} // namespace mviewer::core
