#pragma once
#include <string>
#include <vector>
#include <functional>

// UseCase: open a directory and load image list
// Emits progress callbacks (loaded count) and completion
class OpenDirectoryUseCase
{
public:
    struct Result {
        std::vector<std::string> imagePaths;
        std::string error;
        bool success() const { return error.empty(); }
    };

    // Synchronous version for simplicity (async can be added later)
    static Result execute(const std::string &directoryPath, int maxImages = 1000);
};
