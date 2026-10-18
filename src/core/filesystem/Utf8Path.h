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

// One path-key normalizer for RatingStore / TagStore load and lookup.
// Applies QDir::fromNativeSeparators and QDir::cleanPath (slash form, "." /
// ".." collapsed). On Windows the key is also case-folded, because the
// filesystem is case-insensitive. Elsewhere case is preserved.
std::string normalizeKey(std::string path);

// Historical name. Same function as normalizeKey.
inline std::string normalizePathKey(std::string path)
{
    return normalizeKey(std::move(path));
}

} // namespace mviewer::core
