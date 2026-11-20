#include "core/image/DisplayAdjust.h"

#include "core/filesystem/Utf8Path.h"

#include <algorithm>
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

/// Trilinear sample of a blue-major .cube LUT.
void sampleLut(const DisplayAdjust &adjust, int r, int g, int b, std::uint8_t out[3])
{
    const int size = adjust.lutSize;
    const auto clamp01 = [](double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
    const double fx = clamp01(r / 255.0) * (size - 1);
    const double fy = clamp01(g / 255.0) * (size - 1);
    const double fz = clamp01(b / 255.0) * (size - 1);
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int z0 = static_cast<int>(std::floor(fz));
    const int x1 = std::min(x0 + 1, size - 1);
    const int y1 = std::min(y0 + 1, size - 1);
    const int z1 = std::min(z0 + 1, size - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    const double tz = fz - z0;

    for (int c = 0; c < 3; ++c)
    {
        double value = 0.0;
        for (int dz = 0; dz <= 1; ++dz)
            for (int dy = 0; dy <= 1; ++dy)
                for (int dx = 0; dx <= 1; ++dx)
                {
                    const int x = dx ? x1 : x0;
                    const int y = dy ? y1 : y0;
                    const int z = dz ? z1 : z0;
                    // Adobe .cube order: red is the outer index, blue the inner.
                    const size_t at = (static_cast<size_t>(x) * static_cast<size_t>(size) +
                                       static_cast<size_t>(y)) *
                                          static_cast<size_t>(size) +
                                      static_cast<size_t>(z);
                    const double weight =
                        (dx ? tx : 1.0 - tx) * (dy ? ty : 1.0 - ty) * (dz ? tz : 1.0 - tz);
                    value +=
                        weight * static_cast<double>(adjust.lut[at * 3u + static_cast<size_t>(c)]);
                }
        out[c] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
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

    ImageData out = src;
    const int count = src.width * src.height;

    double lo[3] = {0.0, 0.0, 0.0};
    double hi[3] = {255.0, 255.0, 255.0};
    if (adjust.autoStretch)
    {
        int minV[3] = {255, 255, 255};
        int maxV[3] = {0, 0, 0};
        const std::uint8_t *p = src.constView().data;
        for (int i = 0; i < count; ++i)
        {
            const int c0 = p[i * 3u + 0];
            const int c1 = p[i * 3u + 1];
            const int c2 = p[i * 3u + 2];
            minV[0] = std::min(minV[0], c0);
            minV[1] = std::min(minV[1], c1);
            minV[2] = std::min(minV[2], c2);
            maxV[0] = std::max(maxV[0], c0);
            maxV[1] = std::max(maxV[1], c1);
            maxV[2] = std::max(maxV[2], c2);
        }
        for (int c = 0; c < 3; ++c)
        {
            lo[c] = minV[c];
            // A flat channel has no range to stretch: leave it untouched.
            hi[c] = (maxV[c] > minV[c]) ? maxV[c] : 255;
        }
    }

    const double exposure = std::pow(2.0, adjust.exposureEv);
    const double invGamma = adjust.gamma > 0.05 ? 1.0 / adjust.gamma : 1.0;
    const bool grayscale = src.format == PixelFormat::Grayscale8;
    const std::uint8_t *in = src.constView().data;
    std::uint8_t *dst = out.mutableView().data;
    for (int i = 0; i < count; ++i)
    {
        std::uint8_t rgb[3];
        if (grayscale)
            rgb[0] = rgb[1] = rgb[2] = in[i];
        else
        {
            rgb[0] = in[i * 3u + 0];
            rgb[1] = in[i * 3u + 1];
            rgb[2] = in[i * 3u + 2];
        }
        if (adjust.lutSize > 0)
        {
            sampleLut(adjust, rgb[0], rgb[1], rgb[2], rgb);
            if (grayscale)
            {
                dst[i] = rgb[0];
                continue;
            }
        }
        for (int c = 0; c < (grayscale ? 1 : 3); ++c)
        {
            const double normalized = static_cast<double>(rgb[c]) / 255.0;
            double v = normalized;
            if (adjust.autoStretch)
                v = (static_cast<double>(rgb[c]) - lo[c]) / (hi[c] - lo[c]);
            v *= exposure;
            v = std::pow(v < 0.0 ? 0.0 : v, invGamma);
            dst[grayscale ? i : (i * 3u + static_cast<size_t>(c))] =
                static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        }
    }
    return out;
}

} // namespace mviewer::core
