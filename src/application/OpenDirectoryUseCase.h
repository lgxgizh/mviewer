#pragma once
#include <functional>
#include <string>
#include <vector>

// UseCase: open a directory and load image list
// Emits progress callbacks (loaded count) and completion
class OpenDirectoryUseCase
{
  public:
    struct Result
    {
        std::vector<std::string> imagePaths;
        std::string error;
        bool success() const
        {
            return error.empty();
        }
    };

    // Synchronous version for simplicity (async can be added later).
    // Default raised from 1000 -> 100000 to match FileSystem::listImages and
    // avoid truncating large directories (10000-image benchmark target).
    // Callers may still pass an explicit smaller cap.
    static Result execute(const std::string &directoryPath, int maxImages = 100000);
};
