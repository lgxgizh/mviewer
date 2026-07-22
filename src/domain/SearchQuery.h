#pragma once

#include <string>

namespace mviewer::domain
{

// Domain-level search query DTO (no Qt dependency).
// Describes what the user wants to search for and across which scopes.
struct SearchQuery
{
    std::string text; // the search term(s)
    bool searchFilenames = true;
    bool searchMetadata = true;  // EXIF, camera model, lens, ISO, etc.
    bool searchAnalysis = true;  // analyzer result text
    bool searchPaths = false;    // full file-path substring match
    bool caseSensitive = false;

    bool empty() const { return text.empty(); }
};

} // namespace mviewer::domain
