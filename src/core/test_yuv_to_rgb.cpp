// NOLINTBEGIN
// Unit tests for core/image/yuv/YuvToRgb: Q16 coefficients, BT.601/709/2020
// golden vectors at 8/10/16 bits, clamping, chroma upsampling and cross-format
// equivalence through YuvUnpack, plus a fuzz loop.

#include "core/image/yuv/YuvToRgb.h"
#include "core/image/yuv/YuvUnpack.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace mviewer::core::yuv;

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

static Format fmt(const char *pix, int w, int h, Matrix m = Matrix::BT709, Range r = Range::Limited)
{
    Format f;
    f.width = w;
    f.height = h;
    applyPixFmtName(pix, f);
    f.matrix = m;
    f.range = r;
    return f;
}

static YuvPlanes onePixel444(int depth, int y, int u, int v)
{
    YuvPlanes p;
    p.width = p.height = p.chromaWidth = p.chromaHeight = 1;
    p.bitDepth = depth;
    p.y = {uint16_t(y)};
    p.u = {uint16_t(u)};
    p.v = {uint16_t(v)};
    return p;
}

static bool near(int a, int b)
{
    return std::abs(a - b) <= 1;
}

static void checkGolden(Matrix m, Range r, int y, int u, int v, int er, int eg, int eb)
{
    static const struct
    {
        const char *pix;
        int depth;
        int mul;
    } kDepths[] = {{"yuv444p", 8, 1}, {"yuv444p10le", 10, 4}, {"yuv444p16le", 16, 256}};
    // Limited range scales by bit shift (BT.2100); full range does not, so
    // higher-depth full-range vectors are covered by the exact endpoint checks.
    for (const auto &d : kDepths)
    {
        if (r == Range::Full && d.depth != 8)
            continue;
        const auto rgb =
            toRgb24(onePixel444(d.depth, y * d.mul, u * d.mul, v * d.mul), fmt(d.pix, 1, 1, m, r));
        const bool ok = rgb.size() == 3 && near(rgb[0], er) && near(rgb[1], eg) && near(rgb[2], eb);
        if (!ok)
        {
            std::printf("FAIL golden %s m=%d r=%d YUV(%d,%d,%d) -> (%d,%d,%d) want (%d,%d,%d)\n",
                        d.pix, int(m), int(r), y, u, v, rgb.size() == 3 ? rgb[0] : -1,
                        rgb.size() == 3 ? rgb[1] : -1, rgb.size() == 3 ? rgb[2] : -1, er, eg, eb);
            ++g_failures;
        }
    }
}

static void testCoefficients()
{
    for (Matrix m : {Matrix::BT601, Matrix::BT709, Matrix::BT2020})
        for (Range r : {Range::Limited, Range::Full})
            for (int depth = 1; depth <= 16; ++depth)
            {
                const YuvCoeffs c = makeYuvCoeffs(m, r, depth);
                for (int32_t k : {c.cy, c.crv, c.cgu, c.cgv, c.cbu})
                    CHECK(k > 0 && k <= 32767);
                // Largest |sum| stays far inside int32.
                CHECK(int64_t(c.cy) * 4095 + int64_t(c.crv + c.cgu + c.cgv + c.cbu) * 2048 <
                      (int64_t(1) << 31));
            }
    CHECK(makeYuvCoeffs(Matrix::BT709, Range::Limited).cy == 4769); // 65536*255/3504
    CHECK(makeYuvCoeffs(Matrix::BT709, Range::Limited, 10).cy == 4769);
    CHECK(makeYuvCoeffs(Matrix::BT709, Range::Full, 8).cy == 4096); // 65536*255/4080
    CHECK(makeYuvCoeffs(Matrix::BT709, Range::Full, 16).cy == 4081);
    CHECK(toTwelveBit(255, 8) == 4080 && toTwelveBit(1023, 10) == 4092);
    CHECK(toTwelveBit(65535, 16) == 4095 && toTwelveBit(4095, 12) == 4095);
}

static void testGolden()
{
    const Matrix M709 = Matrix::BT709;
    const Range LIM = Range::Limited, FULL = Range::Full;
    checkGolden(M709, LIM, 16, 128, 128, 0, 0, 0);
    checkGolden(M709, LIM, 235, 128, 128, 255, 255, 255);
    checkGolden(M709, LIM, 126, 128, 128, 128, 128, 128);
    checkGolden(M709, LIM, 63, 102, 240, 255, 0, 0);
    checkGolden(M709, LIM, 173, 42, 26, 0, 255, 0);
    checkGolden(M709, LIM, 32, 240, 118, 0, 0, 255);
    checkGolden(Matrix::BT601, LIM, 81, 90, 240, 255, 0, 0);
    checkGolden(Matrix::BT2020, LIM, 74, 97, 240, 255, 0, 0);
    checkGolden(Matrix::BT601, FULL, 76, 85, 255, 255, 0, 0);
    checkGolden(Matrix::BT601, FULL, 0, 128, 128, 0, 0, 0);
    checkGolden(Matrix::BT601, FULL, 255, 128, 128, 255, 255, 255);
    checkGolden(Matrix::BT709, FULL, 0, 128, 128, 0, 0, 0);
    checkGolden(Matrix::BT709, FULL, 54, 99, 255, 255, 0, 0);
    // Exact (tolerance 0) endpoints for 8-bit and 10-bit full range.
    auto exact = [](const char *pix, int depth, int y, int want)
    {
        const auto rgb = toRgb24(onePixel444(depth, y, 1 << (depth - 1), 1 << (depth - 1)),
                                 fmt(pix, 1, 1, Matrix::BT709, Range::Full));
        return rgb.size() == 3 && rgb[0] == want && rgb[1] == want && rgb[2] == want;
    };
    CHECK(exact("yuv444p", 8, 255, 255));
    CHECK(exact("yuv444p10le", 10, 1023, 255));
    CHECK(exact("yuv444p10le", 10, 0, 0));
    CHECK(exact("yuv444p16le", 16, 65535, 255));
}

static void testClampAndGray()
{
    auto rgb = toRgb24(onePixel444(8, 0, 128, 128), fmt("yuv444p", 1, 1));
    CHECK(rgb.size() == 3 && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    rgb = toRgb24(onePixel444(8, 255, 128, 255), fmt("yuv444p", 1, 1));
    CHECK(rgb.size() == 3 && rgb[0] == 255);
    rgb = toRgb24(onePixel444(8, 255, 255, 255), fmt("yuv444p", 1, 1));
    CHECK(rgb.size() == 3 && rgb[0] == 255 && rgb[2] == 255);

    YuvPlanes g;
    g.width = 3;
    g.height = 2;
    g.bitDepth = 8;
    g.y = {16, 50, 100, 150, 200, 235};
    rgb = toRgb24(g, fmt("gray", 3, 2));
    CHECK(rgb.size() == 18);
    for (size_t i = 0; i + 2 < rgb.size(); i += 3)
        CHECK(rgb[i] == rgb[i + 1] && rgb[i + 1] == rgb[i + 2]);
    CHECK(rgb.size() == 18 && rgb[0] == 0 && rgb[15] == 255);
}

// ── Cross-format equivalence ────────────────────────────────────────────────

static uint32_t g_rng = 0x1234567u;
static uint32_t nextRand()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

// Writes a logical 8-bit picture (Y full res, U/V at the format's chroma res)
// into the byte layout of 'f'. Supports 8-bit planar, semi-planar and packed.
static std::vector<uint8_t> pack8(const Format &f, const YuvPlanes &p)
{
    const auto layout = computeFrameLayout(f);
    std::vector<uint8_t> out(size_t(layout->frameBytes));
    const int w = f.width, h = f.height, cw = chromaWidth(f), ch = chromaHeight(f);
    const bool vu = f.order == PlaneOrder::VU;
    if (f.layout == Layout::Packed)
    {
        const size_t rowBytes = size_t(layout->planes[0].rowBytes);
        for (int y = 0; y < h; ++y)
            for (int c = 0; c < cw; ++c)
            {
                uint8_t *q = out.data() + size_t(y) * rowBytes + size_t(c) * 4;
                const uint8_t y0 = uint8_t(p.y[size_t(y * w + 2 * c)]);
                const uint8_t y1 = 2 * c + 1 < w ? uint8_t(p.y[size_t(y * w + 2 * c + 1)]) : 0;
                const uint8_t u = uint8_t(p.u[size_t(y * cw + c)]);
                const uint8_t v = uint8_t(p.v[size_t(y * cw + c)]);
                if (f.packing == Packing::YUYV)
                    q[0] = y0, q[1] = u, q[2] = y1, q[3] = v;
                else if (f.packing == Packing::UYVY)
                    q[0] = u, q[1] = y0, q[2] = v, q[3] = y1;
                else
                    q[0] = y0, q[1] = v, q[2] = y1, q[3] = u;
            }
        return out;
    }
    for (size_t i = 0; i < size_t(w * h); ++i)
        out[size_t(layout->planes[0].offset) + i] = uint8_t(p.y[i]);
    const size_t o1 = size_t(layout->planes[1].offset);
    for (size_t i = 0; i < size_t(cw * ch); ++i)
    {
        const uint8_t first = uint8_t(vu ? p.v[i] : p.u[i]);
        const uint8_t second = uint8_t(vu ? p.u[i] : p.v[i]);
        if (f.layout == Layout::SemiPlanar)
        {
            out[o1 + 2 * i] = first;
            out[o1 + 2 * i + 1] = second;
        }
        else
        {
            out[o1 + i] = first;
            out[size_t(layout->planes[2].offset) + i] = second;
        }
    }
    return out;
}

static YuvPlanes randomPicture(int w, int h, int cw, int ch)
{
    YuvPlanes p;
    p.width = w;
    p.height = h;
    p.chromaWidth = cw;
    p.chromaHeight = ch;
    p.bitDepth = 8;
    for (int i = 0; i < w * h; ++i)
        p.y.push_back(uint16_t(nextRand() & 0xFF));
    for (int i = 0; i < cw * ch; ++i)
    {
        p.u.push_back(uint16_t(nextRand() & 0xFF));
        p.v.push_back(uint16_t(nextRand() & 0xFF));
    }
    return p;
}

static std::vector<uint8_t> viaUnpack(const char *pix, const YuvPlanes &logical, ChromaUpsample up)
{
    const Format f = fmt(pix, logical.width, logical.height);
    const auto bytes = pack8(f, logical);
    YuvPlanes planes;
    if (!unpackYuvFrame(f, bytes.data(), bytes.size(), planes))
        return {};
    return toRgb24(planes, f, up);
}

static void testEquivalence()
{
    for (int odd = 0; odd < 2; ++odd)
    {
        const int w = 8 - odd, h = 6 - odd;
        const YuvPlanes p420 = randomPicture(w, h, (w + 1) / 2, (h + 1) / 2);
        for (ChromaUpsample up : {ChromaUpsample::Nearest, ChromaUpsample::Bilinear})
        {
            const auto ref = viaUnpack("yuv420p", p420, up);
            CHECK(ref.size() == size_t(w * h * 3));
            CHECK(viaUnpack("nv12", p420, up) == ref);
            CHECK(viaUnpack("nv21", p420, up) == ref);
            CHECK(viaUnpack("yv12", p420, up) == ref);
        }
        const YuvPlanes p422 = randomPicture(w, h, (w + 1) / 2, h);
        const auto ref = viaUnpack("yuv422p", p422, ChromaUpsample::Nearest);
        CHECK(ref.size() == size_t(w * h * 3));
        CHECK(viaUnpack("yuyv422", p422, ChromaUpsample::Nearest) == ref);
        CHECK(viaUnpack("uyvy422", p422, ChromaUpsample::Nearest) == ref);
        CHECK(viaUnpack("yvyu422", p422, ChromaUpsample::Nearest) == ref);
    }
    // 444 built from the 420 picture by nearest replication equals 420 nearest.
    const YuvPlanes p420 = randomPicture(8, 6, 4, 3);
    YuvPlanes p444 = p420;
    p444.chromaWidth = 8;
    p444.chromaHeight = 6;
    p444.u.assign(48, 0);
    p444.v.assign(48, 0);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x)
        {
            p444.u[size_t(y * 8 + x)] = p420.u[size_t((y / 2) * 4 + x / 2)];
            p444.v[size_t(y * 8 + x)] = p420.v[size_t((y / 2) * 4 + x / 2)];
        }
    CHECK(toRgb24(p444, fmt("yuv444p", 8, 6)) == toRgb24(p420, fmt("yuv420p", 8, 6)));
}

// ── Upsampling ──────────────────────────────────────────────────────────────

static void testBilinear()
{
    YuvPlanes flat = randomPicture(6, 4, 3, 2);
    flat.u.assign(6, 90);
    flat.v.assign(6, 200);
    for (ChromaSiting s : {ChromaSiting::Left, ChromaSiting::Center, ChromaSiting::TopLeft})
    {
        Format f = fmt("yuv420p", 6, 4);
        f.siting = s;
        CHECK(toRgb24(flat, f, ChromaUpsample::Bilinear) ==
              toRgb24(flat, f, ChromaUpsample::Nearest));
    }

    // 4x1 yuv422p with a chroma ramp U = {100, 200}; check the upsampled U
    // through gray-free blue channel ordering: compare against hand-built 444.
    YuvPlanes ramp;
    ramp.width = 4;
    ramp.height = 1;
    ramp.chromaWidth = 2;
    ramp.chromaHeight = 1;
    ramp.bitDepth = 8;
    ramp.y = {128, 128, 128, 128};
    ramp.u = {100, 200};
    ramp.v = {128, 128};
    auto expect444 = [&](std::vector<uint16_t> u12)
    {
        YuvPlanes q;
        q.width = q.chromaWidth = 4;
        q.height = q.chromaHeight = 1;
        q.bitDepth = 12;
        q.y.assign(4, uint16_t(128 << 4));
        q.v.assign(4, uint16_t(128 << 4));
        q.u = std::move(u12);
        return toRgb24(q, fmt("yuv444p12le", 4, 1));
    };
    Format f = fmt("yuv422p", 4, 1);
    f.siting = ChromaSiting::Left;
    // Co-sited: 100, (100+200+1)/2, 200, 200 in 12-bit units.
    CHECK(toRgb24(ramp, f, ChromaUpsample::Bilinear) ==
          expect444({1600, (1600 + 3200 + 1) >> 1, 3200, 3200}));
    f.siting = ChromaSiting::Center;
    // Centered 3:1: x0 (3*100+100), x1 (3*100+200), x2 (3*200+100), x3 (3*200+200).
    CHECK(toRgb24(ramp, f, ChromaUpsample::Bilinear) ==
          expect444({1600, (3 * 1600 + 3200 + 2) >> 2, (3 * 3200 + 1600 + 2) >> 2, 3200}));

    for (const char *pix : {"yuv420p", "yuv422p", "yuv444p", "nv12"})
        for (auto [w, h] : {std::pair{5, 3}, std::pair{1, 1}})
        {
            const Format g = fmt(pix, w, h);
            const YuvPlanes p = randomPicture(w, h, chromaWidth(g), chromaHeight(g));
            CHECK(toRgb24(p, g, ChromaUpsample::Bilinear).size() == size_t(w * h * 3));
        }
}

static void testRejects()
{
    const YuvPlanes p = randomPicture(4, 2, 2, 1);
    const Format f = fmt("yuv420p", 4, 2);
    std::vector<uint8_t> dst(4 * 2 * 3);
    CHECK(convertToRgb24(p, f, ChromaUpsample::Nearest, dst.data(), dst.size(), 12));
    CHECK(!convertToRgb24(p, f, ChromaUpsample::Nearest, dst.data(), dst.size() - 1, 12));
    CHECK(!convertToRgb24(p, f, ChromaUpsample::Nearest, dst.data(), dst.size(), 11));
    CHECK(!convertToRgb24(p, f, ChromaUpsample::Nearest, nullptr, dst.size(), 12));
    std::vector<uint8_t> padded(2 * 16);
    CHECK(convertToRgb24(p, f, ChromaUpsample::Nearest, padded.data(), padded.size(), 16));
    CHECK(toRgb24(p, fmt("yuv420p", 4, 4)).empty());
    CHECK(toRgb24(p, fmt("yuv444p", 4, 2)).empty());
    CHECK(toRgb24(p, fmt("yuv420p10le", 4, 2)).empty());
    YuvPlanes shortY = p;
    shortY.y.pop_back();
    CHECK(toRgb24(shortY, f).empty());
    YuvPlanes noV = p;
    noV.v.clear();
    CHECK(toRgb24(noV, f).empty());
    CHECK(toRgb24(YuvPlanes{}, Format{}).empty());
}

static void testFuzz()
{
    const char *names[] = {"yuv420p", "nv12", "yuyv422", "yuv444p10le", "gray", "p010le", "nv21"};
    int ok = 0;
    for (int i = 0; i < 2000; ++i)
    {
        Format f = fmt(names[nextRand() % 7], int(nextRand() % 17) + 1, int(nextRand() % 17) + 1);
        f.siting = ChromaSiting(nextRand() % 3);
        f.matrix = Matrix(nextRand() % 3);
        f.range = Range(nextRand() % 2);
        const auto layout = computeFrameLayout(f);
        if (!layout)
            continue;
        std::vector<uint8_t> bytes(size_t(layout->frameBytes));
        for (auto &b : bytes)
            b = uint8_t(nextRand());
        YuvPlanes planes;
        if (!unpackYuvFrame(f, bytes.data(), bytes.size(), planes))
            continue;
        const auto up = (i & 1) ? ChromaUpsample::Bilinear : ChromaUpsample::Nearest;
        ok += toRgb24(planes, f, up).size() == size_t(f.width * f.height * 3) ? 1 : 0;
    }
    CHECK(ok > 1500);
}

int main()
{
    testCoefficients();
    testGolden();
    testClampAndGray();
    testEquivalence();
    testBilinear();
    testRejects();
    testFuzz();
    if (g_failures == 0)
        std::printf("test_yuv_to_rgb: all passed\n");
    else
        std::printf("test_yuv_to_rgb: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
