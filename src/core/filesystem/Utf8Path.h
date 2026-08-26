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

} // namespace mviewer::core
