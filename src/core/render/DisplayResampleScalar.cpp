#include "core/render/DisplayResampleDetail.h"

#include <array>
#include <cmath>
#include <memory>
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

constexpr int kLinearMax = 65535;
constexpr int kCodeMax = 255;
constexpr double kSrgbEotfKnee = 0.04045;
constexpr double kSrgbOetfKnee = 0.0031308;
constexpr double kSrgbSlope = 12.92;
constexpr double kSrgbOffset = 0.055;
constexpr double kSrgbA = 1.055;
constexpr double kSrgbGamma = 2.4;

double transferEotf(double encoded)
{
    if (encoded <= kSrgbEotfKnee)
        return encoded / kSrgbSlope;
    const double base = (encoded + kSrgbOffset) / kSrgbA;
    return std::pow(base, kSrgbGamma);
}

double transferOetf(double linear)
{
    if (linear <= kSrgbOetfKnee)
        return kSrgbSlope * linear;
    return kSrgbA * std::pow(linear, 1.0 / kSrgbGamma) - kSrgbOffset;
}

int quantizeUnit(double unit, int scale)
{
    long quantized = std::lround(unit * static_cast<double>(scale));
    if (quantized < 0)
        quantized = 0;
    if (quantized > scale)
        quantized = scale;
    return static_cast<int>(quantized);
}

struct SrgbTables
{
    std::array<uint16_t, kCodeMax + 1> toLinear{};
    std::array<uint8_t, kLinearMax + 1> toCode{};
};

void fillSrgbTables(SrgbTables &tables)
{
    for (int code = 0; code <= kCodeMax; ++code)
    {
        const double encoded = static_cast<double>(code) / static_cast<double>(kCodeMax);
        tables.toLinear[static_cast<size_t>(code)] =
            static_cast<uint16_t>(quantizeUnit(transferEotf(encoded), kLinearMax));
    }
    tables.toLinear[0] = 0;
    tables.toLinear[kCodeMax] = static_cast<uint16_t>(kLinearMax);
    for (int linear = 0; linear <= kLinearMax; ++linear)
    {
        const double unit = static_cast<double>(linear) / static_cast<double>(kLinearMax);
        tables.toCode[static_cast<size_t>(linear)] =
            static_cast<uint8_t>(quantizeUnit(transferOetf(unit), kCodeMax));
    }
    for (int code = 0; code <= kCodeMax; ++code)
    {
        const size_t slot = tables.toLinear[static_cast<size_t>(code)];
        tables.toCode[slot] = static_cast<uint8_t>(code);
    }
}

const SrgbTables &srgbTables()
{
    static const std::shared_ptr<SrgbTables> tables = []()
    {
        auto built = std::make_shared<SrgbTables>();
        fillSrgbTables(*built);
        return built;
    }();
    return *tables;
}

uint16_t srgbCodeToLinear(uint8_t code)
{
    return srgbTables().toLinear[code];
}

uint8_t linearToSrgbCode(uint16_t linear)
{
    return srgbTables().toCode[linear];
}

int roundAvg2(int a, int b)
{
    return (a + b + 1) >> 1;
}

bool needsLinearHalve(int src, int dst)
{
    return dst > 0 && src >= 2 && static_cast<long long>(src) >= static_cast<long long>(dst) * 2LL;
}

void linearAxisSpan(int srcCount, int dstIndex, bool half, int &lo, int &hi)
{
    if (!half)
    {
        lo = dstIndex;
        hi = dstIndex;
        return;
    }
    const int dstCount = srcCount >> 1;
    if ((srcCount & 1) != 0 && dstIndex == dstCount - 1)
    {
        lo = srcCount - 3;
        if (lo < 0)
            lo = 0;
        hi = srcCount - 1;
        return;
    }
    lo = dstIndex * 2;
    hi = lo + 1;
}

template <typename Sampler>
int meanRect(const Sampler &sample, int x0, int x1, int y0, int y1, int channel)
{
    int sum = 0;
    int n = 0;
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            sum += sample(x, y, channel);
            ++n;
        }
    }
    if (n <= 0)
        return 0;
    return (sum + n / 2) / n;
}

struct CodeSampler
{
    const RgbView *view = nullptr;

    int operator()(int x, int y, int channel) const
    {
        x = clampIndex(x, view->width);
        y = clampIndex(y, view->height);
        const int code = sampleChannel(rowPointer(*view, y), *view, x, channel);
        return srgbCodeToLinear(static_cast<uint8_t>(code));
    }
};

struct PlaneSampler
{
    const LinearRgb *view = nullptr;

    int operator()(int x, int y, int channel) const
    {
        x = clampIndex(x, view->width);
        y = clampIndex(y, view->height);
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(view->stride);
        const uint16_t *pixel = view->data + (row + static_cast<size_t>(x)) * 3u;
        return pixel[channel];
    }
};

template <typename Sampler>
void storeBoxPixel(const Sampler &sample, uint16_t *pixel, int sx0, int sx1, int sy0, int sy1)
{
    for (int channel = 0; channel < 3; ++channel)
    {
        const int top = roundAvg2(sample(sx0, sy0, channel), sample(sx1, sy0, channel));
        const int bottom = roundAvg2(sample(sx0, sy1, channel), sample(sx1, sy1, channel));
        pixel[channel] = static_cast<uint16_t>(roundAvg2(top, bottom));
    }
}

template <typename Sampler>
void writeLinearBody(const Sampler &sample, uint16_t *dst, int srcW, int dstW, bool halfX,
                     bool halfY, int y0, int y1)
{
    const int bodyW = (halfX && (srcW & 1) != 0) ? dstW - 1 : dstW;
    for (int y = y0; y < y1; ++y)
    {
        const int sy0 = halfY ? y * 2 : y;
        const int sy1 = halfY ? sy0 + 1 : sy0;
        uint16_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        for (int x = 0; x < bodyW; ++x)
        {
            const int sx0 = halfX ? x * 2 : x;
            const int sx1 = halfX ? sx0 + 1 : sx0;
            storeBoxPixel(sample, out + static_cast<size_t>(x) * 3u, sx0, sx1, sy0, sy1);
        }
    }
}

template <typename Sampler>
void storeLinearEdge(const Sampler &sample, uint16_t *dst, int dstW, int srcW, int srcH, int x,
                     int y, bool halfX, bool halfY)
{
    int x0 = 0;
    int x1 = 0;
    int y0 = 0;
    int y1 = 0;
    linearAxisSpan(srcW, x, halfX, x0, x1);
    linearAxisSpan(srcH, y, halfY, y0, y1);
    uint16_t *pixel =
        dst + (static_cast<size_t>(y) * static_cast<size_t>(dstW) + static_cast<size_t>(x)) * 3u;
    for (int channel = 0; channel < 3; ++channel)
        pixel[channel] = static_cast<uint16_t>(meanRect(sample, x0, x1, y0, y1, channel));
}

template <typename Sampler>
void writeLinearEdges(const Sampler &sample, uint16_t *dst, int srcW, int srcH, int dstW, int dstH,
                      bool halfX, bool halfY)
{
    const bool oddX = halfX && (srcW & 1) != 0;
    const bool oddY = halfY && (srcH & 1) != 0;
    if (oddX)
    {
        const int x = dstW - 1;
        for (int y = 0; y < dstH; ++y)
            storeLinearEdge(sample, dst, dstW, srcW, srcH, x, y, halfX, halfY);
    }
    if (!oddY)
        return;
    const int y = dstH - 1;
    const int xEnd = oddX ? dstW - 1 : dstW;
    for (int x = 0; x < xEnd; ++x)
        storeLinearEdge(sample, dst, dstW, srcW, srcH, x, y, halfX, halfY);
}

template <typename Sampler>
bool boxLinearRows(const Sampler &sample, int srcW, int srcH, uint16_t *dst, int dstW, int dstH,
                   bool halfX, bool halfY, const std::atomic<bool> *cancel)
{
    if (dst == nullptr || dstW <= 0 || dstH <= 0)
        return false;
    const int bodyH = (halfY && (srcH & 1) != 0) ? dstH - 1 : dstH;
    parallelForRows(bodyH, 32, cancel,
                    [&](int y0, int y1)
                    {
                        if (resampleCancelled(cancel))
                            return;
                        writeLinearBody(sample, dst, srcW, dstW, halfX, halfY, y0, y1);
                    });
    if (resampleCancelled(cancel))
        return false;
    writeLinearEdges(sample, dst, srcW, srcH, dstW, dstH, halfX, halfY);
    return true;
}

std::shared_ptr<std::vector<uint16_t>> allocLinear(int width, int height)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    return std::make_shared<std::vector<uint16_t>>(count);
}

bool adoptLinear(LinearRgb &view, const std::shared_ptr<std::vector<uint16_t>> &rgb, int width,
                 int height)
{
    if (!rgb || rgb->empty())
        return false;
    view.data = rgb->data();
    view.width = width;
    view.height = height;
    view.stride = width;
    view.keep = rgb;
    return true;
}

void linearizeRow(const RgbView &src, int y, uint16_t *dst)
{
    const uint8_t *row = rowPointer(src, y);
    uint16_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(src.width) * 3u;
    for (int x = 0; x < src.width; ++x)
    {
        uint16_t *pixel = out + static_cast<size_t>(x) * 3u;
        for (int channel = 0; channel < 3; ++channel)
        {
            const int code = sampleChannel(row, src, x, channel);
            pixel[channel] = srgbCodeToLinear(static_cast<uint8_t>(code));
        }
    }
}

bool linearizePacked(const RgbView &src, LinearRgb &out, const std::atomic<bool> *cancel)
{
    if (src.data == nullptr || src.width <= 0 || src.height <= 0)
        return false;
    const std::shared_ptr<std::vector<uint16_t>> rgb = allocLinear(src.width, src.height);
    uint16_t *dst = rgb->data();
    parallelForRows(src.height, 32, cancel,
                    [&](int y0, int y1)
                    {
                        if (resampleCancelled(cancel))
                            return;
                        for (int y = y0; y < y1; ++y)
                            linearizeRow(src, y, dst);
                    });
    if (resampleCancelled(cancel))
        return false;
    return adoptLinear(out, rgb, src.width, src.height);
}

bool boxFromBytes(const RgbView &src, uint16_t *dst, int dstW, int dstH, bool halfX, bool halfY,
                  const std::atomic<bool> *cancel)
{
    const CodeSampler sampler{&src};
    return boxLinearRows(sampler, src.width, src.height, dst, dstW, dstH, halfX, halfY, cancel);
}

bool boxFromPlane(const LinearRgb &src, uint16_t *dst, int dstW, int dstH, bool halfX, bool halfY,
                  const std::atomic<bool> *cancel)
{
    const PlaneSampler sampler{&src};
    return boxLinearRows(sampler, src.width, src.height, dst, dstW, dstH, halfX, halfY, cancel);
}

bool stepLinear(const RgbView &bytes, LinearRgb &plane, bool fromBytes, int nextW, int nextH,
                bool halfX, bool halfY, const std::atomic<bool> *cancel)
{
    const std::shared_ptr<std::vector<uint16_t>> rgb = allocLinear(nextW, nextH);
    bool ok = false;
    if (fromBytes)
        ok = boxFromBytes(bytes, rgb->data(), nextW, nextH, halfX, halfY, cancel);
    else
        ok = boxFromPlane(plane, rgb->data(), nextW, nextH, halfX, halfY, cancel);
    if (!ok || resampleCancelled(cancel))
        return false;
    return adoptLinear(plane, rgb, nextW, nextH);
}

bool reduceLinear(const RgbView &src, LinearRgb &out, int dstW, int dstH,
                  const std::atomic<bool> *cancel)
{
    int width = src.width;
    int height = src.height;
    bool fromBytes = true;
    LinearRgb current;
    while (needsLinearHalve(width, dstW) || needsLinearHalve(height, dstH))
    {
        if (resampleCancelled(cancel))
            return false;
        const bool halfX = needsLinearHalve(width, dstW);
        const bool halfY = needsLinearHalve(height, dstH);
        const int nextW = halfX ? (width >> 1) : width;
        const int nextH = halfY ? (height >> 1) : height;
        if (!stepLinear(src, current, fromBytes, nextW, nextH, halfX, halfY, cancel))
            return false;
        fromBytes = false;
        width = nextW;
        height = nextH;
    }
    out = current;
    return !resampleCancelled(cancel);
}

uint16_t narrowWeight(int64_t acc) noexcept
{
    if (acc <= 0)
        return 0;
    constexpr int64_t kBias = int64_t{1} << (kWeightBits - 1);
    int64_t value = (acc + kBias) >> kWeightBits;
    if (value > kLinearMax)
        value = kLinearMax;
    return static_cast<uint16_t>(value);
}

void convolveHorizontalLinear(const LinearRgb &src, int y, const AxisKernel &kx, uint16_t *dstR,
                              uint16_t *dstG, uint16_t *dstB)
{
    for (int x = 0; x < kx.dstCount; ++x)
    {
        const int taps = kx.count[static_cast<size_t>(x)];
        const int origin = kx.origin[static_cast<size_t>(x)];
        const int16_t *weight = kx.weight.data() + static_cast<size_t>(x) * kMaxTaps;
        int64_t accR = 0;
        int64_t accG = 0;
        int64_t accB = 0;
        for (int tap = 0; tap < taps; ++tap)
        {
            const int sample = clampIndex(origin + tap, src.width);
            const size_t row = static_cast<size_t>(y) * static_cast<size_t>(src.stride);
            const uint16_t *pixel = src.data + (row + static_cast<size_t>(sample)) * 3u;
            const int64_t wr = weight[tap];
            accR += static_cast<int64_t>(pixel[0]) * wr;
            accG += static_cast<int64_t>(pixel[1]) * wr;
            accB += static_cast<int64_t>(pixel[2]) * wr;
        }
        dstR[x] = narrowWeight(accR);
        dstG[x] = narrowWeight(accG);
        dstB[x] = narrowWeight(accB);
    }
}

void convolveVerticalLinear(const uint16_t *planeR, const uint16_t *planeG, const uint16_t *planeB,
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
            int64_t accR = 0;
            int64_t accG = 0;
            int64_t accB = 0;
            for (int tap = 0; tap < taps; ++tap)
            {
                const int sample = clampIndex(origin + tap, srcH);
                const size_t offset =
                    (static_cast<size_t>(sample - srcRow0) * static_cast<size_t>(dstW)) +
                    static_cast<size_t>(x);
                const int64_t wr = weight[tap];
                accR += static_cast<int64_t>(planeR[offset]) * wr;
                accG += static_cast<int64_t>(planeG[offset]) * wr;
                accB += static_cast<int64_t>(planeB[offset]) * wr;
            }
            const size_t pixel = static_cast<size_t>(x) * 3u;
            out[pixel] = linearToSrgbCode(narrowWeight(accR));
            out[pixel + 1u] = linearToSrgbCode(narrowWeight(accG));
            out[pixel + 2u] = linearToSrgbCode(narrowWeight(accB));
        }
    }
}

struct RowRange
{
    int first = 0;
    int last = -1;
};

RowRange coveredRows(const AxisKernel &ky, int srcH, int y0, int y1)
{
    RowRange rows;
    rows.first = srcH;
    rows.last = -1;
    for (int y = y0; y < y1; ++y)
    {
        const int taps = ky.count[static_cast<size_t>(y)];
        const int origin = ky.origin[static_cast<size_t>(y)];
        for (int tap = 0; tap < taps; ++tap)
        {
            const int sample = clampIndex(origin + tap, srcH);
            if (sample < rows.first)
                rows.first = sample;
            if (sample > rows.last)
                rows.last = sample;
        }
    }
    return rows;
}

void fillHorizontalLinear(const LinearRgb &src, const AxisKernel &kx, const RowRange &rows,
                          int dstW, std::vector<uint16_t> &planeR, std::vector<uint16_t> &planeG,
                          std::vector<uint16_t> &planeB)
{
    const int height = rows.last - rows.first + 1;
    const size_t count = static_cast<size_t>(height) * static_cast<size_t>(dstW);
    planeR.assign(count, 0);
    planeG.assign(count, 0);
    planeB.assign(count, 0);
    for (int sample = rows.first; sample <= rows.last; ++sample)
    {
        const size_t offset = static_cast<size_t>(sample - rows.first) * static_cast<size_t>(dstW);
        convolveHorizontalLinear(src, sample, kx, planeR.data() + offset, planeG.data() + offset,
                                 planeB.data() + offset);
    }
}

void convolveLinearBand(const LinearRgb &src, uint8_t *dst, int dstW, const AxisKernel &kx,
                        const AxisKernel &ky, int y0, int y1)
{
    const RowRange rows = coveredRows(ky, src.height, y0, y1);
    if (rows.last < rows.first)
        return;
    std::vector<uint16_t> planeR;
    std::vector<uint16_t> planeG;
    std::vector<uint16_t> planeB;
    fillHorizontalLinear(src, kx, rows, dstW, planeR, planeG, planeB);
    convolveVerticalLinear(planeR.data(), planeG.data(), planeB.data(), rows.first, src.height, ky,
                           y0, y1, dstW, dst);
}

void encodeLinearRow(const LinearRgb &src, int y, uint8_t *dst)
{
    const size_t row = static_cast<size_t>(y) * static_cast<size_t>(src.stride);
    const uint16_t *samples = src.data + row * 3u;
    uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(src.width) * 3u;
    for (int x = 0; x < src.width; ++x)
    {
        const uint16_t *pixel = samples + static_cast<size_t>(x) * 3u;
        uint8_t *encoded = out + static_cast<size_t>(x) * 3u;
        encoded[0] = linearToSrgbCode(pixel[0]);
        encoded[1] = linearToSrgbCode(pixel[1]);
        encoded[2] = linearToSrgbCode(pixel[2]);
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

bool prepareSharpLinear(const RgbView &src, LinearRgb &linear, int dstW, int dstH,
                        const std::atomic<bool> *cancel)
{
    // Sharp-only. Linearize before any 2x box so large reductions stay in
    // 16-bit linear light. The 8-bit pyramid cache remains Preview's.
    if (src.data == nullptr || dstW <= 0 || dstH <= 0)
        return false;
    if (!needsLinearHalve(src.width, dstW) && !needsLinearHalve(src.height, dstH))
        return linearizePacked(src, linear, cancel);
    return reduceLinear(src, linear, dstW, dstH, cancel);
}

bool encodeLinearToRgb(const LinearRgb &src, uint8_t *dst, const std::atomic<bool> *cancel)
{
    if (src.data == nullptr || dst == nullptr || src.width <= 0 || src.height <= 0)
        return false;
    parallelForRows(src.height, 32, cancel,
                    [&](int y0, int y1)
                    {
                        if (resampleCancelled(cancel))
                            return;
                        for (int y = y0; y < y1; ++y)
                            encodeLinearRow(src, y, dst);
                    });
    return !resampleCancelled(cancel);
}

bool resizeSeparableLinear(const LinearRgb &src, uint8_t *dst, int dstW, int dstH,
                           const AxisKernel &kx, const AxisKernel &ky,
                           const std::atomic<bool> *cancel)
{
    if (dst == nullptr || src.data == nullptr || dstW <= 0 || dstH <= 0)
        return false;
    parallelForRows(dstH, 24, cancel,
                    [&](int y0, int y1)
                    {
                        if (resampleCancelled(cancel))
                            return;
                        convolveLinearBand(src, dst, dstW, kx, ky, y0, y1);
                    });
    return !resampleCancelled(cancel);
}

} // namespace mviewer::core::resample_detail
