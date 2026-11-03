#pragma once

#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvUnpack.h"

#include <cstdint>
#include <mutex>
#include <string>

// Raw Y/U/V code-value probe for the pixel inspector. One cached unpacked frame.

namespace mviewer::core::yuv
{

struct YuvSample
{
    bool valid = false;
    int x = 0;
    int y = 0;
    int64_t frameIndex = 0;
    int bitDepth = 8;
    bool hasChroma = false;
    int chromaX = 0;
    int chromaY = 0;
    uint16_t luma = 0;
    uint16_t cb = 0;
    uint16_t cr = 0;
};

// valid=false when x/y are outside the luma plane or the required planes are empty.
YuvSample sampleYuvPlanes(const YuvPlanes &planes, const Format &f, int x, int y);

// "Y=235 U=128 V=128 (8-bit, chroma 3,2)"; gray: "Y=235 (8-bit)"; invalid: "".
std::string formatYuvSample(const YuvSample &s);

// value / (2^bitDepth - 1). Returns 0 when bitDepth is outside 1..16.
double normalizedSample(uint16_t value, int bitDepth);

// Thread-safe. Keeps one unpacked frame keyed by path, frame, cache suffix,
// parameter-store revision, and file size.
class YuvSampleProbe
{
  public:
    static YuvSampleProbe &instance();

    YuvSample sample(const std::string &path, int64_t frameIndex, int x, int y);
    void clear();
    uint64_t readCount() const;

    YuvSampleProbe(const YuvSampleProbe &) = delete;
    YuvSampleProbe &operator=(const YuvSampleProbe &) = delete;

  private:
    YuvSampleProbe() = default;

    struct Cache
    {
        bool present = false;
        std::string path;
        int64_t frameIndex = 0;
        std::string suffix;
        uint64_t revision = 0;
        int64_t fileSize = 0;
        Format format;
        YuvPlanes planes;
    };

    bool cacheHit(const std::string &path, int64_t frameIndex, const std::string &suffix,
                  uint64_t revision, int64_t fileSize) const;

    mutable std::mutex m_mutex;
    Cache m_cache;
    uint64_t m_reads = 0;
};

} // namespace mviewer::core::yuv
