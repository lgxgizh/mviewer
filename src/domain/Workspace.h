#pragma once

#include "Image.h"

#include <string>
#include <vector>

// Domain data model (Architect "Data Model" item): a workspace is a root
// directory the user opened; it contains one or more Folders (sub-directories
// scanned for images); each Folder holds an ImageSet — the collection of
// images discovered in that folder, modelled as ImageMetadata entries (no
// pixels; pixels are fetched on demand via ImageRepository::load).
//
// These are pure value types (no Qt). They describe *what* the user is looking
// at; pixel decoding and caching remain the responsibility of ImageRepository.

namespace mviewer::domain
{

// A set of images discovered in a single directory.
struct ImageSet
{
    std::string folderPath;
    std::vector<ImageMetadata> images;

    bool empty() const
    {
        return images.empty();
    }
    size_t size() const
    {
        return images.size();
    }

    // Index of a file by name, or -1 if not present.
    int indexOf(const std::string &fileName) const
    {
        for (size_t i = 0; i < images.size(); ++i)
            if (images[i].fileName == fileName)
                return static_cast<int>(i);
        return -1;
    }
};

// A folder (directory) the workspace scanned, with its image set.
struct Folder
{
    std::string path;
    std::string name; // directory name (last path component)
    ImageSet imageSet;

    bool empty() const
    {
        return imageSet.empty();
    }
};

// A workspace: the root the user opened, plus every folder scanned under it.
struct Workspace
{
    std::string rootPath;
    std::vector<Folder> folders;

    // M12.2 (review fix): explicit list of image paths that were open in the
    // Compare session when the workspace was saved. Stored explicitly rather
    // than inferred from ROI/analysis presence, because a compare session with
    // neither ROI nor analysis would otherwise be lost on reopen. May be empty
    // for single-image or legacy workspaces.
    std::vector<std::string> comparedImages;

    // M15: serialized CompareSession snapshot (sync mode, per-cell zoom/pan,
    // shared transform, ROI). Stored as an embedded JSON string so the
    // Workspace model stays a flat value type and the serializer change is
    // localized. Empty when no compare session was active at save time.
    std::string compareSessionJson;

    bool empty() const
    {
        return folders.empty();
    }
    size_t folderCount() const
    {
        return folders.size();
    }

    // Total image count across all folders.
    size_t imageCount() const
    {
        size_t n = 0;
        for (const auto &f : folders)
            n += f.imageSet.size();
        return n;
    }

    // First non-empty folder's image set (the default browsing view).
    const ImageSet *firstImageSet() const
    {
        for (const auto &f : folders)
            if (!f.empty())
                return &f.imageSet;
        return nullptr;
    }
};

} // namespace mviewer::domain
