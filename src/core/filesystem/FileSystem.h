#pragma once

#include <string>
#include <vector>

// Filesystem layer: scan directories and enumerate image files. Change watching (QFileSystemWatcher) can be added later.
class FileSystem
{
  public:
    static std::vector<std::string> imageFilters();

    // List images in a directory (by filename); truncate when over the limit.
    // Default cap raised from 2000 -> 100000 so directories with >2000 images
    // (e.g. the 10000-image benchmark corpus) are no longer silently truncated.
    // An explicit `max` is still honored (pass 0 for "no limit").
    static std::vector<std::string> listImages(const std::string &dir, int max = 100000);

    // Check whether a path is a supported image file
    static bool isImage(const std::string &path);
};
