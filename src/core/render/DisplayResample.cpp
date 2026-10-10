#include "core/render/DisplayResampleDetail.h"

#include "core/simd/CpuFeatures.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

namespace mviewer::core::resample_detail
{

static std::atomic<int> g_isaOverride{0};

namespace
{

float catmullWeight(float x)
{
    x = std::fabs(x);
    if (x >= 2.f)
        return 0.f;
    if (x >= 1.f)
        return ((-0.5f * x + 2.5f) * x - 4.f) * x + 2.f;
    return (1.5f * x - 2.5f) * x * x + 1.f;
}

struct TapList
{
    int origin = 0;
    int count = 0;
    float weight[kMaxTaps] = {};
    double sum = 0.0;
};

void collectTaps(TapList &taps, int srcCount, int dstIndex, int dstCount)
{
    const double inv =
        dstCount < srcCount ? static_cast<double>(srcCount) / static_cast<double>(dstCount) : 1.0;
    const double center = (static_cast<double>(dstIndex) + 0.5) * static_cast<double>(srcCount) /
                          static_cast<double>(dstCount);
    const double radius = 2.0 * inv;
    const int begin = static_cast<int>(std::floor(center - radius));
    const int end = static_cast<int>(std::ceil(center + radius));
    float raw[kMaxTaps] = {};
    int n = 0;
    int first = -1;
    int last = -1;
    for (int i = begin; i < end && n < kMaxTaps; ++i)
    {
        const double kernelX = ((static_cast<double>(i) + 0.5) - center) / inv;
        const float weight = catmullWeight(static_cast<float>(kernelX));
        raw[n] = weight;
        if (weight != 0.f)
        {
            if (first < 0)
                first = n;
            last = n;
        }
        ++n;
    }
    if (first < 0)
        return;
    taps.origin = begin + first;
    taps.count = last - first + 1;
    for (int i = 0; i < taps.count; ++i)
    {
        taps.weight[i] = raw[first + i];
        taps.sum += static_cast<double>(taps.weight[i]);
    }
}

void storeQuantized(AxisKernel &kernel, int dstIndex, const TapList &taps, int srcCount)
{
    int16_t *out = kernel.weight.data() + static_cast<size_t>(dstIndex) * kMaxTaps;
    if (taps.count <= 0 || !(taps.sum > 0.0))
    {
        kernel.origin[static_cast<size_t>(dstIndex)] = clampIndex(dstIndex, srcCount);
        kernel.count[static_cast<size_t>(dstIndex)] = 1;
        out[0] = static_cast<int16_t>(kWeightOne);
        return;
    }
    int quantized[kMaxTaps] = {};
    int sum = 0;
    int largest = 0;
    int magnitude = -1;
    for (int tap = 0; tap < taps.count; ++tap)
    {
        const double scaled =
            static_cast<double>(taps.weight[tap]) / taps.sum * static_cast<double>(kWeightOne);
        quantized[tap] = static_cast<int>(std::lround(scaled));
        sum += quantized[tap];
        const int absWeight = quantized[tap] < 0 ? -quantized[tap] : quantized[tap];
        if (absWeight > magnitude)
        {
            magnitude = absWeight;
            largest = tap;
        }
    }
    quantized[largest] += kWeightOne - sum;
    if (quantized[largest] > 32767)
        quantized[largest] = 32767;
    if (quantized[largest] < -32768)
        quantized[largest] = -32768;
    kernel.origin[static_cast<size_t>(dstIndex)] = taps.origin;
    kernel.count[static_cast<size_t>(dstIndex)] = taps.count;
    for (int tap = 0; tap < taps.count; ++tap)
        out[tap] = static_cast<int16_t>(quantized[tap]);
}

} // namespace

RgbView viewFromLayout(const SampleLayout &layout, const SourceRect &rect)
{
    RgbView view;
    view.width = rect.width;
    view.height = rect.height;
    view.stride = layout.width;
    view.channels = layout.channels;
    view.red = layout.red;
    view.green = layout.green;
    view.blue = layout.blue;
    view.gray = layout.gray;
    view.keep = layout.buffer;
    const size_t offset = (static_cast<size_t>(rect.y) * static_cast<size_t>(layout.width) +
                           static_cast<size_t>(rect.x)) *
                          static_cast<size_t>(layout.channels);
    view.data = layout.base + offset;
    return view;
}

int channelOffset(const RgbView &view, int channel)
{
    if (view.gray)
        return 0;
    if (channel == 1)
        return view.green;
    if (channel == 2)
        return view.blue;
    return view.red;
}

void copyRgbView(const RgbView &view, uint8_t *dst)
{
    const bool packed = !view.gray && view.channels == 3 && view.red == 0 && view.green == 1 &&
                        view.blue == 2 && view.stride == view.width;
    if (packed)
    {
        const size_t bytes =
            static_cast<size_t>(view.width) * static_cast<size_t>(view.height) * 3u;
        std::memcpy(dst, view.data, bytes);
        return;
    }
    for (int y = 0; y < view.height; ++y)
    {
        const uint8_t *row = view.data + static_cast<size_t>(y) * static_cast<size_t>(view.stride) *
                                             static_cast<size_t>(view.channels);
        uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(view.width) * 3u;
        for (int x = 0; x < view.width; ++x)
        {
            const uint8_t *pixel =
                row + static_cast<size_t>(x) * static_cast<size_t>(view.channels);
            out[static_cast<size_t>(x) * 3u] = pixel[channelOffset(view, 0)];
            out[static_cast<size_t>(x) * 3u + 1u] = pixel[channelOffset(view, 1)];
            out[static_cast<size_t>(x) * 3u + 2u] = pixel[channelOffset(view, 2)];
        }
    }
}

struct KernelScratch
{
    AxisKernel x;
    AxisKernel y;
    DisplayResampleScratch *scratch = nullptr;

    explicit KernelScratch(DisplayResampleScratch *held) : scratch(held)
    {
        if (scratch == nullptr)
            return;
        exchangeKernel(x, scratch->originX, scratch->countX, scratch->weightX);
        exchangeKernel(y, scratch->originY, scratch->countY, scratch->weightY);
    }

    ~KernelScratch()
    {
        if (scratch == nullptr)
            return;
        exchangeKernel(x, scratch->originX, scratch->countX, scratch->weightX);
        exchangeKernel(y, scratch->originY, scratch->countY, scratch->weightY);
    }

    KernelScratch(const KernelScratch &) = delete;
    KernelScratch &operator=(const KernelScratch &) = delete;
};

bool filterSeparable(const RgbView &view, uint8_t *dst, int dstW, int dstH,
                     DisplayResampleScratch *scratch, const std::atomic<bool> *cancel)
{
    KernelScratch kernels(scratch);
    buildAxisKernel(kernels.x, view.width, dstW);
    buildAxisKernel(kernels.y, view.height, dstH);
    return resizeSeparable(view, dst, dstW, dstH, kernels.x, kernels.y, cancel);
}

Isa activeIsa() noexcept
{
    const int forced = g_isaOverride.load(std::memory_order_relaxed);
    if (forced == 1)
        return Isa::Scalar;
    if (forced == 2)
        return CpuFeatures::hasSse41() ? Isa::Sse41 : Isa::Scalar;
    if (forced == 3)
    {
        if (CpuFeatures::hasAvx2())
            return Isa::Avx2;
        if (CpuFeatures::hasSse41())
            return Isa::Sse41;
        return Isa::Scalar;
    }
    if (CpuFeatures::hasAvx2())
        return Isa::Avx2;
    if (CpuFeatures::hasSse41())
        return Isa::Sse41;
    return Isa::Scalar;
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
    layout.buffer = src.buffer;
    layout.base = src.buffer->data();
    return layout;
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

bool resampleCancelled(const std::atomic<bool> *cancel) noexcept
{
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

void buildAxisKernel(AxisKernel &kernel, int srcCount, int dstCount)
{
    kernel.srcCount = srcCount;
    kernel.dstCount = dstCount;
    if (srcCount <= 0 || dstCount <= 0)
    {
        kernel.origin.clear();
        kernel.count.clear();
        kernel.weight.clear();
        return;
    }
    kernel.origin.assign(static_cast<size_t>(dstCount), 0);
    kernel.count.assign(static_cast<size_t>(dstCount), 0);
    kernel.weight.assign(static_cast<size_t>(dstCount) * static_cast<size_t>(kMaxTaps), 0);
    for (int i = 0; i < dstCount; ++i)
    {
        TapList taps;
        collectTaps(taps, srcCount, i, dstCount);
        storeQuantized(kernel, i, taps, srcCount);
    }
}

void exchangeKernel(AxisKernel &kernel, std::vector<int> &origin, std::vector<int> &count,
                    std::vector<int16_t> &weight)
{
    kernel.origin.swap(origin);
    kernel.count.swap(count);
    kernel.weight.swap(weight);
}

} // namespace mviewer::core::resample_detail

namespace mviewer::core
{
namespace
{

constexpr double kDensityEpsilon = 1e-4;

using resample_detail::SampleLayout;
using resample_detail::SourceRect;

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

ImageData imageFromView(const resample_detail::RgbView &view)
{
    ImageData out = makeImageData(view.width, view.height, PixelFormat::RGB24);
    if (out.isNull() || out.buffer == nullptr)
        return {};
    resample_detail::copyRgbView(view, out.buffer->data());
    return out;
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

void releaseDisplayResampleCache() noexcept
{
    resample_detail::releasePyramidCache();
}

void setDisplayResampleIsaForTest(int isa) noexcept
{
    resample_detail::g_isaOverride.store(isa, std::memory_order_relaxed);
}

ImageData resampleDisplay(const ImageData &src, const DisplayResampleRequest &request,
                          DisplayResampleScratch *scratch, const std::atomic<bool> *cancel)
{
    if (request.targetWidth <= 0 || request.targetHeight <= 0 ||
        resample_detail::resampleCancelled(cancel))
        return {};
    const SampleLayout layout = resample_detail::layoutFor(src);
    const SourceRect rect = resample_detail::clampSource(layout, request.source);
    if (!rect.isValid())
        return {};
    resample_detail::RgbView view = resample_detail::viewFromLayout(layout, rect);
    if (rect.width == request.targetWidth && rect.height == request.targetHeight)
        return imageFromView(view);
    if (!resample_detail::reduceByBox(view, layout, rect, request.targetWidth, request.targetHeight,
                                      cancel) ||
        resample_detail::resampleCancelled(cancel))
        return {};
    if (view.width == request.targetWidth && view.height == request.targetHeight)
        return imageFromView(view);

    ImageData out = makeImageData(request.targetWidth, request.targetHeight, PixelFormat::RGB24);
    if (out.isNull() || out.buffer == nullptr)
        return {};
    bool ok = false;
    if (request.quality == DisplayResampleQuality::Preview)
        ok = resample_detail::resizeBilinear(view, out.buffer->data(), request.targetWidth,
                                             request.targetHeight, cancel);
    else
        ok = resample_detail::filterSeparable(view, out.buffer->data(), request.targetWidth,
                                              request.targetHeight, scratch, cancel);
    if (!ok || resample_detail::resampleCancelled(cancel))
        return {};
    return out;
}

} // namespace mviewer::core
