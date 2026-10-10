#pragma once

// Internal resample helpers. Not part of the public core contract.

#include "core/render/DisplayResample.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace mviewer::core::resample_detail
{

constexpr int kMaxTaps = 16;
constexpr int kWeightBits = 14;
constexpr int kWeightOne = 1 << kWeightBits;
constexpr int kHorizShift = 10; // Q14 accumulator -> Q4 sample
constexpr int kHorizBias = 1 << (kHorizShift - 1);
constexpr int kVertShift = 18; // Q4 sample * Q14 weight
constexpr int kVertBias = 1 << (kVertShift - 1);

enum class Isa : std::uint8_t
{
    Scalar,
    Sse41,
    Avx2
};

struct SampleLayout
{
    const uint8_t *base = nullptr;
    std::shared_ptr<std::vector<uint8_t>> buffer;
    int width = 0;
    int height = 0;
    int channels = 0;
    int red = 0;
    int green = 1;
    int blue = 2;
    bool gray = false;
};

struct SourceRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isValid() const
    {
        return width > 0 && height > 0;
    }
};

struct AxisKernel
{
    int srcCount = 0;
    int dstCount = 0;
    std::vector<int> origin;
    std::vector<int> count;
    std::vector<int16_t> weight;
};

struct RgbView
{
    const uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    int channels = 3;
    int red = 0;
    int green = 1;
    int blue = 2;
    bool gray = false;
    std::shared_ptr<std::vector<uint8_t>> keep;
};

Isa activeIsa() noexcept;
bool viewIsPackedRgb(const RgbView &view) noexcept;

SampleLayout layoutFor(const ImageData &src);
SourceRect clampSource(const SampleLayout &layout, const DisplayResampleRect &rect);
bool resampleCancelled(const std::atomic<bool> *cancel) noexcept;

void buildAxisKernel(AxisKernel &kernel, int srcCount, int dstCount);
void exchangeKernel(AxisKernel &kernel, std::vector<int> &origin, std::vector<int> &count,
                    std::vector<int16_t> &weight);

inline int clampIndex(int index, int count) noexcept
{
    if (index < 0)
        return 0;
    if (index >= count)
        return count - 1;
    return index;
}

inline uint16_t narrowHorizontal(int acc) noexcept
{
    int value = (acc + kHorizBias) >> kHorizShift;
    if (value < 0)
        value = 0;
    if (value > 65535)
        value = 65535;
    return static_cast<uint16_t>(value);
}

inline uint8_t narrowVertical(int acc) noexcept
{
    int value = (acc + kVertBias) >> kVertShift;
    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;
    return static_cast<uint8_t>(value);
}

void convolveHorizontalScalar(const RgbView &src, int y, const AxisKernel &kx, uint16_t *dstR,
                              uint16_t *dstG, uint16_t *dstB);
bool convolveHorizontalFast(const RgbView &src, int y, const AxisKernel &kx, uint16_t *dstR,
                            uint16_t *dstG, uint16_t *dstB);

void convolveVerticalScalar(const uint16_t *planeR, const uint16_t *planeG, const uint16_t *planeB,
                            int srcRow0, int srcH, const AxisKernel &ky, int y0, int y1, int dstW,
                            uint8_t *dst);
bool convolveVerticalFast(const uint16_t *planeR, const uint16_t *planeG, const uint16_t *planeB,
                          int srcRow0, int srcH, const AxisKernel &ky, int y0, int y1, int dstW,
                          uint8_t *dst);

void boxWriteBodyScalar(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX,
                        bool halfY, int y0, int y1);
bool boxWriteBodyFast(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX, bool halfY,
                      int y0, int y1);
void boxWriteEdges(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX, bool halfY);

bool boxReduce(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX, bool halfY,
               const std::atomic<bool> *cancel);
bool resizeSeparable(const RgbView &src, uint8_t *dst, int dstW, int dstH, const AxisKernel &kx,
                     const AxisKernel &ky, const std::atomic<bool> *cancel);
bool resizeBilinear(const RgbView &src, uint8_t *dst, int dstW, int dstH,
                    const std::atomic<bool> *cancel);
bool reduceByBox(RgbView &view, const SampleLayout &original, const SourceRect &rect, int dstW,
                 int dstH, const std::atomic<bool> *cancel);
void releasePyramidCache() noexcept;

void parallelForRows(int rowCount, int minRows, const std::atomic<bool> *cancel,
                     const std::function<void(int, int)> &fn);

} // namespace mviewer::core::resample_detail
