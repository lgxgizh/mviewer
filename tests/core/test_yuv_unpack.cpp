// NOLINTBEGIN
// Black-box tests for core/image/yuv/YuvUnpack: planar, semi-planar, packed,
// high-bit-depth containers, truncation, clamping, and a small random fuzzer.

#include "core/image/yuv/YuvUnpack.h"

#include <cstdint>
#include <cstdio>
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

static Format fmt(const char *name, int w, int h)
{
    Format f;
    f.width = w;
    f.height = h;
    const bool ok = applyPixFmtName(name, f);
    CHECK(ok);
    return f;
}

static int64_t frameBytesOf(const Format &f)
{
    const auto layout = computeFrameLayout(f);
    return layout ? layout->frameBytes : int64_t{-1};
}

static void putLe(std::vector<uint8_t> &b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

static void putBe(std::vector<uint8_t> &b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}

static bool eq(const std::vector<uint16_t> &p, const std::vector<uint16_t> &e)
{
    return p == e;
}

static void test420AndYv12()
{
    // 4x2 4:2:0: Y 4x2, chroma 2x1.
    const uint8_t raw[] = {10, 20, 30, 40, 50, 60, 70, 80, 100, 110, 200, 210};
    const Format a = fmt("yuv420p", 4, 2);
    const Format b = fmt("yv12", 4, 2);
    CHECK(frameBytesOf(a) == 12);
    YuvPlanes pa;
    YuvPlanes pb;
    std::string why;
    CHECK(unpackYuvFrame(a, raw, sizeof raw, pa, &why));
    CHECK(why.empty());
    CHECK(pa.width == 4 && pa.height == 2);
    CHECK(pa.chromaWidth == 2 && pa.chromaHeight == 1);
    CHECK(pa.bitDepth == 8);
    CHECK(eq(pa.y, {10, 20, 30, 40, 50, 60, 70, 80}));
    CHECK(eq(pa.u, {100, 110}));
    CHECK(eq(pa.v, {200, 210}));

    CHECK(unpackYuvFrame(b, raw, sizeof raw, pb, &why));
    CHECK(eq(pb.y, pa.y));
    CHECK(eq(pb.u, {200, 210}));
    CHECK(eq(pb.v, {100, 110}));
    CHECK(pb.chromaWidth == 2 && pb.chromaHeight == 1);
}

static void testNv12Nv21()
{
    // Interleaved chroma: NV12 is U,V ; NV21 is V,U.
    const uint8_t nv12[] = {1, 2, 3, 4, 5, 6, 7, 8, 11, 21, 12, 22};
    const uint8_t nv21[] = {1, 2, 3, 4, 5, 6, 7, 8, 21, 11, 22, 12};
    YuvPlanes a;
    YuvPlanes b;
    std::string why;
    CHECK(unpackYuvFrame(fmt("nv12", 4, 2), nv12, sizeof nv12, a, &why));
    CHECK(a.chromaWidth == 2 && a.chromaHeight == 1);
    CHECK(eq(a.y, {1, 2, 3, 4, 5, 6, 7, 8}));
    CHECK(eq(a.u, {11, 12}));
    CHECK(eq(a.v, {21, 22}));

    CHECK(unpackYuvFrame(fmt("nv21", 4, 2), nv21, sizeof nv21, b, &why));
    CHECK(eq(b.y, a.y));
    CHECK(eq(b.u, {11, 12}));
    CHECK(eq(b.v, {21, 22}));
}

static void checkPacked(const char *name, const uint8_t *raw, size_t n, uint16_t y0, uint16_t y1,
                        uint16_t y2, uint16_t y3, int yCount, uint16_t u0, uint16_t u1, uint16_t v0,
                        uint16_t v1)
{
    YuvPlanes p;
    std::string why;
    const Format f = fmt(name, yCount, 1);
    CHECK(unpackYuvFrame(f, raw, n, p, &why));
    CHECK(p.chromaWidth == 2 && p.chromaHeight == 1);
    CHECK(p.bitDepth == 8);
    CHECK(static_cast<int>(p.y.size()) == yCount);
    CHECK(p.y[0] == y0 && p.y[1] == y1);
    if (yCount == 4)
    {
        CHECK(p.y[2] == y2 && p.y[3] == y3);
    }
    else
    {
        CHECK(p.y[2] == y2);
        CHECK(p.y[2] != 0xEE);
    }
    CHECK(eq(p.u, {u0, u1}));
    CHECK(eq(p.v, {v0, v1}));
}

static void testPacked422()
{
    const uint8_t yuyv[] = {10, 1, 11, 2, 12, 3, 13, 4};
    const uint8_t uyvy[] = {1, 10, 2, 11, 3, 12, 4, 13};
    const uint8_t yvyu[] = {10, 2, 11, 1, 12, 4, 13, 3};
    checkPacked("yuyv422", yuyv, sizeof yuyv, 10, 11, 12, 13, 4, 1, 3, 2, 4);
    checkPacked("uyvy422", uyvy, sizeof uyvy, 10, 11, 12, 13, 4, 1, 3, 2, 4);
    checkPacked("yvyu422", yvyu, sizeof yvyu, 10, 11, 12, 13, 4, 1, 3, 2, 4);

    // Width 3: two macropixels, second luma of the last pair (0xEE) is dropped.
    const uint8_t yuyv3[] = {10, 1, 11, 2, 12, 3, 0xEE, 4};
    const uint8_t uyvy3[] = {1, 10, 2, 11, 3, 12, 4, 0xEE};
    const uint8_t yvyu3[] = {10, 2, 11, 1, 12, 4, 0xEE, 3};
    checkPacked("yuyv422", yuyv3, sizeof yuyv3, 10, 11, 12, 0, 3, 1, 3, 2, 4);
    checkPacked("uyvy422", uyvy3, sizeof uyvy3, 10, 11, 12, 0, 3, 1, 3, 2, 4);
    checkPacked("yvyu422", yvyu3, sizeof yvyu3, 10, 11, 12, 0, 3, 1, 3, 2, 4);
    CHECK(frameBytesOf(fmt("yuyv422", 3, 1)) == 8);
}

static std::vector<uint8_t> plane10(bool be, const uint16_t *s, int n)
{
    std::vector<uint8_t> b;
    b.reserve(static_cast<size_t>(n) * 2u);
    for (int i = 0; i < n; ++i)
    {
        if (be)
        {
            putBe(b, s[i]);
        }
        else
        {
            putLe(b, s[i]);
        }
    }
    return b;
}

static void test10BitEndian()
{
    // 2x2 4:2:0, one chroma sample. Identical code values, opposite endian.
    const uint16_t samples[] = {0, 1, 512, 1023, 100, 200};
    const auto le = plane10(false, samples, 6);
    const auto be = plane10(true, samples, 6);
    YuvPlanes a;
    YuvPlanes b;
    std::string why;
    CHECK(unpackYuvFrame(fmt("yuv420p10le", 2, 2), le.data(), le.size(), a, &why));
    CHECK(unpackYuvFrame(fmt("yuv420p10be", 2, 2), be.data(), be.size(), b, &why));
    CHECK(a.bitDepth == 10 && b.bitDepth == 10);
    CHECK(eq(a.y, {0, 1, 512, 1023}));
    CHECK(eq(a.u, {100}));
    CHECK(eq(a.v, {200}));
    CHECK(eq(a.y, b.y) && eq(a.u, b.u) && eq(a.v, b.v));

    // LE bytes 0x03 0xFC -> raw 0xFC03, low 10 bits are 0x003. Other samples 0.
    uint8_t garbage[12] = {};
    garbage[0] = 0x03;
    garbage[1] = 0xFC;
    YuvPlanes g;
    CHECK(unpackYuvFrame(fmt("yuv420p10le", 2, 2), garbage, sizeof garbage, g, &why));
    CHECK(g.y[0] == 0x003);
    CHECK(g.y[1] == 0 && g.u[0] == 0 && g.v[0] == 0);
}

static void testP010P016()
{
    // P010 stores 10-bit samples in the top bits: (1023 << 6) == 0xFFC0.
    std::vector<uint8_t> p10;
    putLe(p10, static_cast<uint16_t>(1023u << 6));
    for (int i = 0; i < 5; ++i)
    {
        putLe(p10, 0);
    }
    YuvPlanes a;
    std::string why;
    const Format f10 = fmt("p010le", 2, 2);
    CHECK(f10.bitDepth == 10);
    CHECK(f10.msbAligned);
    CHECK(unpackYuvFrame(f10, p10.data(), p10.size(), a, &why));
    CHECK(a.y[0] == 1023);
    CHECK(a.y[1] == 0 && a.u[0] == 0 && a.v[0] == 0);

    std::vector<uint8_t> p16;
    putLe(p16, 0xFFFF);
    for (int i = 0; i < 5; ++i)
    {
        putLe(p16, 0x0001);
    }
    YuvPlanes b;
    const Format f16 = fmt("p016le", 2, 2);
    CHECK(f16.bitDepth == 16);
    CHECK(unpackYuvFrame(f16, p16.data(), p16.size(), b, &why));
    CHECK(b.y[0] == 0xFFFF);
    CHECK(b.u[0] == 0x0001 && b.v[0] == 0x0001);
}

static void testGray()
{
    const uint8_t g[] = {9, 8, 7, 6};
    YuvPlanes p;
    std::string why;
    const Format f = fmt("gray", 2, 2);
    CHECK(unpackYuvFrame(f, g, sizeof g, p, &why));
    CHECK(p.chromaWidth == 0 && p.chromaHeight == 0);
    CHECK(p.u.empty() && p.v.empty());
    CHECK(eq(p.y, {9, 8, 7, 6}));

    std::vector<uint8_t> g16;
    putBe(g16, 0x1234);
    putBe(g16, 0xABCD);
    YuvPlanes q;
    const Format f16 = fmt("gray16be", 2, 1);
    CHECK(f16.bitDepth == 16);
    CHECK(f16.endian == Endian::Big);
    CHECK(unpackYuvFrame(f16, g16.data(), g16.size(), q, &why));
    CHECK(q.chromaWidth == 0);
    CHECK(q.u.empty() && q.v.empty());
    CHECK(eq(q.y, {0x1234, 0xABCD}));
}

static void expectLast(const char *name, int ySize, int cSize, int cW, int cH, bool semi)
{
    const int h = 3;
    const int w = 5;
    const Format f = fmt(name, w, h);
    const auto layout = computeFrameLayout(f);
    CHECK(layout.has_value());
    if (!layout)
    {
        return;
    }
    std::vector<uint8_t> raw(static_cast<size_t>(layout->frameBytes));
    for (size_t i = 0; i < raw.size(); ++i)
    {
        raw[i] = static_cast<uint8_t>(i + 1u);
    }
    YuvPlanes p;
    std::string why;
    CHECK(unpackYuvFrame(f, raw.data(), raw.size(), p, &why));
    CHECK(p.width == w && p.height == h);
    CHECK(p.chromaWidth == cW && p.chromaHeight == cH);
    CHECK(static_cast<int>(p.y.size()) == ySize);
    CHECK(p.y.back() == static_cast<uint16_t>(ySize));
    if (cSize == 0)
    {
        CHECK(p.u.empty() && p.v.empty());
        return;
    }
    CHECK(static_cast<int>(p.u.size()) == cSize);
    CHECK(static_cast<int>(p.v.size()) == cSize);
    if (!semi)
    {
        CHECK(p.u.back() == static_cast<uint16_t>(ySize + cSize));
        CHECK(p.v.back() == static_cast<uint16_t>(ySize + cSize + cSize));
    }
    else
    {
        // Interleaved UV: last byte is the last V sample.
        CHECK(p.v.back() == static_cast<uint16_t>(raw.size()));
        CHECK(p.u.back() == static_cast<uint16_t>(raw.size() - 1u));
    }
}

static void testOddSizes()
{
    // 5x3 rounds chroma up: 420 -> 3x2, 422 -> 3x3, 444 -> 5x3.
    expectLast("yuv420p", 15, 6, 3, 2, false);
    expectLast("yuv422p", 15, 9, 3, 3, false);
    expectLast("yuv444p", 15, 15, 5, 3, false);
    expectLast("nv12", 15, 6, 3, 2, true);
    CHECK(frameBytesOf(fmt("yuv420p", 5, 3)) == 15 + 6 + 6);
    CHECK(frameBytesOf(fmt("yuv422p", 5, 3)) == 15 + 9 + 9);
    CHECK(frameBytesOf(fmt("yuv444p", 5, 3)) == 45);
    CHECK(frameBytesOf(fmt("nv12", 5, 3)) == 15 + 12);
}

static void testReject()
{
    const Format f = fmt("yuv420p", 4, 2);
    const int64_t n = frameBytesOf(f);
    CHECK(n == 12);
    std::vector<uint8_t> raw(static_cast<size_t>(n), 1);
    YuvPlanes p;
    std::string why = "stale";
    CHECK(!unpackYuvFrame(f, raw.data(), static_cast<size_t>(n - 1), p, &why));
    CHECK(!why.empty());

    why.clear();
    CHECK(!unpackYuvFrame(f, nullptr, 0, p, &why));
    CHECK(!why.empty());

    why.clear();
    CHECK(!unpackYuvFrame(f, nullptr, 4, p, &why));
    CHECK(!why.empty());

    Format bad;
    bad.width = 0;
    bad.height = 2;
    why.clear();
    CHECK(!unpackYuvFrame(bad, raw.data(), raw.size(), p, &why));
    CHECK(!why.empty());

    // Extra trailing bytes are ignored.
    raw.push_back(0xFF);
    why = "x";
    CHECK(unpackYuvFrame(f, raw.data(), raw.size(), p, &why));
    CHECK(why.empty());
    CHECK(p.y[0] == 1);
}

static void testSampleAt()
{
    const std::vector<uint16_t> plane = {1, 2, 3, 4, 5, 6};
    CHECK(sampleAt(plane, 3, 2, 0, 0) == 1);
    CHECK(sampleAt(plane, 3, 2, 2, 1) == 6);
    CHECK(sampleAt(plane, 3, 2, -1, -1) == 1);
    CHECK(sampleAt(plane, 3, 2, 3, 2) == 6);
    CHECK(sampleAt(plane, 3, 2, 100, -5) == 3);
    const std::vector<uint16_t> empty;
    CHECK(sampleAt(empty, 0, 0, 0, 0) == 0);
    CHECK(sampleAt(empty, 4, 4, 1, 1) == 0);
}

struct Rng
{
    uint32_t s;
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

static void testFuzz()
{
    static const char *names[] = {"yuv420p",     "yv12",        "nv12",    "nv21",    "yuv422p",
                                  "yuv444p",     "yuyv422",     "uyvy422", "yvyu422", "gray",
                                  "yuv420p10le", "yuv420p10be", "p010le",  "p016le",  "gray16be",
                                  "yuv444p12le", "nv16",        "nv24"};
    Rng rng{0xC0FFEEu};
    for (int i = 0; i < 3000; ++i)
    {
        const char *name = names[rng.next() % 18u];
        const int w = static_cast<int>(rng.next() % 33u) + 1;
        const int h = static_cast<int>(rng.next() % 33u) + 1;
        const Format f = fmt(name, w, h);
        const auto layout = computeFrameLayout(f);
        CHECK(layout.has_value());
        if (!layout)
        {
            return;
        }
        const int64_t fb = layout->frameBytes;
        const uint32_t span = static_cast<uint32_t>(fb) + 4u;
        const size_t sz = static_cast<size_t>(rng.next() % span);
        std::vector<uint8_t> raw(sz);
        for (size_t k = 0; k < raw.size(); ++k)
        {
            raw[k] = static_cast<uint8_t>(rng.next() & 0xFFu);
        }
        YuvPlanes p;
        std::string why;
        const uint8_t *ptr = raw.empty() ? nullptr : raw.data();
        const bool ok = unpackYuvFrame(f, ptr, raw.size(), p, &why);
        const bool expect = static_cast<int64_t>(sz) >= fb;
        if (ok != expect)
        {
            std::printf("FAIL fuzz i=%d %s %dx%d sz=%zu fb=%lld ok=%d\n", i, name, w, h, sz,
                        static_cast<long long>(fb), ok ? 1 : 0);
            ++g_failures;
            return;
        }
        if (!ok)
        {
            CHECK(!why.empty());
            continue;
        }
        CHECK(why.empty());
        const unsigned maxv = (f.bitDepth >= 16) ? 0xFFFFu : (1u << f.bitDepth) - 1u;
        for (uint16_t s : p.y)
        {
            CHECK(s <= maxv);
        }
        for (uint16_t s : p.u)
        {
            CHECK(s <= maxv);
        }
        for (uint16_t s : p.v)
        {
            CHECK(s <= maxv);
        }
        if (f.subsampling == Subsampling::S400)
        {
            CHECK(p.chromaWidth == 0 && p.u.empty() && p.v.empty());
        }
    }
}

int main()
{
    test420AndYv12();
    testNv12Nv21();
    testPacked422();
    test10BitEndian();
    testP010P016();
    testGray();
    testOddSizes();
    testReject();
    testSampleAt();
    testFuzz();
    if (g_failures != 0)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
// NOLINTEND
