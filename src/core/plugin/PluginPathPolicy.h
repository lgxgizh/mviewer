#pragma once

#include <string>
#include <vector>

// Path and identity checks that run before a plugin module is mapped.
// Hashing and directory enumeration read the filesystem. They do not call
// dlopen or LoadLibrary.

namespace mviewer::core
{

struct PluginFileAssessment
{
    bool regularFile = false;
    std::string canonicalUtf8;
    std::string error;
};

// `directoryUtf8` empty: the file itself must be a regular file, not a
// symlink or junction. Non-empty: that directory must be a real directory
// (not a symlink) and the file's canonical path must be a direct child of it.
PluginFileAssessment assessPluginFile(const std::string &fileUtf8,
                                      const std::string &directoryUtf8);

bool assessPluginDirectory(const std::string &directoryUtf8, std::string &canonicalUtf8,
                           std::string &error);

// Direct children with the platform plugin extension. Symlinks, junctions,
// and paths that canonicalize outside the directory are omitted.
std::vector<std::string> scanPluginCandidates(const std::string &dirPath);

// Lowercase hex SHA-256 of a regular file. Empty on failure. Does not map it.
std::string pluginFileSha256(const std::string &fileUtf8, std::string &error);

inline std::string normalizePluginSha256(std::string hex)
{
    for (char &ch : hex)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z')
            ch = static_cast<char>(c - 'A' + 'a');
    }
    return hex;
}

inline bool isPluginSha256Hex(const std::string &hex)
{
    if (hex.size() != 64)
        return false;
    for (const unsigned char c : hex)
    {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower)
            return false;
    }
    return true;
}

} // namespace mviewer::core
