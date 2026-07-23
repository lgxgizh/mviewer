#pragma once

#include <string>
#include <vector>

namespace mviewer::domain
{

// A single match within a search result.
struct SearchMatch
{
    enum class Type : uint8_t
    {
        Filename,
        Metadata,
        Analysis,
        Path
    };

    Type type = Type::Filename;
    std::string key;     // e.g. "Make", "Lens", "Histogram", empty for filename
    std::string snippet; // the matched text fragment
};

// A search result item, returned by SearchEngine.
struct SearchResult
{
    std::string filePath;
    std::vector<SearchMatch> matches;
    int score = 0; // relevance score (higher = more relevant)

    bool operator<(const SearchResult &o) const
    {
        return score > o.score;
    }
};

} // namespace mviewer::domain
