#pragma once

#include <cstddef>

namespace mviewer::core
{

// Installed physical RAM in bytes. Returns 0 when the platform query fails or
// the value is unknown (callers should treat 0 as "do not shrink budgets").
size_t physicalMemoryBytes();

// Scale applied to the large in-memory image pools (Preview and Viewer).
// Unknown RAM (physicalBytes == 0) and machines with at least 12 GiB keep the
// authored defaults (1.0). 8 GiB and above use 0.75. Below 8 GiB use 0.5.
// Pure: depends only on physicalBytes.
double inMemoryCacheScale(size_t physicalBytes);

// defaultBytes * scale, raised to 16 MiB when the product would be smaller.
// A defaultBytes below 16 MiB is returned unchanged so a tiny authored cap is
// never inflated.
size_t scaledCacheBytes(size_t defaultBytes, double scale);

} // namespace mviewer::core
