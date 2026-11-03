// NOLINTBEGIN
// Raw Y/U/V sample probe for the pixel inspector.

#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvSampleProbe.h"
#include "core/image/yuv/YuvSource.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using mviewer::core::yuv::applyPixFmtName;
using mviewer::core::yuv::Format;
using mviewer::core::yuv::formatYuvSample;
using mviewer::core::yuv::normalizedSample;
using mviewer::core::yuv::sampleYuvPlanes;
using mviewer::core::yuv::Subsampling;
using mviewer::core::yuv::YuvPlanes;
using mviewer::core::yuv::YuvSampleProbe;

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

static std::filesystem::path g_root;

static void writeBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(path, std::ios::binary);
    CHECK(out.good());
    if (!bytes.empty())
    {
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(out.good());
}

static Format makeFormat(const char *pix, int width, int height, int bitDepth)
{
    Format format;
    format.width = width;
    format.height = height;
    format.bitDepth = bitDepth;
    CHECK(applyPixFmtName(pix, format));
    format.width = width;
    format.height = height;
    format.bitDepth = bitDepth;
    return format;
}

static void fillPlane(std::vector<std::uint16_t> &plane, int width, int height, std::uint16_t base)
{
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    plane.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        plane[i] = static_cast<std::uint16_t>(base + static_cast<std::uint16_t>(i));
}

static YuvPlanes makePlanes(const Format &format, std::uint16_t yBase, std::uint16_t uBase,
                            std::uint16_t vBase)
{
    YuvPlanes planes;
    planes.width = format.width;
    planes.height = format.height;
    planes.bitDepth = format.bitDepth;
    fillPlane(planes.y, format.width, format.height, yBase);
    if (format.subsampling != Subsampling::S400)
    {
        const int cw =
            (format.subsampling == Subsampling::S444) ? format.width : (format.width + 1) / 2;
        const int ch =
            (format.subsampling == Subsampling::S420) ? (format.height + 1) / 2 : format.height;
        planes.chromaWidth = cw;
        planes.chromaHeight = ch;
        fillPlane(planes.u, cw, ch, uBase);
        fillPlane(planes.v, cw, ch, vBase);
    }
    return planes;
}

static std::uint16_t at(const std::vector<std::uint16_t> &plane, int width, int x, int y)
{
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    return plane[index];
}

static void testPlanes()
{
    const Format y420 = makeFormat("yuv420p", 5, 3, 8);
    const YuvPlanes p420 = makePlanes(y420, 1, 100, 200);
    CHECK(p420.chromaWidth == 3);
    CHECK(p420.chromaHeight == 2);

    const auto c00 = sampleYuvPlanes(p420, y420, 0, 0);
    CHECK(c00.valid);
    CHECK(c00.chromaX == 0 && c00.chromaY == 0);
    CHECK(c00.luma == at(p420.y, 5, 0, 0));
    CHECK(c00.cb == at(p420.u, 3, 0, 0));
    CHECK(c00.cr == at(p420.v, 3, 0, 0));
    CHECK(formatYuvSample(c00) == "Y=1 U=100 V=200 (8-bit, chroma 0,0)");

    const auto odd = sampleYuvPlanes(p420, y420, 1, 1);
    CHECK(odd.valid);
    CHECK(odd.chromaX == 0 && odd.chromaY == 0);
    CHECK(odd.luma == at(p420.y, 5, 1, 1));
    CHECK(odd.cb == c00.cb && odd.cr == c00.cr);

    const auto mid = sampleYuvPlanes(p420, y420, 2, 2);
    CHECK(mid.valid);
    CHECK(mid.chromaX == 1 && mid.chromaY == 1);
    CHECK(mid.cb == at(p420.u, 3, 1, 1));
    CHECK(mid.cr == at(p420.v, 3, 1, 1));

    const auto corner = sampleYuvPlanes(p420, y420, 4, 2);
    CHECK(corner.valid);
    CHECK(corner.chromaX == 2 && corner.chromaY == 1);
    CHECK(corner.luma == at(p420.y, 5, 4, 2));
    CHECK(corner.cb == at(p420.u, 3, 2, 1));

    const Format y444 = makeFormat("yuv444p", 4, 2, 8);
    const YuvPlanes p444 = makePlanes(y444, 10, 20, 30);
    const auto s444 = sampleYuvPlanes(p444, y444, 3, 1);
    CHECK(s444.valid);
    CHECK(s444.chromaX == 3 && s444.chromaY == 1);
    CHECK(s444.cb == at(p444.u, 4, 3, 1));
    CHECK(s444.cr == at(p444.v, 4, 3, 1));

    const Format y422 = makeFormat("yuv422p", 4, 2, 8);
    const YuvPlanes p422 = makePlanes(y422, 40, 50, 60);
    CHECK(p422.chromaWidth == 2);
    CHECK(p422.chromaHeight == 2);
    const auto s422 = sampleYuvPlanes(p422, y422, 3, 1);
    CHECK(s422.valid);
    CHECK(s422.chromaX == 1);
    CHECK(s422.chromaY == 1);
    CHECK(s422.cb == at(p422.u, 2, 1, 1));

    const Format gray = makeFormat("gray", 4, 2, 8);
    const YuvPlanes pGray = makePlanes(gray, 235, 0, 0);
    const auto g = sampleYuvPlanes(pGray, gray, 1, 0);
    CHECK(g.valid);
    CHECK(!g.hasChroma);
    CHECK(g.luma == at(pGray.y, 4, 1, 0));
    CHECK(g.cb == 0 && g.cr == 0);
    const std::string grayText = formatYuvSample(g);
    CHECK(grayText.find("U=") == std::string::npos);
    CHECK(grayText.find("Y=") == 0);
    CHECK(grayText.find("(8-bit)") != std::string::npos);

    Format ten = makeFormat("yuv420p10le", 2, 2, 10);
    YuvPlanes p10 = makePlanes(ten, 0, 0, 0);
    p10.y[0] = 1023;
    p10.u[0] = 512;
    p10.v[0] = 0;
    p10.bitDepth = 10;
    const auto s10 = sampleYuvPlanes(p10, ten, 0, 0);
    CHECK(s10.valid);
    CHECK(s10.luma == 1023);
    CHECK(s10.bitDepth == 10);
    const std::string tenText = formatYuvSample(s10);
    CHECK(tenText.find("10-bit") != std::string::npos);
    CHECK(tenText.find("Y=1023") != std::string::npos);

    CHECK(!sampleYuvPlanes(p420, y420, -1, 0).valid);
    CHECK(!sampleYuvPlanes(p420, y420, 0, -1).valid);
    CHECK(!sampleYuvPlanes(p420, y420, 5, 0).valid);
    CHECK(!sampleYuvPlanes(p420, y420, 0, 3).valid);
    CHECK(!sampleYuvPlanes(p420, y420, INT_MAX, INT_MAX).valid);
    CHECK(formatYuvSample(sampleYuvPlanes(p420, y420, -1, -1)).empty());

    YuvPlanes empty;
    empty.width = 5;
    empty.height = 3;
    CHECK(!sampleYuvPlanes(empty, y420, 0, 0).valid);
}

static void testNormalized()
{
    CHECK(normalizedSample(1023, 10) == 1.0);
    CHECK(normalizedSample(0, 8) == 0.0);
    const double mid = normalizedSample(128, 8);
    const double expect = 128.0 / 255.0;
    CHECK(mid > expect - 0.00001 && mid < expect + 0.00001);
    CHECK(mid > 0.50196 && mid < 0.50197);
}

static std::vector<std::uint8_t> frame420(std::uint8_t y, std::uint8_t u, std::uint8_t v)
{
    std::vector<std::uint8_t> frame(12, y);
    frame[8] = u;
    frame[9] = u;
    frame[10] = v;
    frame[11] = v;
    return frame;
}

static void testProbe()
{
    // "4x2" is not a size token (each side needs 2..5 digits). The parent folder is.
    const auto dir = g_root / "04x02";
    std::filesystem::create_directories(dir);
    const auto path = dir / "probe_4x2_yuv420p.yuv";
    std::vector<std::uint8_t> bytes;
    const std::uint8_t ys[3] = {10, 40, 70};
    const std::uint8_t us[3] = {20, 50, 80};
    const std::uint8_t vs[3] = {30, 60, 90};
    for (int i = 0; i < 3; ++i)
    {
        const auto frame = frame420(ys[i], us[i], vs[i]);
        CHECK(frame.size() == 12);
        bytes.insert(bytes.end(), frame.begin(), frame.end());
    }
    writeBytes(path, bytes);
    const std::string key = path.string();

    YuvSampleProbe::instance().clear();
    const uint64_t before = YuvSampleProbe::instance().readCount();
    for (int i = 0; i < 3; ++i)
    {
        const auto s = YuvSampleProbe::instance().sample(key, i, 1, 1);
        CHECK(s.valid);
        CHECK(s.frameIndex == i);
        CHECK(s.luma == ys[i]);
        CHECK(s.cb == us[i]);
        CHECK(s.cr == vs[i]);
        CHECK(s.hasChroma);
        CHECK(s.chromaX == 0 && s.chromaY == 0);
        CHECK(s.bitDepth == 8);
    }
    const uint64_t afterThree = YuvSampleProbe::instance().readCount();
    CHECK(afterThree == before + 3);

    const auto again = YuvSampleProbe::instance().sample(key, 2, 3, 0);
    CHECK(again.valid);
    CHECK(again.luma == 70);
    CHECK(again.cb == 80);
    CHECK(again.cr == 90);
    CHECK(again.chromaX == 1);
    CHECK(YuvSampleProbe::instance().readCount() == afterThree);

    const auto past = YuvSampleProbe::instance().sample(key, 3, 0, 0);
    CHECK(!past.valid);
    CHECK(past.frameIndex == 3);
    const auto neg = YuvSampleProbe::instance().sample(key, -1, 0, 0);
    CHECK(!neg.valid);
    CHECK(YuvSampleProbe::instance().readCount() == afterThree);

    const auto unknownPath = g_root / "unknown.yuv";
    writeBytes(unknownPath, std::vector<std::uint8_t>(7, 1));
    const auto unknown = YuvSampleProbe::instance().sample(unknownPath.string(), 0, 0, 0);
    CHECK(!unknown.valid);

    const auto missing =
        YuvSampleProbe::instance().sample((g_root / "no_such_file.yuv").string(), 0, 0, 0);
    CHECK(!missing.valid);

    const auto truncPath = dir / "trunc_4x2_yuv420p.yuv";
    std::vector<std::uint8_t> trunc = bytes;
    trunc.resize(30);
    writeBytes(truncPath, trunc);
    const auto frame2 = YuvSampleProbe::instance().sample(truncPath.string(), 2, 0, 0);
    CHECK(!frame2.valid);
    const auto frame0 = YuvSampleProbe::instance().sample(truncPath.string(), 0, 0, 0);
    CHECK(frame0.valid);
    CHECK(frame0.luma == 10);

    const uint64_t beforeClear = YuvSampleProbe::instance().readCount();
    YuvSampleProbe::instance().clear();
    const auto reread = YuvSampleProbe::instance().sample(key, 1, 0, 0);
    CHECK(reread.valid);
    CHECK(reread.luma == 40);
    CHECK(reread.cb == 50);
    CHECK(YuvSampleProbe::instance().readCount() == beforeClear + 1);
}

static void testFuzz()
{
    const Format format = makeFormat("yuv420p", 7, 5, 8);
    const YuvPlanes planes = makePlanes(format, 1, 2, 3);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(-10, 7 + 10);
    std::uniform_int_distribution<int> distY(-10, 5 + 10);
    for (int i = 0; i < 2000; ++i)
    {
        const int x = dist(rng);
        const int y = distY(rng);
        const auto s = sampleYuvPlanes(planes, format, x, y);
        const bool inRange = x >= 0 && y >= 0 && x < 7 && y < 5;
        CHECK(s.valid == inRange);
        if (s.valid)
        {
            CHECK(s.chromaX == (x >> 1));
            CHECK(s.chromaY == (y >> 1));
            CHECK(s.luma == at(planes.y, 7, x, y));
        }
    }
}

int main()
{
    g_root = std::filesystem::temp_directory_path() / "mviewer_yuv_sample_probe";
    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);
    CHECK(!ec);

    testPlanes();
    testNormalized();
    testProbe();
    testFuzz();

    std::filesystem::remove_all(g_root, ec);
    if (g_failures != 0)
    {
        std::printf("%d failed\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
