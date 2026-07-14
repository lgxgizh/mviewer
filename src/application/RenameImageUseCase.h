#pragma once
#include <string>

class RenameImageUseCase
{
public:
    struct Result
    {
        bool success = false;
        std::string error;
        std::string newPath;
    };

    static Result execute(const std::string& oldPath, const std::string& newName);
};
