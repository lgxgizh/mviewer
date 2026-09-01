#pragma once

// M58: value-semantics query state shared by Browse workers and tests.  The
// structure deliberately contains only immutable-at-evaluation inputs so a
// worker can evaluate a snapshot without reading Qt widgets or global stores.

#include <cstdint>
#include <string>

namespace mviewer::core
{

enum class BrowseSortField
{
    Name,
    Date,
    Size,
    Resolution,
    Type,
    Rating,
    Camera,
    Lens
};

struct BrowseQuery
{
    std::string text;
    bool recursive = false;
    bool metadata = false;
    int ratingMinimum = 0;
    int colorLabel = 0;
    bool rejectedOnly = false;
    bool pickedOnly = false;
    bool recentOnly = false;
    std::string camera;
    std::string lens;
    int iso = 0;
    std::string tag;
    std::string type;
    BrowseSortField sort = BrowseSortField::Name;
    bool ascending = true;
    uint64_t generation = 0;

    bool empty() const
    {
        return text.empty() && ratingMinimum == 0 && colorLabel == 0 && !rejectedOnly &&
               !pickedOnly && !recentOnly && camera.empty() && lens.empty() && iso == 0 &&
               tag.empty() && type.empty();
    }
};

} // namespace mviewer::core
