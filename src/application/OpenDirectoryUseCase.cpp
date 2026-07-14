#include "OpenDirectoryUseCase.h"
#include "core/filesystem/FileSystem.h"
#include <algorithm>

OpenDirectoryUseCase::Result
OpenDirectoryUseCase::execute(const std::string &directoryPath, int maxImages) {
  Result r;
  std::vector<std::string> images =
      FileSystem::listImages(directoryPath, maxImages);
  for (const std::string &p : images) {
    r.imagePaths.push_back(p);
  }
  if (r.imagePaths.empty()) {
    r.error = "No supported images found in directory";
  }
  return r;
}
