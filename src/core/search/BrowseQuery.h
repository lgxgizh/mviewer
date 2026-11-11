#pragma once

// M58: value-semantics query state shared by Browse workers and tests.  The
// structure deliberately contains only immutable-at-evaluation inputs so a
// worker can evaluate a snapshot without reading Qt widgets or global stores.

#include <cstdint>
#include <string>

namespace mviewer::core
{

enum class BrowseSortField : std::uint8_t
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
    bool recentOnly = false;
    std::string camera;
    std::string lens;
    int iso = 0;
    std::string type;
    BrowseSortField sort = BrowseSortField::Name;
    bool ascending = true;
    std::uint64_t generation = 0;

    bool empty() const
    {
        return text.empty() && !recentOnly && camera.empty() && lens.empty() && iso == 0 &&
               type.empty();
    }
};

} // namespace mviewer::core
