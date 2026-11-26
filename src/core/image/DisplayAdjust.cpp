#include "core/image/DisplayAdjust.h"

#include "core/filesystem/Utf8Path.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace mviewer::core
{
namespace
{
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv(std::uint64_t hash, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
    {
        hash ^= (value & 0xFF);
        hash *= kFnvPrime;
        value >>= 8;
    }
    return hash;
}

bool endsWithLower(const std::string &path, const char *suffix)
{
    const size_t len = std::strlen(suffix);
    if (path.size() < len)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        char c = path[path.size() - len + i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != suffix[i])
            return false;
    }
    return true;
}

float lutCorner(const DisplayAdjust &adjust, int size, int x, int y, int z, int c)
{
    const size_t at = (static_cast<size_t>(z) * static_cast<size_t>(size) +
                       static_cast<size_t>(y)) *
                          static_cast<size_t>(size) +
                      static_cast<size_t>(x);
    return static_cast<float>(adjust.lut[at * 3u + static_cast<size_t>(c)]);
}

/// The four corners of the cube sub-division (a tetrahedron) that contains the
/// sample point, plus their barycentric weights. Picking the tetrahedron is a
/// decision table on the three fractional parts; the path always walks each
/// axis monotonically, so the weights are non-negative and sum to 1.
struct Tetra
{
    int ax, ay, az;
    int bx, by, bz;
    int cx, cy, cz;
    int dx, dy, dz;
    float wa, wb, wc, wd;
};

Tetra pickTetrahedron(float tx, float ty, float tz, int x0, int y0, int z0, int x1, int y1,
                      int z1)
{
    Tetra t{};
    t.ax = t.bx = t.cx = t.dx = x0;
    t.ay = t.by = t.cy = t.dy = y0;
    t.az = t.bz = t.cz = t.dz = z0;
    if (tx >= ty)
    {
        if (ty >= tz)
        {
            t.bx = x1;
            t.cx = t.dx = x1;
            t.cy = t.dy = y1;
            t.dz = z1;
            t.wa = 1.0f - tx;
            t.wb = tx - ty;
            t.wc = ty - tz;
            t.wd = tz;
        }
        else if (tx >= tz)
        {
            t.bx = t.cx = t.dx = x1;
            t.cz = t.dz = z1;
            t.dy = y1;
            t.wa = 1.0f - tx;
            t.wb = tx - tz;
            t.wc = tz;
            t.wd = ty;
        }
        else
        {
            t.bz = t.cz = t.dz = z1;
            t.cx = t.dx = x1;
            t.dy = y1;
            t.wa = 1.0f - tz;
            t.wb = tz - tx;
            t.wc = tx - ty;
            t.wd = ty;
        }
    }
    else
    {
        if (tx >= tz)
        {
            t.ay = t.by = t.cy = t.dy = y1;
            t.cx = t.dx = x1;
            t.dz = z1;
            t.wa = 1.0f - ty;
            t.wb = ty - tx;
            t.wc = tx - tz;
            t.wd = tz;
        }
        else if (ty >= tz)
        {
            t.ay = t.by = t.cy = t.dy = y1;
            t.cz = t.dz = z1;
            t.dx = x1;
            t.wa = 1.0f - ty;
            t.wb = ty - tz;
            t.wc = tz;
            t.wd = tx;
        }
        else
        {
            t.bz = t.cz = t.dz = z1;
            t.cy = t.dy = y1;
            t.dx = x1;
            t.wa = 1.0f - tz;
            t.wb = tz - ty;
            t.wc = ty - tx;
            t.wd = tx;
        }
    }
    return t;
}

/// Tetrahedral sample of a blue-major .cube LUT.
///
/// Four corners instead of eight: one subdivision of the cube is a tetrahedron
/// that contains the sample point, and the weights are its barycentric
/// coordinates. Trilinear reads 8 neighbours and does 7 lerps per channel; this
/// reads 4 and does 3, and it is the interpolation a colourist expects (a
/// trilinear blend of a swap LUT is exact, and so is this).
void sampleLut(const DisplayAdjust &adjust, int r, int g, int b, std::uint8_t out[3])
{
    const int size = adjust.lutSize;
    const auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const float fx = clamp01(static_cast<float>(r) / 255.0f) * static_cast<float>(size - 1);
    const float fy = clamp01(static_cast<float>(g) / 255.0f) * static_cast<float>(size - 1);
    const float fz = clamp01(static_cast<float>(b) / 255.0f) * static_cast<float>(size - 1);
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int z0 = static_cast<int>(std::floor(fz));
    const int x1 = std::min(x0 + 1, size - 1);
    const int y1 = std::min(y0 + 1, size - 1);
    const int z1 = std::min(z0 + 1, size - 1);

    const Tetra tetra = pickTetrahedron(fx - static_cast<float>(x0), fy - static_cast<float>(y0),
                                        fz - static_cast<float>(z0), x0, y0, z0, x1, y1, z1);
    for (int c = 0; c < 3; ++c)
    {
        const float value = tetra.wa * lutCorner(adjust, size, tetra.ax, tetra.ay, tetra.az, c) +
                            tetra.wb * lutCorner(adjust, size, tetra.bx, tetra.by, tetra.bz, c) +
                            tetra.wc * lutCorner(adjust, size, tetra.cx, tetra.cy, tetra.cz, c) +
                            tetra.wd * lutCorner(adjust, size, tetra.dx, tetra.dy, tetra.dz, c);
        out[c] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
    }
}

} // namespace

bool DisplayAdjust::identity() const
{
    return exposureEv == 0.0 && gamma == 1.0 && !autoStretch && lutSize <= 0;
}

std::uint64_t DisplayAdjust::fingerprint() const
{
    std::uint64_t hash = kFnvOffset;
    hash = fnv(hash, std::bit_cast<std::uint64_t>(exposureEv));
    hash = fnv(hash, std::bit_cast<std::uint64_t>(gamma));
    hash = fnv(hash, autoStretch ? 1 : 0);
    hash = fnv(hash, static_cast<std::uint64_t>(lutSize));
    for (const std::uint8_t byte : lut)
        hash = fnv(hash, byte);
    return hash;
}

bool DisplayAdjust::isCubePath(const std::string &path)
{
    return endsWithLower(path, ".cube");
}

bool DisplayAdjust::loadCube(const std::string &path, DisplayAdjust &out, std::string &error)
{
    std::ifstream in(mviewer::core::pathFromUtf8(path));
    if (!in)
    {
        error = "cannot open LUT file";
        return false;
    }
    int size = 0;
    std::vector<double> values;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        if (line[first] == '#')
            continue;
        std::istringstream ss(line.substr(first));
        std::string keyword;
        ss >> keyword;
        if (keyword == "LUT_3D_SIZE" || keyword == "LUT_1D_SIZE")
        {
            if (!(ss >> size) || size <= 0 || size > 256)
            {
                error = "invalid LUT size";
                return false;
            }
            continue;
        }
        if (keyword == "TITLE" || keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX")
            continue;
        // Otherwise the line is a triple of LUT values.
        std::istringstream valuesStream(line.substr(first));
        double r = 0.0, g = 0.0, b = 0.0;
        if (valuesStream >> r >> g >> b)
            values.push_back(r), values.push_back(g), values.push_back(b);
    }
    if (size <= 0)
    {
        error = "LUT size missing";
        return false;
    }
    const size_t expected =
        static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size) * 3u;
    if (values.size() != expected)
    {
        error = "LUT value count does not match LUT_3D_SIZE";
        return false;
    }
    out.lutSize = size;
    out.lut.assign(expected, 0);
    for (size_t i = 0; i < expected; ++i)
        out.lut[i] =
            static_cast<std::uint8_t>(std::lround(std::clamp(values[i], 0.0, 1.0) * 255.0));
    return true;
}

ImageData applyDisplayAdjust(const ImageData &src, const DisplayAdjust &adjust)
{
    if (src.isNull() || adjust.identity())
        return src;
    if (src.format != PixelFormat::RGB24 && src.format != PixelFormat::Grayscale8)
        return src;

    // Every pixel is rewritten below, so build a fresh buffer instead of
    // copying src (a needless full-frame memcpy on the 12 MP display path).
    ImageData out = makeImageData(src.width, src.height, src.format);
    if (out.isNull())
        return src; // allocation failed: leave the frame untouched
    const int width = src.width;
    const int height = src.height;
    const bool grayscale = src.format == PixelFormat::Grayscale8;
    const int channels = grayscale ? 1 : 3;
    const size_t inStride = src.constView().stride();
    const size_t outStride = out.constView().stride();

    double lo[3] = {0.0, 0.0, 0.0};
    double hi[3] = {255.0, 255.0, 255.0};
    if (adjust.autoStretch)
    {
        int minV[3] = {255, 255, 255};
        int maxV[3] = {0, 0, 0};
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t *row = src.constView().data + static_cast<size_t>(y) * inStride;
            for (int x = 0; x < width; ++x)
            {
                // A Grayscale8 buffer has ONE byte per pixel: indexing it as
                // RGB24 reads up to 3x past the end of the allocation.
                const std::uint8_t *px =
                    row + static_cast<size_t>(x) * static_cast<size_t>(channels);
                for (int c = 0; c < channels; ++c)
                {
                    const int v = px[c];
                    minV[c] = std::min(minV[c], v);
                    maxV[c] = std::max(maxV[c], v);
                }
            }
        }
        for (int c = 0; c < 3; ++c)
        {
            // A flat channel has no range to stretch: leave it untouched. The
            // bounds must still span 0..255 - collapsing both to 255 would divide
            // by zero below (lround(NaN) is undefined behaviour).
            const bool flat = !(maxV[c] > minV[c]);
            lo[c] = flat ? 0.0 : static_cast<double>(minV[c]);
            hi[c] = flat ? 255.0 : static_cast<double>(maxV[c]);
        }
    }

    const double exposure = std::pow(2.0, adjust.exposureEv);
    const double invGamma = adjust.gamma > 0.05 ? 1.0 / adjust.gamma : 1.0;
    const std::uint8_t *in = src.constView().data;
    std::uint8_t *dst = out.mutableView().data;

    // 8-bit exposure / AutoStretch / gamma collapse into three 256-entry
    // tables, so the pixel loop is a pure lookup instead of three std::pow per
    // channel (36M pow calls for a 12MP frame). The tables are only valid when
    // no 3D LUT remapped the value first; the LUT branch re-derives rgb[] and
    // then indexes the same tables, which is what the scalar path did too.
    std::array<std::uint8_t, 256> lut[3];
    for (int c = 0; c < 3; ++c)
    {
        for (int i = 0; i < 256; ++i)
        {
            double v = static_cast<double>(i) / 255.0;
            if (adjust.autoStretch && hi[c] > lo[c])
                v = (static_cast<double>(i) - lo[c]) / (hi[c] - lo[c]);
            v *= exposure;
            v = std::pow(v < 0.0 ? 0.0 : v, invGamma);
            lut[c][i] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        }
    }

    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t *inRow = in + static_cast<size_t>(y) * inStride;
        std::uint8_t *outRow = dst + static_cast<size_t>(y) * outStride;
        for (int x = 0; x < width; ++x)
        {
            const std::uint8_t *px = inRow + static_cast<size_t>(x) * static_cast<size_t>(channels);
            std::uint8_t rgb[3];
            if (grayscale)
                rgb[0] = rgb[1] = rgb[2] = px[0];
            else
            {
                rgb[0] = px[0];
                rgb[1] = px[1];
                rgb[2] = px[2];
            }
            if (adjust.lutSize > 0)
            {
                sampleLut(adjust, rgb[0], rgb[1], rgb[2], rgb);
                if (grayscale)
                {
                    // Keep the exposure / gamma / auto-stretch 1D LUT in the chain
                    // for grey images too - skipping it made grey and colour
                    // semantics diverge once a 3D LUT was loaded.
                    outRow[x] = lut[0][rgb[0]];
                    continue;
                }
            }
            for (int c = 0; c < channels; ++c)
                outRow[static_cast<size_t>(x) * static_cast<size_t>(channels) +
                       static_cast<size_t>(c)] = lut[c][rgb[c]];
        }
    }
    return out;
}

} // namespace mviewer::core
