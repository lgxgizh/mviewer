// NOLINTBEGIN
// Unit tests for core/image/yuv/YuvFormat (ADR 032): plane geometry, odd sizes,
// overflow-checked byte math, pixel-format names and the canonical text form.

#include "core/image/yuv/YuvFormat.h"

#include <cstdint>
#include <cstdio>
#include <limits>
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

static int64_t frameBytes(const Format &f)
{
    const auto layout = computeFrameLayout(f);
    return layout ? layout->frameBytes : -1;
}

static void testPlanarGeometry()
{
    const Format i420 = fmt("yuv420p", 1920, 1080);
    const auto l = computeFrameLayout(i420);
    CHECK(l.has_value());
    CHECK(l->planeCount == 3);
    CHECK(l->frameBytes == 3110400);
    CHECK(l->planes[0].offset == 0 && l->planes[0].bytes == 2073600);
    CHECK(l->planes[1].offset == 2073600 && l->planes[1].width == 960);
    CHECK(l->planes[1].height == 540 && l->planes[1].bytes == 518400);
    CHECK(l->planes[2].offset == 2592000);

    const Format yv12 = fmt("yv12", 1920, 1080);
    CHECK(yv12.order == PlaneOrder::VU);
    CHECK(frameBytes(yv12) == 3110400);

    const Format nv12 = fmt("nv12", 1920, 1080);
    const auto n = computeFrameLayout(nv12);
    CHECK(n && n->planeCount == 2 && n->frameBytes == 3110400);
    CHECK(n->planes[1].rowBytes == 1920 && n->planes[1].width == 960);
}

static void testOddSizes()
{
    const Format odd = fmt("yuv420p", 1921, 1081);
    CHECK(chromaWidth(odd) == 961 && chromaHeight(odd) == 541);
    CHECK(frameBytes(odd) == int64_t(1921) * 1081 + 2 * int64_t(961) * 541);

    const auto nv = computeFrameLayout(fmt("nv12", 3, 3));
    CHECK(nv && nv->frameBytes == 9 + 2 * 2 * 2);
    CHECK(nv->planes[1].rowBytes == 4);

    const auto yuyv = computeFrameLayout(fmt("yuyv422", 3, 2));
    CHECK(yuyv && yuyv->planes[0].rowBytes == 8 && yuyv->frameBytes == 16);

    const Format p422 = fmt("yuv422p", 5, 3);
    CHECK(chromaWidth(p422) == 3 && chromaHeight(p422) == 3);
    CHECK(frameBytes(p422) == 15 + 2 * 9);

    const Format one = fmt("yuv420p", 1, 1);
    CHECK(frameBytes(one) == 3);
}

static void testHighBitDepth()
{
    CHECK(frameBytes(fmt("yuv420p10le", 1920, 1080)) == 6220800);
    const Format p010 = fmt("p010le", 1920, 1080);
    CHECK(p010.msbAligned && p010.bitDepth == 10 && p010.layout == Layout::SemiPlanar);
    CHECK(frameBytes(p010) == 6220800);
    CHECK(frameBytes(fmt("gray", 640, 480)) == 307200);
    CHECK(frameBytes(fmt("gray16le", 640, 480)) == 614400);
    CHECK(frameBytes(fmt("yuv444p16le", 7680, 4320)) == int64_t(7680) * 4320 * 6);
    CHECK(bytesPerSample(fmt("yuv420p9be", 2, 2)) == 2);
}

static void testRejects()
{
    Format f = fmt("yuv420p", 0, 0);
    std::string why;
    CHECK(!isValid(f, &why) && !why.empty());
    f.width = -1;
    f.height = 10;
    CHECK(!isValid(f));
    f = fmt("yuv420p", kMaxDimension + 1, 16);
    CHECK(!isValid(f));
    f = fmt("yuv420p10le", 16, 16);
    f.bitDepth = 7;
    CHECK(!isValid(f));
    f.bitDepth = 17;
    CHECK(!isValid(f));
    f = fmt("yuyv422", 16, 16);
    f.subsampling = Subsampling::S420;
    CHECK(!isValid(f));
    f = fmt("yuv420p", 16, 16);
    f.msbAligned = true;
    CHECK(!isValid(f));
    f = fmt("yuv444p16le", 65536, 65536);
    CHECK(!isValid(f, &why));
    f = fmt("nv12", 16, 16);
    f.subsampling = Subsampling::S400;
    CHECK(!isValid(f));
    f = fmt("yuv420p", 16, 16);
    f.packing = Packing::UYVY;
    CHECK(!isValid(f));
    f = fmt("yuyv422", 16, 16);
    f.packing = Packing::None;
    CHECK(!isValid(f));
}

static void testCheckedMath()
{
    const int64_t kMax = std::numeric_limits<int64_t>::max();
    int64_t out = 7;
    CHECK(checkedMul(3, 4, out) && out == 12);
    CHECK(checkedMul(0, kMax, out) && out == 0);
    out = 7;
    CHECK(!checkedMul(kMax, 2, out) && out == 7);
    CHECK(!checkedMul(-1, 2, out));
    CHECK(checkedMul(kMax, 1, out) && out == kMax);
    CHECK(!checkedMul(int64_t(1) << 32, int64_t(1) << 31, out));
    CHECK(checkedAdd(kMax - 1, 1, out) && out == kMax);
    CHECK(!checkedAdd(kMax, 1, out));
    CHECK(!checkedAdd(std::numeric_limits<int64_t>::min(), -1, out));
}

static const char *const kAllNames[] = {
    "yuv420p",     "yuv422p",     "yuv444p",     "yv12",        "yuv420p9le",  "yuv420p9be",
    "yuv420p10le", "yuv420p10be", "yuv420p12le", "yuv420p12be", "yuv420p14le", "yuv420p14be",
    "yuv420p16le", "yuv420p16be", "yuv422p9le",  "yuv422p10le", "yuv422p12be", "yuv422p16le",
    "yuv444p9be",  "yuv444p10le", "yuv444p12le", "yuv444p14le", "yuv444p16be", "gray",
    "gray9le",     "gray10le",    "gray10be",    "gray12le",    "gray14be",    "gray16le",
    "gray16be",    "nv12",        "nv21",        "nv16",        "nv24",        "p010le",
    "p010be",      "p016le",      "p016be",      "p210le",      "p410le",      "yuyv422",
    "uyvy422",     "yvyu422"};

static void testNamesRoundTrip()
{
    for (const char *name : kAllNames)
    {
        const Format f = fmt(name, 64, 32);
        CHECK(pixFmtName(f) == name);
        const std::string text = toString(f);
        CHECK(!text.empty());
        const auto back = parseFormatString(text);
        CHECK(back.has_value() && *back == f);
        if (!back || !(*back == f))
            std::printf("  round trip failed for %s (%s)\n", name, text.c_str());
    }
    const Matrix matrices[] = {Matrix::BT601, Matrix::BT709, Matrix::BT2020};
    const Range ranges[] = {Range::Limited, Range::Full};
    const ChromaSiting sitings[] = {ChromaSiting::Left, ChromaSiting::Center,
                                    ChromaSiting::TopLeft};
    for (Matrix m : matrices)
        for (Range r : ranges)
            for (ChromaSiting s : sitings)
            {
                Format f = fmt("yuv420p10le", 1920, 1080);
                f.matrix = m;
                f.range = r;
                f.siting = s;
                const auto back = parseFormatString(toString(f));
                CHECK(back && *back == f);
            }
    CHECK(toString(fmt("yuv420p10le", 1920, 1080)) == "1920x1080:yuv420p10le:bt709:limited:left");
}

static void testAliases()
{
    CHECK(pixFmtName(fmt("I420", 2, 2)) == "yuv420p");
    CHECK(pixFmtName(fmt("iyuv", 2, 2)) == "yuv420p");
    CHECK(pixFmtName(fmt("YV12", 2, 2)) == "yv12");
    CHECK(pixFmtName(fmt("yuy2", 2, 2)) == "yuyv422");
    CHECK(pixFmtName(fmt("yuyv", 2, 2)) == "yuyv422");
    CHECK(pixFmtName(fmt("uyvy", 2, 2)) == "uyvy422");
    CHECK(pixFmtName(fmt("y422", 2, 2)) == "uyvy422");
    CHECK(pixFmtName(fmt("yvyu", 2, 2)) == "yvyu422");
    CHECK(pixFmtName(fmt("y800", 2, 2)) == "gray");
    CHECK(pixFmtName(fmt("gray8", 2, 2)) == "gray");
    CHECK(pixFmtName(fmt("y8", 2, 2)) == "gray");
    CHECK(pixFmtName(fmt("i422", 2, 2)) == "yuv422p");
    CHECK(pixFmtName(fmt("i444", 2, 2)) == "yuv444p");
    CHECK(pixFmtName(fmt("gray16", 2, 2)) == "gray16le");
    CHECK(pixFmtName(fmt("gray10", 2, 2)) == "gray10le");
    CHECK(pixFmtName(fmt("P010", 2, 2)) == "p010le");
    CHECK(pixFmtName(fmt("p016", 2, 2)) == "p016le");
    CHECK(pixFmtName(fmt("yuv420p10", 2, 2)) == "yuv420p10le");
    CHECK(pixFmtName(fmt("yuv444p12", 2, 2)) == "yuv444p12le");

    Format keep = fmt("nv12", 8, 8);
    const Format before = keep;
    CHECK(!applyPixFmtName("nope", keep) && keep == before);
    CHECK(!applyPixFmtName("", keep) && keep == before);

    Matrix m = Matrix::BT709;
    CHECK(parseMatrix("SMPTE170M", m) && m == Matrix::BT601);
    CHECK(parseMatrix("bt2020nc", m) && m == Matrix::BT2020);
    CHECK(!parseMatrix("bt999", m));
    Range r = Range::Limited;
    CHECK(parseRange("PC", r) && r == Range::Full);
    CHECK(parseRange("tv", r) && r == Range::Limited);
    ChromaSiting s = ChromaSiting::Left;
    CHECK(parseSiting("TopLeft", s) && s == ChromaSiting::TopLeft);
    CHECK(!parseSiting("middle", s));
}

static void testParseRejects()
{
    const char *const bad[] = {"",
                               "x",
                               "1920x",
                               "x1080",
                               "1920x1080",
                               "1920x1080:",
                               "1920x1080:nope",
                               "+1920x1080:yuv420p",
                               "-1920x1080:yuv420p",
                               "1920x-1080:yuv420p",
                               "99999999999999999999x2:yuv420p",
                               "1920x1080:yuv420p:bt709:limited:left:extra",
                               "1920x1080:yuv420p:bt999",
                               "1920x1080:yuv420p:bt709:wide",
                               "1920x1080:yuv420p:bt709:full:middle",
                               "1920x1x1080:yuv420p",
                               "0x0:yuv420p",
                               "65537x16:yuv420p",
                               "1920 x1080:yuv420p",
                               "1920x1080::bt709"};
    for (const char *text : bad)
    {
        const bool rejected = !parseFormatString(text).has_value();
        CHECK(rejected);
        if (!rejected)
            std::printf("  accepted bad input: '%s'\n", text);
    }
    const auto ok = parseFormatString("352x288:I420");
    CHECK(ok && ok->matrix == Matrix::BT709 && ok->range == Range::Limited &&
          ok->siting == ChromaSiting::Left);
}

static uint32_t g_rng = 0x12345678u;
static uint32_t nextRand()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void testFuzz()
{
    static const char kAlphabet[] = "0123456789xX:yuvplebgranv12-+ \xff";
    const size_t alphabetSize = sizeof(kAlphabet); // includes the trailing NUL
    int accepted = 0;
    for (int i = 0; i < 20000; ++i)
    {
        std::string s;
        const uint32_t len = nextRand() % 65;
        for (uint32_t k = 0; k < len; ++k)
            s.push_back(kAlphabet[nextRand() % alphabetSize]);
        if (i % 4 == 0)
            s = "64x32:" + s; // bias toward the pix-fmt field
        const auto parsed = parseFormatString(s);
        if (parsed)
        {
            ++accepted;
            CHECK(isValid(*parsed));
            const auto again = parseFormatString(toString(*parsed));
            CHECK(again && *again == *parsed);
        }
        Format f;
        f.width = 16;
        f.height = 16;
        if (applyPixFmtName(s, f))
            CHECK(!pixFmtName(f).empty());
    }
    std::printf("  fuzz accepted %d inputs\n", accepted);
}

static bool containsName(const std::vector<std::string> &names, const char *wanted)
{
    for (const std::string &name : names)
    {
        if (name == wanted)
            return true;
    }
    return false;
}

static void testCanonicalNames()
{
    const std::vector<std::string> names = canonicalPixFmtNames();
    CHECK(!names.empty());
    CHECK(containsName(names, "yuv420p"));
    CHECK(containsName(names, "nv12"));
    CHECK(containsName(names, "p010le"));
    CHECK(containsName(names, "yuyv422"));
    CHECK(containsName(names, "gray"));
    CHECK(!containsName(names, "i420"));
    for (const std::string &name : names)
    {
        Format f;
        f.width = 16;
        f.height = 16;
        CHECK(applyPixFmtName(name, f));
        CHECK(pixFmtName(f) == name);
    }
}

static void testFrameCount()
{
    FrameCount c = computeFrameCount(3110400 * 10, 0, 0, 3110400);
    CHECK(c.frames == 10 && c.trailingBytes == 0);
    c = computeFrameCount(3110400 * 10 + 100, 0, 0, 3110400);
    CHECK(c.frames == 10 && c.trailingBytes == 100);
    c = computeFrameCount(3110400 * 3 - 1, 0, 0, 3110400);
    CHECK(c.frames == 2 && c.trailingBytes == 3110400 - 1);
    c = computeFrameCount(40 + 3 * (6 + 12), 40, 6, 12);
    CHECK(c.frames == 3 && c.trailingBytes == 0);
    c = computeFrameCount(39, 40, 6, 12);
    CHECK(c.frames == 0);
    c = computeFrameCount(1000, 0, 0, 0);
    CHECK(c.frames == 0);
    c = computeFrameCount(-5, 0, 0, 10);
    CHECK(c.frames == 0);
    c = computeFrameCount(100, -1, 0, 10);
    CHECK(c.frames == 0);
    c = computeFrameCount(100, 0, -1, 10);
    CHECK(c.frames == 0);
    const int64_t kMax = std::numeric_limits<int64_t>::max();
    c = computeFrameCount(kMax, 0, 0, 3);
    CHECK(c.frames == kMax / 3 && c.trailingBytes == kMax % 3);
    c = computeFrameCount(kMax, 0, kMax, 1);
    CHECK(c.frames == 0);
}

int main()
{
    testPlanarGeometry();
    testOddSizes();
    testHighBitDepth();
    testRejects();
    testCheckedMath();
    testNamesRoundTrip();
    testAliases();
    testParseRejects();
    testFuzz();
    testCanonicalNames();
    testFrameCount();
    if (g_failures == 0)
        std::printf("test_yuv_format: all passed\n");
    else
        std::printf("test_yuv_format: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
