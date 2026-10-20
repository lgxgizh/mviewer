#pragma once

#include <string>

namespace mviewer::domain
{

// Domain Error Types for image decoding operations.
// Pure C++20 domain layer: zero Qt dependencies.
enum class DecodeErrorCode
{
    Success = 0,
    FileNotFound,
    AccessDenied,
    CorruptData,
    UnsupportedFormat,
    OutOfMemory,
    Unknown,
    // The request that owned the decode went stale (cooperative cancel). Not
    // a property of the file: never report it as corrupt or cache it.
    Cancelled
};

struct DecodeError
{
    DecodeErrorCode code = DecodeErrorCode::Unknown;
    std::string message;
    std::string path;

    std::string toString() const
    {
        std::string s = "DecodeError: ";
        s += message;
        if (!path.empty())
        {
            s += " (path: " + path + ")";
        }
        return s;
    }
};

} // namespace mviewer::domain
