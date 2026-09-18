#include "core/analysis/PixelInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// NOLINTBEGIN(bugprone-easily-swappable-parameters,bugprone-incorrect-roundings,cppcoreguidelines-pro-type-vararg)

namespace mviewer::core
{
namespace
{
inline double clamp01(double x)
{
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

// Rec. 709 luma — the luminance used for neighborhood stats.
inline double luma(double r, double g, double b)
{
    return 0.2126 * r + 0.7152 * g + 0.0722 * b; // r,g,b in 0..1
}
} // namespace
namespace
{
struct CropBounds
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

CropBounds analysisCropBounds(int sourceWidth, int sourceHeight,
                              const AnalysisAdjustment &adjustment)
{
    CropBounds bounds{0, 0, sourceWidth, sourceHeight};
    if (!adjustment.hasCrop || adjustment.cropW <= 0 || adjustment.cropH <= 0)
        return bounds;

    const auto clampCoordinate = [](long long value, int limit)
    { return static_cast<int>(std::clamp(value, 0LL, static_cast<long long>(limit))); };
    bounds.x = clampCoordinate(adjustment.cropX, sourceWidth);
    bounds.y = clampCoordinate(adjustment.cropY, sourceHeight);
    const int right =
        clampCoordinate(static_cast<long long>(adjustment.cropX) + adjustment.cropW, sourceWidth);
    const int bottom =
        clampCoordinate(static_cast<long long>(adjustment.cropY) + adjustment.cropH, sourceHeight);
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
    return bounds;
}

CropBounds analysisCropBounds(const ImageData &source, const AnalysisAdjustment &adjustment)
{
    return analysisCropBounds(source.width, source.height, adjustment);
}

void rotateDisplayToCropped(int rotation, int cropWidth, int cropHeight, int displayX, int displayY,
                            int &croppedX, int &croppedY)
{
    switch (rotation)
    {
    case 90:
        croppedX = displayY;
        croppedY = cropHeight - 1 - displayX;
        break;
    case 180:
        croppedX = cropWidth - 1 - displayX;
        croppedY = cropHeight - 1 - displayY;
        break;
    case 270:
        croppedX = cropWidth - 1 - displayY;
        croppedY = displayX;
        break;
    default:
        croppedX = displayX;
        croppedY = displayY;
        break;
    }
}

void rotateCroppedToDisplay(int rotation, int cropWidth, int cropHeight, int croppedX, int croppedY,
                            int &displayX, int &displayY)
{
    switch (rotation)
    {
    case 90:
        displayX = cropHeight - 1 - croppedY;
        displayY = croppedX;
        break;
    case 180:
        displayX = cropWidth - 1 - croppedX;
        displayY = cropHeight - 1 - croppedY;
        break;
    case 270:
        displayX = croppedY;
        displayY = cropWidth - 1 - croppedX;
        break;
    default:
        displayX = croppedX;
        displayY = croppedY;
        break;
    }
}

mviewer::domain::Selection boundsFromInclusiveCorners(const int xs[4], const int ys[4], int width,
                                                      int height)
{
    int x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < 4; ++i)
    {
        x0 = std::min(x0, xs[i]);
        x1 = std::max(x1, xs[i]);
        y0 = std::min(y0, ys[i]);
        y1 = std::max(y1, ys[i]);
    }
    return mviewer::domain::normalizeSelection(x0, y0, x1 + 1, y1 + 1, width, height);
}

int normalizedRotation(int rotation)
{
    rotation %= 360;
    if (rotation < 0)
        rotation += 360;
    return rotation;
}

int adjustAnalysisChannel(int value, const AnalysisAdjustment &adjustment)
{
    if (adjustment.brightness != 0)
        value = std::clamp(value + std::clamp(adjustment.brightness, -255, 255), 0, 255);

    const float contrast = static_cast<float>(adjustment.contrast);
    if (std::abs(contrast - 1.0f) >= 1e-6f)
    {
        const float v = (static_cast<float>(value) - 128.0f) * std::max(contrast, 0.0f) + 128.0f;
        value = std::clamp(static_cast<int>(std::lroundf(v)), 0, 255);
    }

    const float gamma = static_cast<float>(adjustment.gamma);
    if (std::abs(gamma - 1.0f) >= 1e-6f)
    {
        const float clampedGamma = std::clamp(gamma, 0.05f, 8.0f);
        const float corrected = std::pow(static_cast<float>(value) / 255.0f, 1.0f / clampedGamma);
        value = static_cast<int>(std::lroundf(corrected * 255.0f));
    }
    return std::clamp(value, 0, 255);
}

struct AnalysisLUT
{
    std::array<uint8_t, 256> r{};
    std::array<uint8_t, 256> g{};
    std::array<uint8_t, 256> b{};
    bool isIdentity = true;
};

AnalysisLUT buildAnalysisLUT(const AnalysisAdjustment &adjustment, PixelFormat format)
{
    AnalysisLUT lut;
    lut.isIdentity = (adjustment.brightness == 0 &&
                      std::abs(static_cast<float>(adjustment.contrast) - 1.0f) < 1e-6f &&
                      std::abs(static_cast<float>(adjustment.gamma) - 1.0f) < 1e-6f &&
                      std::abs(static_cast<float>(adjustment.redGain) - 1.0f) < 1e-6f &&
                      std::abs(static_cast<float>(adjustment.blueGain) - 1.0f) < 1e-6f);
    if (lut.isIdentity)
        return lut;

    for (int i = 0; i < 256; ++i)
    {
        const int ch = adjustAnalysisChannel(i, adjustment);
        lut.g[static_cast<size_t>(i)] = static_cast<uint8_t>(ch);
        if (format != PixelFormat::Grayscale8)
        {
            int r = ch, b = ch;
            if (std::abs(static_cast<float>(adjustment.redGain) - 1.0f) >= 1e-6f)
                r = std::clamp(static_cast<int>(std::lroundf(
                                   static_cast<float>(r) *
                                   std::max(static_cast<float>(adjustment.redGain), 0.01f))),
                               0, 255);
            if (std::abs(static_cast<float>(adjustment.blueGain) - 1.0f) >= 1e-6f)
                b = std::clamp(static_cast<int>(std::lroundf(
                                   static_cast<float>(b) *
                                   std::max(static_cast<float>(adjustment.blueGain), 0.01f))),
                               0, 255);
            lut.r[static_cast<size_t>(i)] = static_cast<uint8_t>(r);
            lut.b[static_cast<size_t>(i)] = static_cast<uint8_t>(b);
        }
        else
        {
            lut.r[static_cast<size_t>(i)] = static_cast<uint8_t>(ch);
            lut.b[static_cast<size_t>(i)] = static_cast<uint8_t>(ch);
        }
    }
    return lut;
}
} // namespace

AnalysisPixel sampleAnalysisPixel(const ImageData &source, const AnalysisAdjustment &adjustment,
                                  int adjustedX, int adjustedY)
{
    AnalysisPixel result;
    if (source.isNull())
        return result;

    const CropBounds crop = analysisCropBounds(source, adjustment);
    if (crop.width <= 0 || crop.height <= 0)
        return result;

    const int rotation = normalizedRotation(adjustment.rotation);
    const int outputWidth = (rotation == 90 || rotation == 270) ? crop.height : crop.width;
    const int outputHeight = (rotation == 90 || rotation == 270) ? crop.width : crop.height;
    if (adjustedX < 0 || adjustedY < 0 || adjustedX >= outputWidth || adjustedY >= outputHeight)
        return result;

    int croppedX = adjustedX;
    int croppedY = adjustedY;
    switch (rotation)
    {
    case 90:
        croppedX = adjustedY;
        croppedY = crop.height - 1 - adjustedX;
        break;
    case 180:
        croppedX = crop.width - 1 - adjustedX;
        croppedY = crop.height - 1 - adjustedY;
        break;
    case 270:
        croppedX = crop.width - 1 - adjustedY;
        croppedY = adjustedX;
        break;
    default:
        break;
    }

    if (adjustment.flipH)
        croppedX = crop.width - 1 - croppedX;
    if (adjustment.flipV)
        croppedY = crop.height - 1 - croppedY;

    const PixelRGBA sourcePixel = samplePixel(source, crop.x + croppedX, crop.y + croppedY);
    if (!sourcePixel.valid)
        return result;

    const bool isAdjIdentity = (adjustment.brightness == 0 &&
                                std::abs(static_cast<float>(adjustment.contrast) - 1.0f) < 1e-6f &&
                                std::abs(static_cast<float>(adjustment.gamma) - 1.0f) < 1e-6f &&
                                std::abs(static_cast<float>(adjustment.redGain) - 1.0f) < 1e-6f &&
                                std::abs(static_cast<float>(adjustment.blueGain) - 1.0f) < 1e-6f);
    if (isAdjIdentity)
    {
        result.r = sourcePixel.r;
        result.g = sourcePixel.g;
        result.b = sourcePixel.b;
        result.valid = true;
        return result;
    }

    result.r = adjustAnalysisChannel(sourcePixel.r, adjustment);
    result.g = adjustAnalysisChannel(sourcePixel.g, adjustment);
    result.b = adjustAnalysisChannel(sourcePixel.b, adjustment);
    if (source.format != PixelFormat::Grayscale8)
    {
        if (std::abs(static_cast<float>(adjustment.redGain) - 1.0f) >= 1e-6f)
            result.r = std::clamp(static_cast<int>(std::lroundf(
                                      static_cast<float>(result.r) *
                                      std::max(static_cast<float>(adjustment.redGain), 0.01f))),
                                  0, 255);
        if (std::abs(static_cast<float>(adjustment.blueGain) - 1.0f) >= 1e-6f)
            result.b = std::clamp(static_cast<int>(std::lroundf(
                                      static_cast<float>(result.b) *
                                      std::max(static_cast<float>(adjustment.blueGain), 0.01f))),
                                  0, 255);
    }
    result.valid = true;
    return result;
}

mviewer::domain::Selection mapDisplaySelectionToSource(const mviewer::domain::Selection &display,
                                                       const AnalysisAdjustment &adjustment,
                                                       int sourceWidth, int sourceHeight)
{
    if (display.isEmpty() || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    const CropBounds crop = analysisCropBounds(sourceWidth, sourceHeight, adjustment);
    if (crop.width <= 0 || crop.height <= 0)
        return {};
    const int rotation = normalizedRotation(adjustment.rotation);
    const int displayWidth = (rotation == 90 || rotation == 270) ? crop.height : crop.width;
    const int displayHeight = (rotation == 90 || rotation == 270) ? crop.width : crop.height;
    const auto clipped = mviewer::domain::normalizeSelection(
        display.x, display.y, display.x + display.width, display.y + display.height, displayWidth,
        displayHeight);
    if (clipped.isEmpty())
        return {};
    const int x1 = clipped.x + clipped.width - 1;
    const int y1 = clipped.y + clipped.height - 1;
    const int dx[4] = {clipped.x, x1, clipped.x, x1};
    const int dy[4] = {clipped.y, clipped.y, y1, y1};
    int sx[4] = {};
    int sy[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        int croppedX = 0;
        int croppedY = 0;
        rotateDisplayToCropped(rotation, crop.width, crop.height, dx[i], dy[i], croppedX, croppedY);
        if (adjustment.flipH)
            croppedX = crop.width - 1 - croppedX;
        if (adjustment.flipV)
            croppedY = crop.height - 1 - croppedY;
        sx[i] = crop.x + croppedX;
        sy[i] = crop.y + croppedY;
    }
    return boundsFromInclusiveCorners(sx, sy, sourceWidth, sourceHeight);
}

mviewer::domain::Selection mapSourceSelectionToDisplay(const mviewer::domain::Selection &source,
                                                       const AnalysisAdjustment &adjustment,
                                                       int sourceWidth, int sourceHeight)
{
    if (source.isEmpty() || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    const CropBounds crop = analysisCropBounds(sourceWidth, sourceHeight, adjustment);
    if (crop.width <= 0 || crop.height <= 0)
        return {};
    const auto clipped = mviewer::domain::normalizeSelection(
        source.x, source.y, source.x + source.width, source.y + source.height, crop.x + crop.width,
        crop.y + crop.height);
    const auto inCrop =
        mviewer::domain::normalizeSelection(clipped.x, clipped.y, clipped.x + clipped.width,
                                            clipped.y + clipped.height, sourceWidth, sourceHeight);
    if (inCrop.isEmpty())
        return {};
    const int left = std::max(inCrop.x, crop.x);
    const int top = std::max(inCrop.y, crop.y);
    const int right = std::min(inCrop.x + inCrop.width, crop.x + crop.width);
    const int bottom = std::min(inCrop.y + inCrop.height, crop.y + crop.height);
    if (right <= left || bottom <= top)
        return {};
    const int rotation = normalizedRotation(adjustment.rotation);
    const int displayWidth = (rotation == 90 || rotation == 270) ? crop.height : crop.width;
    const int displayHeight = (rotation == 90 || rotation == 270) ? crop.width : crop.height;
    const int x1 = right - 1;
    const int y1 = bottom - 1;
    const int sx[4] = {left, x1, left, x1};
    const int sy[4] = {top, top, y1, y1};
    int dx[4] = {};
    int dy[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        int cx = sx[i] - crop.x;
        int cy = sy[i] - crop.y;
        if (adjustment.flipH)
            cx = crop.width - 1 - cx;
        if (adjustment.flipV)
            cy = crop.height - 1 - cy;
        rotateCroppedToDisplay(rotation, crop.width, crop.height, cx, cy, dx[i], dy[i]);
    }
    return boundsFromInclusiveCorners(dx, dy, displayWidth, displayHeight);
}

NeighborhoodStats neighborhoodStats(const ImageData &source, const AnalysisAdjustment &adjustment,
                                    int adjustedX, int adjustedY, int n)
{
    NeighborhoodStats stats;
    if (source.isNull() || n < 1)
        return stats;

    const CropBounds crop = analysisCropBounds(source, adjustment);
    if (crop.width <= 0 || crop.height <= 0)
        return stats;

    const int rotation = normalizedRotation(adjustment.rotation);
    const int outputWidth = (rotation == 90 || rotation == 270) ? crop.height : crop.width;
    const int outputHeight = (rotation == 90 || rotation == 270) ? crop.width : crop.height;
    const AnalysisLUT lut = buildAnalysisLUT(adjustment, source.format);

    int64_t sum = 0, sumSq = 0;
    int64_t rSum = 0, gSum = 0, bSum = 0, vSum = 0;
    int minValue = 255, maxValue = 0;
    const int half = n / 2;
    for (int dy = -half; dy <= half; ++dy)
    {
        const int curY = adjustedY + dy;
        if (curY < 0 || curY >= outputHeight)
            continue;
        for (int dx = -half; dx <= half; ++dx)
        {
            const int curX = adjustedX + dx;
            if (curX < 0 || curX >= outputWidth)
                continue;

            int croppedX = curX, croppedY = curY;
            switch (rotation)
            {
            case 90:
                croppedX = curY;
                croppedY = crop.height - 1 - curX;
                break;
            case 180:
                croppedX = crop.width - 1 - curX;
                croppedY = crop.height - 1 - curY;
                break;
            case 270:
                croppedX = crop.width - 1 - curY;
                croppedY = curX;
                break;
            default:
                break;
            }

            if (adjustment.flipH)
                croppedX = crop.width - 1 - croppedX;
            if (adjustment.flipV)
                croppedY = crop.height - 1 - croppedY;

            const PixelRGBA sourcePixel = samplePixel(source, crop.x + croppedX, crop.y + croppedY);
            if (!sourcePixel.valid)
                continue;

            const int pr = lut.isIdentity ? sourcePixel.r : lut.r[sourcePixel.r];
            const int pg = lut.isIdentity ? sourcePixel.g : lut.g[sourcePixel.g];
            const int pb = lut.isIdentity ? sourcePixel.b : lut.b[sourcePixel.b];

            const int luminance = static_cast<int>(0.2126 * pr + 0.7152 * pg + 0.0722 * pb + 0.5);
            sum += luminance;
            sumSq += static_cast<int64_t>(luminance) * luminance;
            rSum += pr;
            gSum += pg;
            bSum += pb;
            vSum += std::max({pr, pg, pb});
            minValue = std::min(minValue, luminance);
            maxValue = std::max(maxValue, luminance);
            ++stats.count;
        }
    }
    if (stats.count == 0)
        return stats;

    stats.mean = static_cast<double>(sum) / stats.count;
    const double variance = static_cast<double>(sumSq) / stats.count - stats.mean * stats.mean;
    stats.variance = std::max(0.0, variance);
    stats.stdDev = std::sqrt(stats.variance);
    stats.min = minValue;
    stats.max = maxValue;
    stats.rMean = static_cast<double>(rSum) / stats.count;
    stats.gMean = static_cast<double>(gSum) / stats.count;
    stats.bMean = static_cast<double>(bSum) / stats.count;
    stats.vMean = static_cast<double>(vSum) / stats.count;
    return stats;
}

NeighborhoodStats neighborhoodStats(const uint8_t *data, int stride, int width, int height, int cx,
                                    int cy, int n, int channels)
{
    NeighborhoodStats s;
    if (!data || width <= 0 || height <= 0 || n < 1 || cx < 0 || cy < 0 || cx >= width ||
        cy >= height)
        return s;

    const int half = n / 2; // n=1→0, n=3→1, n=5→2, n=7→3
    const int yStart = std::max(0, cy - half);
    const int yEnd = std::min(height - 1, cy + half);
    const int xStart = std::max(0, cx - half);
    const int xEnd = std::min(width - 1, cx + half);
    if (yStart > yEnd || xStart > xEnd)
        return s;

    int64_t sum = 0, sumSq = 0;
    int64_t rSum = 0, gSum = 0, bSum = 0, vSum = 0;
    int mn = 255, mx = 0;
    const int count = (yEnd - yStart + 1) * (xEnd - xStart + 1);

    if (channels == 1)
    {
        for (int yy = yStart; yy <= yEnd; ++yy)
        {
            const uint8_t *row = data + static_cast<size_t>(yy) * stride;
            for (int xx = xStart; xx <= xEnd; ++xx)
            {
                const uint8_t v = row[xx];
                sum += v;
                sumSq += static_cast<int64_t>(v) * v;
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
            }
        }
        rSum = gSum = bSum = vSum = sum;
    }
    else if (channels == 4)
    {
        for (int yy = yStart; yy <= yEnd; ++yy)
        {
            const uint8_t *row = data + static_cast<size_t>(yy) * stride;
            for (int xx = xStart; xx <= xEnd; ++xx)
            {
                const uint8_t *p = row + static_cast<size_t>(xx) * 4;
                const uint8_t b = p[0];
                const uint8_t g = p[1];
                const uint8_t r = p[2];
                rSum += r;
                gSum += g;
                bSum += b;
                vSum += std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
                const double lum = luma(r, g, b);
                const int v = static_cast<int>(lum + 0.5);
                sum += v;
                sumSq += static_cast<int64_t>(v) * v;
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
            }
        }
    }
    else
    {
        for (int yy = yStart; yy <= yEnd; ++yy)
        {
            const uint8_t *row = data + static_cast<size_t>(yy) * stride;
            for (int xx = xStart; xx <= xEnd; ++xx)
            {
                const uint8_t *p = row + static_cast<size_t>(xx) * 3;
                const uint8_t r = p[0];
                const uint8_t g = p[1];
                const uint8_t b = p[2];
                rSum += r;
                gSum += g;
                bSum += b;
                vSum += std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
                const double lum = luma(r, g, b);
                const int v = static_cast<int>(lum + 0.5);
                sum += v;
                sumSq += static_cast<int64_t>(v) * v;
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
            }
        }
    }

    if (count == 0)
        return s;
    const double mean = static_cast<double>(sum) / count;
    const double var = static_cast<double>(sumSq) / count - mean * mean;
    s.mean = mean;
    s.variance = var > 0 ? var : 0.0;
    s.stdDev = std::sqrt(s.variance);
    s.min = mn;
    s.max = mx;
    s.count = count;
    s.rMean = static_cast<double>(rSum) / count;
    s.gMean = static_cast<double>(gSum) / count;
    s.bMean = static_cast<double>(bSum) / count;
    s.vMean = static_cast<double>(vSum) / count;
    return s;
}
} // namespace mviewer::core

// NOLINTEND(bugprone-easily-swappable-parameters,bugprone-incorrect-roundings,cppcoreguidelines-pro-type-vararg)
