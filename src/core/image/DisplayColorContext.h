#pragma once

// Qt-free value type describing the presentation target for a raster.  The
// actual QColorSpace conversion lives in QtConvert.cpp; keeping this context
// free of Qt lets asynchronous core/application requests carry a stable
// target snapshot without leaking UI types into the core contract.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mviewer::core
{

struct DisplayColorContext
{
    // An empty profile means the deterministic sRGB fallback target.  For a
    // real target this contains the ICC bytes supplied by the platform or a
    // test fixture.
    std::vector<uint8_t> iccProfile;
    // Stable target identity used by raster caches.  Callers may provide a
    // platform fingerprint; fromIccProfile() derives one when absent.
    std::string fingerprint;
    // Incremented when the target/profile changes (for example a monitor
    // change).  It prevents a previous target's raster from being reused.
    uint64_t generation = 0;

    static DisplayColorContext sRGB(uint64_t generation = 0)
    {
        DisplayColorContext context;
        context.fingerprint = "srgb";
        context.generation = generation;
        return context;
    }

    static DisplayColorContext fromIccProfile(std::vector<uint8_t> profile, uint64_t generation = 0,
                                              std::string identity = {})
    {
        DisplayColorContext context;
        context.iccProfile = std::move(profile);
        context.generation = generation;
        if (!identity.empty())
            context.fingerprint = std::move(identity);
        else
        {
            // FNV-1a is sufficient here: the fingerprint is a cache identity,
            // not a security primitive, and keeps this value type Qt-free.
            uint64_t hash = 1469598103934665603ULL;
            for (const uint8_t byte : context.iccProfile)
            {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            context.fingerprint = "icc-" + std::to_string(hash);
        }
        return context;
    }

    bool hasProfile() const
    {
        return !iccProfile.empty();
    }

    std::string cacheKey() const
    {
        return fingerprint.empty() ? "srgb@" + std::to_string(generation)
                                   : fingerprint + "@" + std::to_string(generation);
    }
};

} // namespace mviewer::core
