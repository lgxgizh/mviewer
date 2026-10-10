#include "core/render/DisplayResample.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mviewer::core
{
namespace
{

constexpr int kMaxCatmullTaps = 16;
constexpr float kCatmullSupport = 2.f;
constexpr double kDensityEpsilon = 1e-4;

struct Rgb
{
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
};

struct SampleLayout
{
    const uint8_t *base = nullptr;
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

float catmullWeight(float x)
{
    x = std::fabs(x);
    if (x >= kCatmullSupport)
        return 0.f;
    if (x >= 1.f)
        return ((-0.5f * x + 2.5f) * x - 4.f) * x + 2.f;
    return (1.5f * x - 2.5f) * x * x + 1.f;
}

SampleLayout layoutFor(const ImageData &src)
{
    SampleLayout layout;
    if (src.isNull() || src.buffer == nullptr)
        return layout;
    layout.width = src.width;
    layout.height = src.height;
    layout.gray = src.format == PixelFormat::Grayscale8;
    switch (src.format)
    {
    case PixelFormat::RGB24:
        layout.channels = 3;
        layout.red = 0;
        break;
    case PixelFormat::RGBA32:
        layout.channels = 4;
        layout.red = 0;
        break;
    case PixelFormat::BGR24:
        layout.channels = 3;
        layout.red = 2;
        layout.blue = 0;
        break;
    case PixelFormat::BGRA32:
        layout.channels = 4;
        layout.red = 2;
        layout.blue = 0;
        break;
    case PixelFormat::Grayscale8:
        layout.channels = 1;
        break;
    default:
        return {};
    }
    const size_t need = static_cast<size_t>(layout.width) * static_cast<size_t>(layout.height) *
                        static_cast<size_t>(layout.channels);
    if (layout.channels <= 0 || src.buffer->size() < need)
        return {};
    layout.base = src.buffer->data();
    return layout;
}

Rgb loadRgb(const SampleLayout &layout, int x, int y)
{
    if (x < 0)
        x = 0;
    else if (x >= layout.width)
        x = layout.width - 1;
    if (y < 0)
        y = 0;
    else if (y >= layout.height)
        y = layout.height - 1;
    const uint8_t *pixel =
        layout.base +
        (static_cast<size_t>(y) * static_cast<size_t>(layout.width) + static_cast<size_t>(x)) *
            static_cast<size_t>(layout.channels);
    if (layout.gray)
        return {static_cast<float>(pixel[0]), static_cast<float>(pixel[0]),
                static_cast<float>(pixel[0])};
    return {static_cast<float>(pixel[layout.red]), static_cast<float>(pixel[layout.green]),
            static_cast<float>(pixel[layout.blue])};
}

uint8_t quantize(float value)
{
    if (value <= 0.f)
        return 0;
    if (value >= 255.f)
        return 255;
    return static_cast<uint8_t>(std::lround(value));
}

SourceRect clampSource(const SampleLayout &layout, const DisplayResampleRect &rect)
{
    SourceRect clamped;
    if (layout.base == nullptr || !rect.isValid() || layout.width <= 0 || layout.height <= 0)
        return clamped;
    const int left = (std::max)(0, rect.x);
    const int top = (std::max)(0, rect.y);
    const int right = (std::min)(layout.width, rect.x + rect.width);
    const int bottom = (std::min)(layout.height, rect.y + rect.height);
    if (right <= left || bottom <= top)
        return clamped;
    clamped.x = left;
    clamped.y = top;
    clamped.width = right - left;
    clamped.height = bottom - top;
    return clamped;
}

ImageData copyIdentity(const SampleLayout &layout, const SourceRect &rect)
{
    ImageData out = makeImageData(rect.width, rect.height, PixelFormat::RGB24);
    uint8_t *dst = out.buffer->data();
    for (int y = 0; y < rect.height; ++y)
    {
        uint8_t *row = dst + static_cast<size_t>(y) * static_cast<size_t>(rect.width) * 3u;
        for (int x = 0; x < rect.width; ++x)
        {
            const Rgb pixel = loadRgb(layout, rect.x + x, rect.y + y);
            uint8_t *outPx = row + static_cast<size_t>(x) * 3u;
            outPx[0] = static_cast<uint8_t>(pixel.r);
            outPx[1] = static_cast<uint8_t>(pixel.g);
            outPx[2] = static_cast<uint8_t>(pixel.b);
        }
    }
    return out;
}

int intermediateExtent(int src, int dst)
{
    if (src <= 0 || dst <= 0)
        return 0;
    if (dst >= src)
        return src;
    // Leave about 2x for the sharp kernel so a wide box does not blur alone.
    if (src >= dst * 2)
    {
        const int twice = dst * 2;
        return twice < src ? twice : src;
    }
    return src;
}

float spanAverage(const float *samples, const float *prefix, int srcCount, int channel,
                  double start, double end)
{
    const double span = end - start;
    if (!(span > 0.0))
        return 0.f;
    int first = static_cast<int>(std::floor(start));
    int last = static_cast<int>(std::ceil(end - 1e-9)) - 1;
    if (first < 0)
        first = 0;
    if (last >= srcCount)
        last = srcCount - 1;
    if (first > last)
        return 0.f;

    double sum = 0.0;
    const double leftEnd = (std::min)(end, static_cast<double>(first + 1));
    const double leftFrac = leftEnd - (std::max)(start, static_cast<double>(first));
    if (leftFrac > 0.0)
        sum += leftFrac * samples[static_cast<size_t>(first) * 3u + static_cast<size_t>(channel)];
    if (last == first)
        return static_cast<float>(sum / span);

    const double lastLo = static_cast<double>(last);
    const bool lastPartial = end < lastLo + 1.0;
    const int fullEnd = lastPartial ? last : last + 1;
    if (fullEnd > first + 1)
    {
        sum += prefix[static_cast<size_t>(fullEnd) * 3u + static_cast<size_t>(channel)] -
               prefix[static_cast<size_t>(first + 1) * 3u + static_cast<size_t>(channel)];
    }
    if (lastPartial)
    {
        const double rightFrac = end - lastLo;
        if (rightFrac > 0.0)
            sum +=
                rightFrac * samples[static_cast<size_t>(last) * 3u + static_cast<size_t>(channel)];
    }
    return static_cast<float>(sum / span);
}

void boxHorizontal(const SampleLayout &layout, int x0, int y, int srcW, int dstW, float *dst,
                   std::vector<float> &samples, std::vector<float> &prefix)
{
    samples.resize(static_cast<size_t>(srcW) * 3u);
    prefix.assign(static_cast<size_t>(srcW + 1) * 3u, 0.f);
    for (int x = 0; x < srcW; ++x)
    {
        const Rgb pixel = loadRgb(layout, x0 + x, y);
        const size_t i = static_cast<size_t>(x) * 3u;
        samples[i] = pixel.r;
        samples[i + 1] = pixel.g;
        samples[i + 2] = pixel.b;
        prefix[i + 3] = prefix[i] + pixel.r;
        prefix[i + 4] = prefix[i + 1] + pixel.g;
        prefix[i + 5] = prefix[i + 2] + pixel.b;
    }
    for (int x = 0; x < dstW; ++x)
    {
        const double start = static_cast<double>(x) * static_cast<double>(srcW) / dstW;
        const double end = static_cast<double>(x + 1) * static_cast<double>(srcW) / dstW;
        float *out = dst + static_cast<size_t>(x) * 3u;
        out[0] = spanAverage(samples.data(), prefix.data(), srcW, 0, start, end);
        out[1] = spanAverage(samples.data(), prefix.data(), srcW, 1, start, end);
        out[2] = spanAverage(samples.data(), prefix.data(), srcW, 2, start, end);
    }
}

void boxToPlane(const SampleLayout &layout, const SourceRect &rect, int dstW, int dstH, float *dst,
                DisplayResampleScratch &scratch)
{
    scratch.row.resize(static_cast<size_t>(dstW) * 3u);
    scratch.accum.assign(static_cast<size_t>(dstW) * 3u, 0.f);
    for (int y = 0; y < dstH; ++y)
    {
        const double y0 = static_cast<double>(y) * static_cast<double>(rect.height) / dstH;
        const double y1 = static_cast<double>(y + 1) * static_cast<double>(rect.height) / dstH;
        std::fill(scratch.accum.begin(), scratch.accum.end(), 0.f);
        int row0 = static_cast<int>(std::floor(y0));
        int row1 = static_cast<int>(std::ceil(y1 - 1e-9)) - 1;
        if (row0 < 0)
            row0 = 0;
        if (row1 >= rect.height)
            row1 = rect.height - 1;
        for (int sy = row0; sy <= row1; ++sy)
        {
            const double lo = (std::max)(y0, static_cast<double>(sy));
            const double hi = (std::min)(y1, static_cast<double>(sy + 1));
            const float coverage = static_cast<float>(hi - lo);
            if (!(coverage > 0.f))
                continue;
            boxHorizontal(layout, rect.x, rect.y + sy, rect.width, dstW, scratch.row.data(),
                          scratch.samples, scratch.prefix);
            for (size_t i = 0; i < scratch.accum.size(); ++i)
                scratch.accum[i] += coverage * scratch.row[i];
        }
        const float span = static_cast<float>(y1 - y0);
        float *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        for (int x = 0; x < dstW * 3; ++x)
            out[x] = span > 0.f ? scratch.accum[static_cast<size_t>(x)] / span : 0.f;
    }
}

bool absorbTap(int index, float weight, int *indices, float *weights, int &count, double &sum)
{
    for (int i = 0; i < count; ++i)
    {
        if (indices[i] != index)
            continue;
        weights[i] += weight;
        sum += static_cast<double>(weight);
        return true;
    }
    if (count >= kMaxCatmullTaps)
        return false;
    indices[count] = index;
    weights[count] = weight;
    sum += static_cast<double>(weight);
    ++count;
    return true;
}

int buildPixelTaps(int srcCount, int dstIndex, int dstCount, int *indices, float *weights)
{
    const double inv = dstCount < srcCount ? static_cast<double>(srcCount) / dstCount : 1.0;
    const double center = (static_cast<double>(dstIndex) + 0.5) * static_cast<double>(srcCount) /
                          static_cast<double>(dstCount);
    const double radius = static_cast<double>(kCatmullSupport) * inv;
    const int begin = static_cast<int>(std::floor(center - radius));
    const int end = static_cast<int>(std::ceil(center + radius));
    int count = 0;
    double sum = 0.0;
    for (int i = begin; i < end; ++i)
    {
        const double kernelX = ((static_cast<double>(i) + 0.5) - center) / inv;
        const float weight = catmullWeight(static_cast<float>(kernelX));
        if (weight == 0.f)
            continue;
        int clamped = i;
        if (clamped < 0)
            clamped = 0;
        else if (clamped >= srcCount)
            clamped = srcCount - 1;
        absorbTap(clamped, weight, indices, weights, count, sum);
    }
    if (!(sum > 0.0) || count <= 0)
        return 0;
    const float norm = static_cast<float>(sum);
    for (int i = 0; i < count; ++i)
        weights[i] /= norm;
    return count;
}

void buildAxisTaps(int srcCount, int dstCount, std::vector<int> &offset, std::vector<int> &index,
                   std::vector<float> &weight)
{
    offset.assign(static_cast<size_t>(dstCount) + 1u, 0);
    index.clear();
    weight.clear();
    index.reserve(static_cast<size_t>(dstCount) * 4u);
    weight.reserve(static_cast<size_t>(dstCount) * 4u);
    int packedIndex[kMaxCatmullTaps];
    float packedWeight[kMaxCatmullTaps];
    for (int i = 0; i < dstCount; ++i)
    {
        const int count = buildPixelTaps(srcCount, i, dstCount, packedIndex, packedWeight);
        offset[static_cast<size_t>(i)] = static_cast<int>(index.size());
        for (int t = 0; t < count; ++t)
        {
            index.push_back(packedIndex[t]);
            weight.push_back(packedWeight[t]);
        }
    }
    offset[static_cast<size_t>(dstCount)] = static_cast<int>(index.size());
}

template <typename Load>
ImageData catmullResize(Load load, int srcW, int srcH, int dstW, int dstH,
                        DisplayResampleScratch &scratch)
{
    buildAxisTaps(srcW, dstW, scratch.tapOffset, scratch.tapIndex, scratch.tapWeight);
    buildAxisTaps(srcH, dstH, scratch.tapOffsetY, scratch.tapIndexY, scratch.tapWeightY);

    ImageData out = makeImageData(dstW, dstH, PixelFormat::RGB24);
    uint8_t *dst = out.buffer->data();
    scratch.row.assign(static_cast<size_t>(dstW) * 3u, 0.f);
    scratch.accum.assign(static_cast<size_t>(dstW) * 3u, 0.f);

    for (int y = 0; y < dstH; ++y)
    {
        std::fill(scratch.accum.begin(), scratch.accum.end(), 0.f);
        const int yBegin = scratch.tapOffsetY[static_cast<size_t>(y)];
        const int yEnd = scratch.tapOffsetY[static_cast<size_t>(y) + 1u];
        for (int tap = yBegin; tap < yEnd; ++tap)
        {
            const int sy = scratch.tapIndexY[static_cast<size_t>(tap)];
            const float wy = scratch.tapWeightY[static_cast<size_t>(tap)];
            for (int x = 0; x < dstW; ++x)
            {
                float r = 0.f;
                float g = 0.f;
                float b = 0.f;
                const int xBegin = scratch.tapOffset[static_cast<size_t>(x)];
                const int xEnd = scratch.tapOffset[static_cast<size_t>(x) + 1u];
                for (int xt = xBegin; xt < xEnd; ++xt)
                {
                    const Rgb sample = load(scratch.tapIndex[static_cast<size_t>(xt)], sy);
                    const float wx = scratch.tapWeight[static_cast<size_t>(xt)];
                    r += wx * sample.r;
                    g += wx * sample.g;
                    b += wx * sample.b;
                }
                const size_t i = static_cast<size_t>(x) * 3u;
                scratch.row[i] = r;
                scratch.row[i + 1] = g;
                scratch.row[i + 2] = b;
            }
            for (size_t i = 0; i < scratch.accum.size(); ++i)
                scratch.accum[i] += wy * scratch.row[i];
        }
        uint8_t *row = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        for (int x = 0; x < dstW * 3; ++x)
            row[x] = quantize(scratch.accum[static_cast<size_t>(x)]);
    }
    return out;
}

ImageData catmullFromImage(const SampleLayout &layout, const SourceRect &rect, int dstW, int dstH,
                           DisplayResampleScratch &scratch)
{
    const int x0 = rect.x;
    const int y0 = rect.y;
    const int srcW = rect.width;
    const int srcH = rect.height;
    return catmullResize([&](int x, int y) { return loadRgb(layout, x0 + x, y0 + y); }, srcW, srcH,
                         dstW, dstH, scratch);
}

ImageData catmullFromPlane(const float *plane, int srcW, int srcH, int dstW, int dstH,
                           DisplayResampleScratch &scratch)
{
    return catmullResize(
        [&](int x, int y)
        {
            if (x < 0)
                x = 0;
            else if (x >= srcW)
                x = srcW - 1;
            if (y < 0)
                y = 0;
            else if (y >= srcH)
                y = srcH - 1;
            const float *p =
                plane +
                (static_cast<size_t>(y) * static_cast<size_t>(srcW) + static_cast<size_t>(x)) * 3u;
            return Rgb{p[0], p[1], p[2]};
        },
        srcW, srcH, dstW, dstH, scratch);
}

bool sameLevel(const DisplayRasterLevel &a, const DisplayRasterLevel &b)
{
    return a.targetWidth == b.targetWidth && a.targetHeight == b.targetHeight &&
           a.coveredSourceWidth == b.coveredSourceWidth &&
           a.coveredSourceHeight == b.coveredSourceHeight && a.originX == b.originX &&
           a.originY == b.originY;
}

bool matchesDesiredSize(const DisplayRasterLevel &level, const DisplayRasterLevel &desired)
{
    return desired.targetWidth > 0 && desired.targetHeight > 0 &&
           level.targetWidth == desired.targetWidth && level.targetHeight == desired.targetHeight &&
           level.coveredSourceWidth == desired.coveredSourceWidth &&
           level.coveredSourceHeight == desired.coveredSourceHeight;
}

bool coversDesired(const DisplayRasterLevel &level, const DisplayRasterLevel &desired)
{
    if (desired.coveredSourceWidth <= 0 || desired.coveredSourceHeight <= 0)
        return true;
    if (level.coveredSourceWidth <= 0 || level.coveredSourceHeight <= 0)
        return false;
    const int right = level.originX + level.coveredSourceWidth;
    const int bottom = level.originY + level.coveredSourceHeight;
    const int wantRight = desired.originX + desired.coveredSourceWidth;
    const int wantBottom = desired.originY + desired.coveredSourceHeight;
    return level.originX <= desired.originX && level.originY <= desired.originY &&
           right >= wantRight && bottom >= wantBottom;
}

bool standInAllowed(const DisplayRasterLevel &level, const DisplayRasterLevel &shown,
                    bool haveShown, double shownDensity, const DisplayRasterLevel &desired)
{
    const double density = displayRasterDensity(level);
    if (!(density > 0.0))
        return false;
    if (haveShown && density + kDensityEpsilon < shownDensity)
        return false;
    if (haveShown && sameLevel(level, shown))
        return false;
    return coversDesired(level, desired);
}

bool preferStandIn(const DisplayRasterLevel &level, double density, const DisplayRasterLevel &best,
                   double bestDensity, const DisplayRasterLevel &desired)
{
    if (density > bestDensity + kDensityEpsilon)
        return true;
    if (density + kDensityEpsilon < bestDensity)
        return false;
    return matchesDesiredSize(level, desired) && !matchesDesiredSize(best, desired);
}

} // namespace

double displayRasterDensity(const DisplayRasterLevel &level) noexcept
{
    if (level.targetWidth <= 0 || level.targetHeight <= 0 || level.coveredSourceWidth <= 0 ||
        level.coveredSourceHeight <= 0)
        return 0.0;
    const double sx = static_cast<double>(level.targetWidth) / level.coveredSourceWidth;
    const double sy = static_cast<double>(level.targetHeight) / level.coveredSourceHeight;
    return sx < sy ? sx : sy;
}

bool isBlurrierDisplayStandIn(const DisplayRasterLevel &shown,
                              const DisplayRasterLevel &incoming) noexcept
{
    const double shownDensity = displayRasterDensity(shown);
    if (!(shownDensity > 0.0))
        return false;
    const double incomingDensity = displayRasterDensity(incoming);
    if (!(incomingDensity > 0.0))
        return true;
    return incomingDensity + kDensityEpsilon < shownDensity;
}

int selectDisplayStandIn(const DisplayRasterLevel *levels, int count,
                         const DisplayRasterLevel &shown, const DisplayRasterLevel &desired)
{
    if (levels == nullptr || count <= 0)
        return -1;
    const double shownDensity = displayRasterDensity(shown);
    const bool haveShown = shownDensity > 0.0;
    int best = -1;
    double bestDensity = -1.0;
    for (int i = 0; i < count; ++i)
    {
        if (!standInAllowed(levels[i], shown, haveShown, shownDensity, desired))
            continue;
        const double density = displayRasterDensity(levels[i]);
        if (best >= 0 && !preferStandIn(levels[i], density, levels[best], bestDensity, desired))
            continue;
        best = i;
        bestDensity = density;
    }
    return best;
}

ImageData resampleDisplay(const ImageData &src, const DisplayResampleRequest &request,
                          DisplayResampleScratch *scratch)
{
    if (request.targetWidth <= 0 || request.targetHeight <= 0)
        return {};
    const SampleLayout layout = layoutFor(src);
    const SourceRect rect = clampSource(layout, request.source);
    if (!rect.isValid())
        return {};
    DisplayResampleScratch local;
    DisplayResampleScratch &work = scratch != nullptr ? *scratch : local;
    if (rect.width == request.targetWidth && rect.height == request.targetHeight)
        return copyIdentity(layout, rect);

    const int midW = intermediateExtent(rect.width, request.targetWidth);
    const int midH = intermediateExtent(rect.height, request.targetHeight);
    if (midW == rect.width && midH == rect.height)
        return catmullFromImage(layout, rect, request.targetWidth, request.targetHeight, work);

    work.plane.resize(static_cast<size_t>(midW) * static_cast<size_t>(midH) * 3u);
    boxToPlane(layout, rect, midW, midH, work.plane.data(), work);
    if (midW == request.targetWidth && midH == request.targetHeight)
    {
        ImageData out = makeImageData(midW, midH, PixelFormat::RGB24);
        uint8_t *dst = out.buffer->data();
        for (size_t i = 0; i < work.plane.size(); ++i)
            dst[i] = quantize(work.plane[i]);
        return out;
    }
    return catmullFromPlane(work.plane.data(), midW, midH, request.targetWidth,
                            request.targetHeight, work);
}

} // namespace mviewer::core
