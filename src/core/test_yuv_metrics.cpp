// NOLINTBEGIN
#include "core/analysis/YuvMetrics.h"
#include "core/image/yuv/YuvFormat.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using mviewer::core::yuv::applyPixFmtName;
using mviewer::core::yuv::comparePlane;
using mviewer::core::yuv::compareYuvFiles;
using mviewer::core::yuv::compareYuvFrames;
using mviewer::core::yuv::Format;
using mviewer::core::yuv::formatYuvFrameMetrics;
using mviewer::core::yuv::PlaneMetrics;
using mviewer::core::yuv::Subsampling;
using mviewer::core::yuv::yuvComparable;
using mviewer::core::yuv::YuvFrameMetrics;
using mviewer::core::yuv::YuvPlanes;

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

static bool near(double value, double expect, double tol)
{
    return std::fabs(value - expect) <= tol;
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

static std::vector<std::uint16_t> filled(int width, int height, std::uint16_t value)
{
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return std::vector<std::uint16_t>(n, value);
}

static std::vector<std::uint16_t> checker(int width, int height, std::uint16_t on,
                                          std::uint16_t off)
{
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint16_t> plane(n);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            const bool dark = ((x + y) % 2) == 0;
            plane[index] = dark ? on : off;
        }
    }
    return plane;
}

static void testIdentical()
{
    const auto a = filled(8, 8, 40);
    const PlaneMetrics m8 = comparePlane(a, a, 8, 8, 8);
    CHECK(m8.mse == 0.0);
    CHECK(std::isinf(m8.psnr));
    CHECK(near(m8.ssim, 1.0, 1e-9));

    const auto b = filled(16, 8, 1000);
    const PlaneMetrics m10 = comparePlane(b, b, 16, 8, 10);
    CHECK(m10.mse == 0.0);
    CHECK(std::isinf(m10.psnr));
    CHECK(near(m10.ssim, 1.0, 1e-9));
}

static void testGoldenMse()
{
    const auto a = filled(4, 4, 100);
    const auto b = filled(4, 4, 110);
    const PlaneMetrics m = comparePlane(a, b, 4, 4, 8);
    const double expect = 10.0 * std::log10((255.0 * 255.0) / 100.0);
    CHECK(m.mse == 100.0);
    CHECK(near(m.psnr, expect, 1e-3));
    CHECK(near(m.psnr, 28.1308, 1e-3));

    const auto c = filled(4, 4, 0);
    const auto d = filled(4, 4, 4);
    const PlaneMetrics m10 = comparePlane(c, d, 4, 4, 10);
    const double expect10 = 10.0 * std::log10((1023.0 * 1023.0) / 16.0);
    CHECK(m10.mse == 16.0);
    CHECK(near(m10.psnr, expect10, 1e-3));
}

static void testSsim()
{
    const auto lo = filled(16, 16, 20);
    const auto hi = filled(16, 16, 200);
    const PlaneMetrics constants = comparePlane(lo, hi, 16, 16, 8);
    CHECK(constants.ssim > 0.0);
    CHECK(constants.ssim < 1.0);

    const auto chk = checker(16, 16, 0, 255);
    const auto inv = checker(16, 16, 255, 0);
    const PlaneMetrics low = comparePlane(chk, inv, 16, 16, 8);
    CHECK(low.ssim < 0.1);
    const PlaneMetrics swapped = comparePlane(inv, chk, 16, 16, 8);
    CHECK(near(low.ssim, swapped.ssim, 1e-12));

    const int sizes[3][2] = {{5, 3}, {1, 1}, {9, 7}};
    for (const auto &size : sizes)
    {
        const auto left = checker(size[0], size[1], 12, 200);
        const auto right = checker(size[0], size[1], 80, 3);
        const PlaneMetrics odd = comparePlane(left, right, size[0], size[1], 8);
        CHECK(odd.ssim >= -1.0);
        CHECK(odd.ssim <= 1.0);
        const PlaneMetrics back = comparePlane(right, left, size[0], size[1], 8);
        CHECK(near(odd.ssim, back.ssim, 1e-12));
    }
}

static YuvPlanes constantFrame(const Format &format, std::uint16_t y, std::uint16_t u,
                               std::uint16_t v)
{
    YuvPlanes planes;
    planes.width = format.width;
    planes.height = format.height;
    planes.bitDepth = format.bitDepth;
    planes.y = filled(format.width, format.height, y);
    if (format.subsampling == Subsampling::S400)
        return planes;
    const int cw =
        (format.subsampling == Subsampling::S444) ? format.width : (format.width + 1) / 2;
    const int ch =
        (format.subsampling == Subsampling::S420) ? (format.height + 1) / 2 : format.height;
    planes.chromaWidth = cw;
    planes.chromaHeight = ch;
    planes.u = filled(cw, ch, u);
    planes.v = filled(cw, ch, v);
    return planes;
}

static void testAverages()
{
    const Format format = makeFormat("yuv420p", 8, 8, 8);
    const YuvPlanes a = constantFrame(format, 10, 20, 30);
    YuvPlanes b = constantFrame(format, 20, 20, 40);
    const YuvFrameMetrics finite = compareYuvFrames(a, b);
    CHECK(finite.valid);
    CHECK(finite.planeCount == 3);
    CHECK(std::isinf(finite.planes[1].psnr));
    CHECK(std::isinf(finite.psnr611));
    CHECK(std::isinf(finite.psnr111));
    CHECK(near(finite.ssim611,
               (6.0 * finite.planes[0].ssim + finite.planes[1].ssim + finite.planes[2].ssim) / 8.0,
               1e-12));
    CHECK(near(finite.ssim111,
               (finite.planes[0].ssim + finite.planes[1].ssim + finite.planes[2].ssim) / 3.0,
               1e-12));

    b = constantFrame(format, 20, 30, 50);
    const YuvFrameMetrics allFinite = compareYuvFrames(a, b);
    CHECK(allFinite.valid);
    CHECK(!std::isinf(allFinite.planes[0].psnr));
    CHECK(!std::isinf(allFinite.planes[1].psnr));
    CHECK(!std::isinf(allFinite.planes[2].psnr));
    const double p611 =
        (6.0 * allFinite.planes[0].psnr + allFinite.planes[1].psnr + allFinite.planes[2].psnr) /
        8.0;
    const double p111 =
        (allFinite.planes[0].psnr + allFinite.planes[1].psnr + allFinite.planes[2].psnr) / 3.0;
    CHECK(near(allFinite.psnr611, p611, 1e-9));
    CHECK(near(allFinite.psnr111, p111, 1e-9));

    const Format gray = makeFormat("gray", 8, 8, 8);
    const YuvFrameMetrics g =
        compareYuvFrames(constantFrame(gray, 15, 0, 0), constantFrame(gray, 15, 0, 0));
    CHECK(g.valid);
    CHECK(g.planeCount == 1);
    CHECK(std::isinf(g.psnr611));
    CHECK(std::isinf(g.psnr111));
    CHECK(near(g.ssim611, g.planes[0].ssim, 1e-12));
    CHECK(near(g.ssim111, g.planes[0].ssim, 1e-12));
    const std::string text = formatYuvFrameMetrics(g);
    CHECK(text.find("U ") == std::string::npos);
    CHECK(text.find("inf") != std::string::npos);
    CHECK(text.find("SSIM Y 1.0000") != std::string::npos);

    const std::string color = formatYuvFrameMetrics(allFinite);
    CHECK(color.find("6:1:1 ") != std::string::npos);
    CHECK(color.find("SSIM Y ") != std::string::npos);
}

static void testComparable()
{
    Format a = makeFormat("yuv420p", 1920, 1080, 8);
    Format b = makeFormat("yuv420p", 1280, 720, 8);
    std::string why;
    CHECK(!yuvComparable(a, b, &why));
    CHECK(why == "size differs: 1920x1080 vs 1280x720");

    b = makeFormat("yuv444p", 1920, 1080, 8);
    CHECK(!yuvComparable(a, b, &why));
    CHECK(why == "subsampling differs: 4:2:0 vs 4:4:4");

    b = makeFormat("yuv420p10le", 1920, 1080, 10);
    CHECK(!yuvComparable(a, b, &why));
    CHECK(why == "bit depth differs: 8 vs 10");

    Format nv = makeFormat("nv12", 1920, 1080, 8);
    CHECK(yuvComparable(a, nv, &why));
}

static void testRejectFrames()
{
    const Format format = makeFormat("yuv420p", 4, 4, 8);
    YuvPlanes a = constantFrame(format, 1, 2, 3);
    YuvPlanes b = constantFrame(format, 1, 2, 3);
    a.y.pop_back();
    const YuvFrameMetrics shortPlane = compareYuvFrames(a, b);
    CHECK(!shortPlane.valid);
    CHECK(!shortPlane.why.empty());

    b.width = 2;
    a = constantFrame(format, 1, 2, 3);
    const YuvFrameMetrics mismatch = compareYuvFrames(a, b);
    CHECK(!mismatch.valid);
}

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

static std::vector<std::uint8_t> frame420(std::uint8_t y, std::uint8_t u, std::uint8_t v)
{
    std::vector<std::uint8_t> frame(12, y);
    frame[8] = u;
    frame[9] = u;
    frame[10] = v;
    frame[11] = v;
    return frame;
}

static void testFiles(const std::filesystem::path &root)
{
    const auto dir = root / "04x02";
    std::filesystem::create_directories(dir);
    std::vector<std::uint8_t> three;
    std::vector<std::uint8_t> two;
    for (int i = 0; i < 3; ++i)
    {
        const auto frame = frame420(static_cast<std::uint8_t>(10 + i), 20, 30);
        three.insert(three.end(), frame.begin(), frame.end());
        if (i < 2)
            two.insert(two.end(), frame.begin(), frame.end());
    }
    const auto pathA = dir / "a_yuv420p.yuv";
    const auto pathB = dir / "b_yuv420p.yuv";
    const auto pathC = dir / "a_copy_yuv420p.yuv";
    const auto path444 = dir / "x_yuv444p.yuv";
    writeBytes(pathA, three);
    writeBytes(pathB, two);
    writeBytes(pathC, three);
    writeBytes(path444, std::vector<std::uint8_t>(48, 0));

    const auto pair = compareYuvFiles(pathA.string(), pathB.string());
    CHECK(pair.valid);
    CHECK(pair.framesCompared == 2);
    CHECK(pair.framesA == 3);
    CHECK(pair.framesB == 2);

    const auto one = compareYuvFiles(pathA.string(), pathB.string(), 1);
    CHECK(one.valid);
    CHECK(one.framesCompared == 1);

    const auto cancelled =
        compareYuvFiles(pathA.string(), pathB.string(), 0, [](int64_t, int64_t) { return false; });
    CHECK(!cancelled.valid);
    CHECK(cancelled.why == "cancelled");

    const auto sub = compareYuvFiles(pathA.string(), path444.string());
    CHECK(!sub.valid);
    CHECK(sub.why.find("subsampling") != std::string::npos);

    const auto missing = compareYuvFiles((dir / "missing_yuv420p.yuv").string(), pathA.string());
    CHECK(!missing.valid);
    CHECK(missing.why.rfind("A: ", 0) == 0);

    const auto missingB = compareYuvFiles(pathA.string(), (dir / "no_b_yuv420p.yuv").string());
    CHECK(!missingB.valid);
    CHECK(missingB.why.rfind("B: ", 0) == 0);

    const auto same = compareYuvFiles(pathA.string(), pathC.string());
    CHECK(same.valid);
    CHECK(same.framesCompared == 3);
    CHECK(std::isinf(same.mean.psnr611));
    CHECK(std::isinf(same.mean.planes[0].psnr));
}

static void testInt64()
{
    const std::size_t n = static_cast<std::size_t>(1024) * static_cast<std::size_t>(1024);
    const std::vector<std::uint16_t> zeros(n, 0);
    const std::vector<std::uint16_t> ones(n, 65535);
    const PlaneMetrics m = comparePlane(zeros, ones, 1024, 1024, 16);
    const double expect = 65535.0 * 65535.0;
    CHECK(m.mse == expect);
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "mviewer_yuv_metrics_b8a";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    testIdentical();
    testGoldenMse();
    testSsim();
    testAverages();
    testComparable();
    testRejectFrames();
    testFiles(root);
    testInt64();

    std::filesystem::remove_all(root);
    if (g_failures != 0)
    {
        std::printf("test_yuv_metrics: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_yuv_metrics: all passed\n");
    return 0;
}
// NOLINTEND
