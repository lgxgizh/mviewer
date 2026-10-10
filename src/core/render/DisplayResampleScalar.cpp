#include "core/render/DisplayResampleDetail.h"

#include <cmath>
#include <vector>

namespace mviewer::core::resample_detail
{
namespace
{

int channelIndex(const RgbView &src, int channel)
{
    if (channel == 1)
        return src.green;
    if (channel == 2)
        return src.blue;
    return src.red;
}

int sampleChannel(const uint8_t *row, const RgbView &src, int x, int channel)
{
    const uint8_t *pixel = row + static_cast<size_t>(x) * static_cast<size_t>(src.channels);
    if (src.gray)
        return pixel[0];
    return pixel[channelIndex(src, channel)];
}

const uint8_t *rowPointer(const RgbView &src, int y)
{
    return src.data + static_cast<size_t>(y) * static_cast<size_t>(src.stride) *
                          static_cast<size_t>(src.channels);
}

void writeBilinearPixel(const RgbView &src, uint8_t *dst, int x, int y, int dstW, int dstH)
{
    const double sy = (static_cast<double>(y) + 0.5) * static_cast<double>(src.height) /
                          static_cast<double>(dstH) -
                      0.5;
    const double sx = (static_cast<double>(x) + 0.5) * static_cast<double>(src.width) /
                          static_cast<double>(dstW) -
                      0.5;
    const double clampedY = sy > 0.0 ? sy : 0.0;
    const double clampedX = sx > 0.0 ? sx : 0.0;
    int y0 = static_cast<int>(clampedY);
    int x0 = static_cast<int>(clampedX);
    if (y0 >= src.height)
        y0 = src.height - 1;
    if (x0 >= src.width)
        x0 = src.width - 1;
    int y1 = y0 + 1;
    int x1 = x0 + 1;
    if (y1 >= src.height)
        y1 = src.height - 1;
    if (x1 >= src.width)
        x1 = src.width - 1;
    int fy = static_cast<int>(std::lround((clampedY - static_cast<double>(y0)) * 256.0));
    int fx = static_cast<int>(std::lround((clampedX - static_cast<double>(x0)) * 256.0));
    if (fy < 0)
        fy = 0;
    if (fy > 256)
        fy = 256;
    if (fx < 0)
        fx = 0;
    if (fx > 256)
        fx = 256;
    const uint8_t *row0 = rowPointer(src, y0);
    const uint8_t *row1 = rowPointer(src, y1);
    uint8_t *pixel =
        dst + (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) * 3u;
    for (int channel = 0; channel < 3; ++channel)
    {
        const int p00 = sampleChannel(row0, src, x0, channel);
        const int p10 = sampleChannel(row0, src, x1, channel);
        const int p01 = sampleChannel(row1, src, x0, channel);
        const int p11 = sampleChannel(row1, src, x1, channel);
        const int top = (p00 * (256 - fx) + p10 * fx + 128) >> 8;
        const int bottom = (p01 * (256 - fx) + p11 * fx + 128) >> 8;
        int value = (top * (256 - fy) + bottom * fy + 128) >> 8;
        if (value < 0)
            value = 0;
        if (value > 255)
            value = 255;
        pixel[channel] = static_cast<uint8_t>(value);
    }
}

} // namespace

bool viewIsPackedRgb(const RgbView &view) noexcept
{
    return !view.gray && view.channels == 3 && view.red == 0 && view.green == 1 && view.blue == 2;
}

void convolveHorizontalScalar(const RgbView &src, int y, const AxisKernel &kx, uint16_t *dstR,
                              uint16_t *dstG, uint16_t *dstB)
{
    const uint8_t *row = rowPointer(src, y);
    for (int x = 0; x < kx.dstCount; ++x)
    {
        const int taps = kx.count[static_cast<size_t>(x)];
        const int origin = kx.origin[static_cast<size_t>(x)];
        const int16_t *weight = kx.weight.data() + static_cast<size_t>(x) * kMaxTaps;
        int accR = 0;
        int accG = 0;
        int accB = 0;
        for (int tap = 0; tap < taps; ++tap)
        {
            const int sample = clampIndex(origin + tap, src.width);
            const int wr = static_cast<int>(weight[tap]);
            accR += sampleChannel(row, src, sample, 0) * wr;
            accG += sampleChannel(row, src, sample, 1) * wr;
            accB += sampleChannel(row, src, sample, 2) * wr;
        }
        dstR[x] = narrowHorizontal(accR);
        dstG[x] = narrowHorizontal(accG);
        dstB[x] = narrowHorizontal(accB);
    }
}

void convolveVerticalScalar(const uint16_t *planeR, const uint16_t *planeG, const uint16_t *planeB,
                            int srcRow0, int srcH, const AxisKernel &ky, int y0, int y1, int dstW,
                            uint8_t *dst)
{
    for (int y = y0; y < y1; ++y)
    {
        const int taps = ky.count[static_cast<size_t>(y)];
        const int origin = ky.origin[static_cast<size_t>(y)];
        const int16_t *weight = ky.weight.data() + static_cast<size_t>(y) * kMaxTaps;
        uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        for (int x = 0; x < dstW; ++x)
        {
            int accR = 0;
            int accG = 0;
            int accB = 0;
            for (int tap = 0; tap < taps; ++tap)
            {
                const int sample = clampIndex(origin + tap, srcH);
                const size_t offset =
                    (static_cast<size_t>(sample - srcRow0) * static_cast<size_t>(dstW)) +
                    static_cast<size_t>(x);
                const int wr = static_cast<int>(weight[tap]);
                accR += static_cast<int>(planeR[offset]) * wr;
                accG += static_cast<int>(planeG[offset]) * wr;
                accB += static_cast<int>(planeB[offset]) * wr;
            }
            out[static_cast<size_t>(x) * 3u] = narrowVertical(accR);
            out[static_cast<size_t>(x) * 3u + 1u] = narrowVertical(accG);
            out[static_cast<size_t>(x) * 3u + 2u] = narrowVertical(accB);
        }
    }
}

bool resizeBilinear(const RgbView &src, uint8_t *dst, int dstW, int dstH,
                    const std::atomic<bool> *cancel)
{
    if (dstW <= 0 || dstH <= 0 || src.data == nullptr)
        return false;
    parallelForRows(dstH, 32, cancel,
                    [&](int y0, int y1)
                    {
                        for (int y = y0; y < y1; ++y)
                        {
                            if (resampleCancelled(cancel))
                                return;
                            for (int x = 0; x < dstW; ++x)
                                writeBilinearPixel(src, dst, x, y, dstW, dstH);
                        }
                    });
    return !resampleCancelled(cancel);
}

bool resizeSeparable(const RgbView &src, uint8_t *dst, int dstW, int dstH, const AxisKernel &kx,
                     const AxisKernel &ky, const std::atomic<bool> *cancel)
{
    if (dstW <= 0 || dstH <= 0 || src.data == nullptr)
        return false;
    const bool allowFast = viewIsPackedRgb(src) && activeIsa() != Isa::Scalar;
    parallelForRows(
        dstH, 24, cancel,
        [&](int y0, int y1)
        {
            if (resampleCancelled(cancel))
                return;
            int first = src.height;
            int last = -1;
            for (int y = y0; y < y1; ++y)
            {
                const int taps = ky.count[static_cast<size_t>(y)];
                const int origin = ky.origin[static_cast<size_t>(y)];
                for (int tap = 0; tap < taps; ++tap)
                {
                    const int sample = clampIndex(origin + tap, src.height);
                    if (sample < first)
                        first = sample;
                    if (sample > last)
                        last = sample;
                }
            }
            if (last < first)
                return;
            const int rows = last - first + 1;
            const size_t planeCount = static_cast<size_t>(rows) * static_cast<size_t>(dstW);
            std::vector<uint16_t> planeR(planeCount);
            std::vector<uint16_t> planeG(planeCount);
            std::vector<uint16_t> planeB(planeCount);
            for (int sample = first; sample <= last; ++sample)
            {
                const size_t offset =
                    static_cast<size_t>(sample - first) * static_cast<size_t>(dstW);
                uint16_t *dstR = planeR.data() + offset;
                uint16_t *dstG = planeG.data() + offset;
                uint16_t *dstB = planeB.data() + offset;
                const bool ranFast =
                    allowFast && convolveHorizontalFast(src, sample, kx, dstR, dstG, dstB);
                if (!ranFast)
                    convolveHorizontalScalar(src, sample, kx, dstR, dstG, dstB);
            }
            const bool ranVertical =
                allowFast && convolveVerticalFast(planeR.data(), planeG.data(), planeB.data(),
                                                  first, src.height, ky, y0, y1, dstW, dst);
            if (!ranVertical)
                convolveVerticalScalar(planeR.data(), planeG.data(), planeB.data(), first,
                                       src.height, ky, y0, y1, dstW, dst);
        });
    return !resampleCancelled(cancel);
}

} // namespace mviewer::core::resample_detail
