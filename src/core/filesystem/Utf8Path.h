#pragma once

#include <filesystem>
#include <string>

namespace mviewer::core
{

// Explicit boundary between the application's UTF-8 path contract and the
// native filesystem representation. On Windows this is UTF-8 <-> UTF-16;
// using path(string) or path.string() here is not Unicode-safe.
std::filesystem::path pathFromUtf8(const std::string &path);
std::string pathToUtf8(const std::filesystem::path &path);

// Slash-normalize a path key for RatingStore / TagStore lookups. On Windows the
// filesystem is case-insensitive, so the key is also case-folded (same idea as
// BrowsePathIndex); elsewhere only '\' -> '/' is applied.
std::string normalizePathKey(std::string path);

} // namespace mviewer::core
