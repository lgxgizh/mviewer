#include "core/image/UsmSharpen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mviewer::core
{
namespace
{

std::vector<int> buildGaussianKernel1D(double sigma, int &radius)
{
    const double s = std::clamp(sigma, 0.1, 20.0);
    radius = std::clamp(static_cast<int>(std::ceil(3.0 * s)), 1, 60);
    const int size = 2 * radius + 1;
    std::vector<double> weights(size);
    double sum = 0.0;
    const double twoSigmaSq = 2.0 * s * s;
    for (int i = 0; i < size; ++i)
    {
        const double diff = static_cast<double>(i - radius);
        weights[i] = std::exp(-(diff * diff) / twoSigmaSq);
        sum += weights[i];
    }
    std::vector<int> kernel(size);
    int sumInt = 0;
    for (int i = 0; i < size; ++i)
    {
        kernel[i] = static_cast<int>(std::round((weights[i] / sum) * 16384.0));
        sumInt += kernel[i];
    }
    kernel[radius] += (16384 - sumInt);
    return kernel;
}

bool blurHorizontal(const uint8_t *src, int w, int h, int cpp, int colorChannels,
                    const std::vector<int> &kernel, int radius, uint8_t *temp,
                    const std::function<bool()> &cancelled)
{
    const int kSize = static_cast<int>(kernel.size());
    for (int y = 0; y < h; ++y)
    {
        if ((y & 15) == 0 && cancelled && cancelled())
            return false;
        const uint8_t *srcRow =
            src + static_cast<size_t>(y) * static_cast<size_t>(w) * static_cast<size_t>(cpp);
        uint8_t *tempRow = temp + static_cast<size_t>(y) * static_cast<size_t>(w) *
                                      static_cast<size_t>(colorChannels);
        for (int x = 0; x < w; ++x)
        {
            for (int c = 0; c < colorChannels; ++c)
            {
                int acc = 0;
                for (int k = 0; k < kSize; ++k)
                {
                    const int sx = std::clamp(x - radius + k, 0, w - 1);
                    acc += srcRow[static_cast<size_t>(sx) * cpp + c] * kernel[k];
                }
                tempRow[static_cast<size_t>(x) * colorChannels + c] =
                    static_cast<uint8_t>((acc + 8192) >> 14);
            }
        }
    }
    return true;
}

bool blurVerticalAndCombine(const uint8_t *src, const uint8_t *temp, int w, int h, int cpp,
                            int colorChannels, const std::vector<int> &kernel, int radius,
                            double amount, int threshold, uint8_t *dst,
                            const std::function<bool()> &cancelled)
{
    const int kSize = static_cast<int>(kernel.size());
    const size_t tempRowStride = static_cast<size_t>(w) * static_cast<size_t>(colorChannels);
    for (int y = 0; y < h; ++y)
    {
        if ((y & 15) == 0 && cancelled && cancelled())
            return false;
        const uint8_t *srcRow =
            src + static_cast<size_t>(y) * static_cast<size_t>(w) * static_cast<size_t>(cpp);
        uint8_t *dstRow =
            dst + static_cast<size_t>(y) * static_cast<size_t>(w) * static_cast<size_t>(cpp);
        for (int x = 0; x < w; ++x)
        {
            const size_t px = static_cast<size_t>(x) * cpp;
            for (int c = 0; c < colorChannels; ++c)
            {
                int acc = 0;
                for (int k = 0; k < kSize; ++k)
                {
                    const int sy = std::clamp(y - radius + k, 0, h - 1);
                    acc += temp[static_cast<size_t>(sy) * tempRowStride +
                                static_cast<size_t>(x) * colorChannels + c] *
                           kernel[k];
                }
                const int blurVal = (acc + 8192) >> 14;
                const int origVal = srcRow[px + c];
                const int diff = origVal - blurVal;
                if (std::abs(diff) >= threshold)
                {
                    const int sharp = static_cast<int>(std::round(origVal + amount * diff));
                    dstRow[px + c] = static_cast<uint8_t>(std::clamp(sharp, 0, 255));
                }
                else
                {
                    dstRow[px + c] = static_cast<uint8_t>(origVal);
                }
            }
            if (cpp == 4)
                dstRow[px + 3] = srcRow[px + 3];
        }
    }
    return true;
}

void extractLuma(const uint8_t *src, int w, int h, int cpp, PixelFormat format, uint8_t *luma)
{
    const bool isBgr = (format == PixelFormat::BGR24 || format == PixelFormat::BGRA32);
    const size_t totalPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < totalPixels; ++i)
    {
        const uint8_t *p = src + i * cpp;
        const int r = isBgr ? p[2] : p[0];
        const int g = p[1];
        const int b = isBgr ? p[0] : p[2];
        luma[i] = static_cast<uint8_t>((19595 * r + 38470 * g + 7471 * b) >> 16);
    }
}

bool combineLumaSharpen(const uint8_t *src, const uint8_t *lumaSrc, const uint8_t *lumaTemp, int w,
                        int h, int cpp, PixelFormat format, const std::vector<int> &kernel,
                        int radius, double amount, int threshold, uint8_t *dst,
                        const std::function<bool()> &cancelled)
{
    const int kSize = static_cast<int>(kernel.size());
    const bool isBgr = (format == PixelFormat::BGR24 || format == PixelFormat::BGRA32);
    for (int y = 0; y < h; ++y)
    {
        if ((y & 15) == 0 && cancelled && cancelled())
            return false;
        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(w);
        const uint8_t *srcRow = src + rowOffset * cpp;
        uint8_t *dstRow = dst + rowOffset * cpp;
        for (int x = 0; x < w; ++x)
        {
            int acc = 0;
            for (int k = 0; k < kSize; ++k)
            {
                const int sy = std::clamp(y - radius + k, 0, h - 1);
                acc += lumaTemp[static_cast<size_t>(sy) * static_cast<size_t>(w) +
                                static_cast<size_t>(x)] *
                       kernel[k];
            }
            const int blurLuma = (acc + 8192) >> 14;
            const int origLuma = lumaSrc[rowOffset + static_cast<size_t>(x)];
            const int diffLuma = origLuma - blurLuma;
            int delta = 0;
            if (std::abs(diffLuma) >= threshold)
                delta = static_cast<int>(std::round(amount * diffLuma));

            const int rIdx = isBgr ? 2 : 0;
            const int gIdx = 1;
            const int bIdx = isBgr ? 0 : 2;
            const size_t px = static_cast<size_t>(x) * cpp;
            dstRow[px + rIdx] = static_cast<uint8_t>(std::clamp(srcRow[px + rIdx] + delta, 0, 255));
            dstRow[px + gIdx] = static_cast<uint8_t>(std::clamp(srcRow[px + gIdx] + delta, 0, 255));
            dstRow[px + bIdx] = static_cast<uint8_t>(std::clamp(srcRow[px + bIdx] + delta, 0, 255));
            if (cpp == 4)
                dstRow[px + 3] = srcRow[px + 3];
        }
    }
    return true;
}

} // namespace

ImageData applyUsmSharpen(const ImageData &src, const UsmParams &params,
                          const std::function<bool()> &cancelled)
{
    if (src.isNull() || src.width <= 0 || src.height <= 0)
        return {};
    if (params.isIdentity())
    {
        ImageData copy = makeImageData(src.width, src.height, src.format);
        if (copy.buffer && src.buffer)
            std::memcpy(copy.buffer->data(), src.buffer->data(), src.byteSize());
        return copy;
    }

    int radius = 1;
    const std::vector<int> kernel = buildGaussianKernel1D(params.radius, radius);
    const int w = src.width;
    const int h = src.height;
    const int cpp = src.channelsPerPixel();
    const uint8_t *srcData = src.constView().data;

    ImageData dst = makeImageData(w, h, src.format);
    uint8_t *dstData = dst.mutableView().data;

    const bool lumaPath = params.lumaOnly && src.format != PixelFormat::Grayscale8;
    if (lumaPath)
    {
        std::vector<uint8_t> luma(static_cast<size_t>(w) * static_cast<size_t>(h));
        extractLuma(srcData, w, h, cpp, src.format, luma.data());
        std::vector<uint8_t> temp(static_cast<size_t>(w) * static_cast<size_t>(h));
        if (!blurHorizontal(luma.data(), w, h, 1, 1, kernel, radius, temp.data(), cancelled))
            return {};
        if (!combineLumaSharpen(srcData, luma.data(), temp.data(), w, h, cpp, src.format, kernel,
                                radius, params.amount, params.threshold, dstData, cancelled))
            return {};
    }
    else
    {
        const int colorChannels = (cpp == 4) ? 3 : cpp;
        std::vector<uint8_t> temp(static_cast<size_t>(w) * static_cast<size_t>(h) *
                                  static_cast<size_t>(colorChannels));
        if (!blurHorizontal(srcData, w, h, cpp, colorChannels, kernel, radius, temp.data(),
                            cancelled))
            return {};
        if (!blurVerticalAndCombine(srcData, temp.data(), w, h, cpp, colorChannels, kernel, radius,
                                    params.amount, params.threshold, dstData, cancelled))
            return {};
    }

    return dst;
}

} // namespace mviewer::core
