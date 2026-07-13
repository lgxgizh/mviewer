#pragma once
#include <string>
#include <vector>

// UseCase: orchestrate multi-image comparison
class CompareImagesUseCase
{
public:
    struct Result {
        bool success = false;
        std::string error;
    };

    static Result execute(const std::vector<std::string> &imagePaths);
};
