#pragma once
#include <string>

class DeleteImageUseCase
{
  public:
    struct Result
    {
        bool success = false;
        std::string error;
    };

    // Move file to recycle bin (Qt's moveToTrash)
    static Result execute(const std::string &filePath);
};
