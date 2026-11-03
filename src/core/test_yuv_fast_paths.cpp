// NOLINTBEGIN
// Fast YUV paths must match the straightforward reference results exactly:
// SSIM integer windows, raw-frame thumbnails, threaded RGB conversion, plane
// buffer reuse, frame-parallel sequence compare and the y4m resolve cache.
#include "core/analysis/YuvMetrics.h"
#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvSource.h"
#include "core/image/yuv/YuvToRgb.h"
#include "core/image/yuv/YuvUnpack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
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

// ── Reference SSIM (the original two-pass window loop) ──────────────────────
static std::vector<int> refStarts(int size, int window)
{
    std::vector<int> starts;
    const int win = size < window ? size : window;
    if (size <= win)
        return {0};
    for (int pos = 0; pos + win <= size; pos += 4)
        starts.push_back(pos);
    if (starts.back() != size - win)
        starts.push_back(size - win);
    return starts;
}

static double refWindow(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int width,
                        int ox, int oy, int winW, int winH, double c1, double c2)
{
    const double n = static_cast<double>(winW) * static_cast<double>(winH);
    double sumA = 0.0, sumB = 0.0, varA = 0.0, varB = 0.0, cov = 0.0;
    for (int pass = 0; pass < 2; ++pass)
    {
        const double muA = sumA / n;
        const double muB = sumB / n;
        if (pass == 1)
            varA = varB = cov = 0.0;
        for (int y = 0; y < winH; ++y)
        {
            for (int x = 0; x < winW; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(oy + y) * width + (ox + x);
                const double sa = a[i];
                const double sb = b[i];
                if (pass == 0)
                {
                    sumA += sa;
                    sumB += sb;
                }
                else
                {
                    const double da = sa - muA;
                    const double db = sb - muB;
                    varA += da * da;
                    varB += db * db;
                    cov += da * db;
                }
            }
        }
    }
    varA /= n;
    varB /= n;
    cov /= n;
    const double muA = sumA / n;
    const double muB = sumB / n;
    const double num = (2.0 * muA * muB + c1) * (2.0 * cov + c2);
    const double den = (muA * muA + muB * muB + c1) * (varA + varB + c2);
    return den == 0.0 ? 1.0 : num / den;
}

static double refSsim(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int width,
                      int height, int bitDepth)
{
    const double peak = static_cast<double>((std::uint64_t{1} << bitDepth) - 1U);
    const double c1 = (0.01 * peak) * (0.01 * peak);
    const double c2 = (0.03 * peak) * (0.03 * peak);
    const int winW = width < 8 ? width : 8;
    const int winH = height < 8 ? height : 8;
    const auto xs = refStarts(width, 8);
    const auto ys = refStarts(height, 8);
    double sum = 0.0;
    for (int oy : ys)
        for (int ox : xs)
            sum += refWindow(a, b, width, ox, oy, winW, winH, c1, c2);
    return sum / (static_cast<double>(xs.size()) * static_cast<double>(ys.size()));
}

static void testSsimMatchesReference()
{
    std::mt19937 rng(1234);
    const int sizes[][2] = {{8, 8}, {9, 13}, {37, 20}, {64, 64}, {130, 71}, {5, 40}, {3, 3}};
    for (int depth : {8, 10, 12, 13, 14, 16})
    {
        const uint32_t mask = (1u << depth) - 1u;
        for (const auto &s : sizes)
        {
            const int w = s[0], h = s[1];
            std::vector<uint16_t> a(static_cast<std::size_t>(w) * h);
            std::vector<uint16_t> b(a.size());
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                a[i] = static_cast<uint16_t>(rng() & mask);
                // Correlated B so SSIM is not ~0.
                b[i] = static_cast<uint16_t>((a[i] + (rng() & 31u)) & mask);
            }
            const PlaneMetrics m = comparePlane(a, b, w, h, depth);
            const double ref = refSsim(a, b, w, h, depth);
            if (m.ssim != ref)
                std::printf("ssim d%d %dx%d got %a want %a\n", depth, w, h, m.ssim, ref);
            CHECK(m.ssim == ref);
            // Flat and identical planes.
            const PlaneMetrics self = comparePlane(a, a, w, h, depth);
            CHECK(self.ssim == refSsim(a, a, w, h, depth));
            std::vector<uint16_t> flat(a.size(), static_cast<uint16_t>(mask));
            CHECK(comparePlane(flat, flat, w, h, depth).ssim == refSsim(flat, flat, w, h, depth));
        }
    }
}

// ── Helpers for frame files ─────────────────────────────────────────────────
static std::vector<uint8_t> randomBytes(std::size_t n, unsigned seed)
{
    std::vector<uint8_t> v(n);
    std::mt19937 rng(seed);
    for (auto &c : v)
        c = static_cast<uint8_t>(rng());
    return v;
}

static Format formatFor(const char *pix, int w, int h)
{
    Format f;
    CHECK(applyPixFmtName(pix, f));
    f.width = w;
    f.height = h;
    return f;
}

static bool sameImage(const ImageData &a, const ImageData &b)
{
    if (a.isNull() || b.isNull())
        return a.isNull() == b.isNull();
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           *a.buffer == *b.buffer;
}

static void testRawThumbnailMatchesPlanes()
{
    const char *formats[] = {"yuv420p", "nv12", "nv21",        "yuv422p", "yuyv422",    "uyvy422",
                             "yuv444p", "gray", "yuv420p10le", "p010le",  "yuv444p16be"};
    for (const char *pix : formats)
    {
        for (const int w : {97, 320})
        {
            const int h = w == 97 ? 66 : 180;
            Format f = formatFor(pix, w, h);
            const auto layout = computeFrameLayout(f);
            CHECK(layout.has_value());
            if (!layout)
                continue;
            YuvSourceInfo info;
            info.known = true;
            info.format = f;
            info.frameBytes = layout->frameBytes;
            info.frameCount = 2;
            const std::vector<uint8_t> data =
                randomBytes(static_cast<std::size_t>(layout->frameBytes) * 2, 7);
            const ReadAtFn read = [&data](int64_t off, uint8_t *buf, std::size_t n) -> std::size_t
            {
                if (off < 0 || static_cast<std::size_t>(off) >= data.size())
                    return 0;
                const std::size_t got = std::min(n, data.size() - static_cast<std::size_t>(off));
                std::copy_n(data.begin() + off, got, buf);
                return got;
            };
            YuvPlanes planes;
            CHECK(readYuvFrame(info, 1, read, planes, nullptr));
            for (const int edge : {0, 16, 64, 255, 4096})
            {
                for (const ChromaUpsample up : {ChromaUpsample::Nearest, ChromaUpsample::Bilinear})
                {
                    const ImageData want = renderYuvImage(planes, f, edge, up);
                    const ImageData got = readYuvFrameImage(info, 1, read, edge, up);
                    if (!sameImage(want, got))
                        std::printf("thumb %s %dx%d edge %d differs\n", pix, w, h, edge);
                    CHECK(sameImage(want, got));
                }
            }
            std::string why;
            CHECK(readYuvFrameImage(info, 5, read, 64, ChromaUpsample::Nearest, &why).isNull());
            CHECK(!why.empty());
        }
    }
}

// ── Threaded conversion == serial ───────────────────────────────────────────
static void threadedRunner(int rowCount, const std::function<void(int, int)> &fn)
{
    const int bands = 5;
    std::vector<std::thread> threads;
    for (int i = 0; i < bands; ++i)
    {
        const int y0 = rowCount * i / bands;
        const int y1 = rowCount * (i + 1) / bands;
        threads.emplace_back([&fn, y0, y1] { fn(y0, y1); });
    }
    for (auto &t : threads)
        t.join();
}

static void testThreadedConvertMatchesSerial()
{
    const YuvRowsRunner saved = yuvRowsRunner();
    for (const char *pix : {"yuv420p", "nv21", "yuv422p10le", "yuv444p", "gray"})
    {
        Format f = formatFor(pix, 1282, 1001);
        const auto layout = computeFrameLayout(f);
        const auto data = randomBytes(static_cast<std::size_t>(layout->frameBytes), 3);
        YuvPlanes planes;
        CHECK(unpackYuvFrame(f, data.data(), data.size(), planes, nullptr));
        for (const ChromaUpsample up : {ChromaUpsample::Nearest, ChromaUpsample::Bilinear})
        {
            setYuvRowsRunner(nullptr);
            const auto serial = toRgb24(planes, f, up);
            setYuvRowsRunner(&threadedRunner);
            const auto threaded = toRgb24(planes, f, up);
            CHECK(!serial.empty());
            CHECK(serial == threaded);
        }
    }
    setYuvRowsRunner(saved);
}

// ── Plane buffers reused across formats ─────────────────────────────────────
static void testUnpackReuse()
{
    YuvPlanes reused;
    const char *order[] = {"yuv444p16le", "gray", "nv12", "yuyv422", "yuv420p10le", "yuv444p"};
    unsigned seed = 11;
    for (const char *pix : order)
    {
        Format f = formatFor(pix, 50, 30);
        const auto layout = computeFrameLayout(f);
        const auto data = randomBytes(static_cast<std::size_t>(layout->frameBytes), seed++);
        YuvPlanes fresh;
        CHECK(unpackYuvFrame(f, data.data(), data.size(), fresh, nullptr));
        CHECK(unpackYuvFrame(f, data.data(), data.size(), reused, nullptr));
        CHECK(fresh.width == reused.width && fresh.height == reused.height);
        CHECK(fresh.chromaWidth == reused.chromaWidth && fresh.bitDepth == reused.bitDepth);
        CHECK(fresh.y == reused.y && fresh.u == reused.u && fresh.v == reused.v);
    }
    Format f = formatFor("yuv420p", 50, 30);
    std::vector<uint8_t> tiny(10);
    CHECK(!unpackYuvFrame(f, tiny.data(), tiny.size(), reused, nullptr));
}

// ── Frame-parallel compareYuvFiles == per-frame compare ─────────────────────
static void writeFile(const std::filesystem::path &p, const std::vector<uint8_t> &bytes)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

static void testParallelCompare(const std::filesystem::path &dir)
{
    const int frames = 23;
    Format f = formatFor("yuv420p", 48, 32);
    const auto layout = computeFrameLayout(f);
    const auto frameBytes = static_cast<std::size_t>(layout->frameBytes);
    const auto bytesA = randomBytes(frameBytes * frames, 21);
    auto bytesB = bytesA;
    std::mt19937 rng(5);
    for (auto &c : bytesB)
        c = static_cast<uint8_t>(c + (rng() & 7u));
    const auto pathA = dir / "fast_a_48x32_yuv420p.yuv";
    const auto pathB = dir / "fast_b_48x32_yuv420p.yuv";
    writeFile(pathA, bytesA);
    writeFile(pathB, bytesB);

    std::vector<int64_t> seen;
    const auto result = compareYuvFiles(pathA.string(), pathB.string(), 0,
                                        [&seen](int64_t index, int64_t total)
                                        {
                                            CHECK(total == frames);
                                            seen.push_back(index);
                                            return true;
                                        });
    CHECK(result.valid);
    CHECK(result.framesCompared == frames);
    CHECK(static_cast<int>(result.frames.size()) == frames);
    CHECK(static_cast<int>(seen.size()) == frames + 1);
    for (std::size_t i = 0; i < seen.size(); ++i)
        CHECK(seen[i] == static_cast<int64_t>(i));
    for (int i = 0; i < frames && i < static_cast<int>(result.frames.size()); ++i)
    {
        YuvPlanes a;
        YuvPlanes b;
        CHECK(unpackYuvFrame(f, bytesA.data() + frameBytes * i, frameBytes, a, nullptr));
        CHECK(unpackYuvFrame(f, bytesB.data() + frameBytes * i, frameBytes, b, nullptr));
        const YuvFrameMetrics want = compareYuvFrames(a, b);
        const YuvFrameMetrics &got = result.frames[static_cast<std::size_t>(i)];
        for (int k = 0; k < want.planeCount; ++k)
        {
            CHECK(got.planes[k].mse == want.planes[k].mse);
            CHECK(got.planes[k].ssim == want.planes[k].ssim);
        }
        CHECK(got.ssim611 == want.ssim611);
    }

    const auto limited = compareYuvFiles(pathA.string(), pathB.string(), 7);
    CHECK(limited.valid && limited.framesCompared == 7);

    const auto cancelled = compareYuvFiles(pathA.string(), pathB.string(), 0,
                                           [](int64_t index, int64_t) { return index < 9; });
    CHECK(!cancelled.valid);
    CHECK(cancelled.why == "cancelled");
    CHECK(cancelled.framesCompared == 9);
    CHECK(cancelled.frames.size() == 9);
}

// ── y4m resolve cache sees file changes ─────────────────────────────────────
static void writeY4m(const std::filesystem::path &p, int w, int h, int frames)
{
    std::string text =
        "YUV4MPEG2 W" + std::to_string(w) + " H" + std::to_string(h) + " F25:1 Ip A1:1 C420jpeg\n";
    std::vector<uint8_t> bytes(text.begin(), text.end());
    const std::size_t fb = static_cast<std::size_t>(w) * h * 3 / 2;
    for (int i = 0; i < frames; ++i)
    {
        const std::string tag = i % 2 ? "FRAME Ixyz\n" : "FRAME\n";
        bytes.insert(bytes.end(), tag.begin(), tag.end());
        bytes.insert(bytes.end(), fb, static_cast<uint8_t>(i));
    }
    writeFile(p, bytes);
}

static void testY4mCache(const std::filesystem::path &dir)
{
    const auto path = dir / "fast_cache.y4m";
    writeY4m(path, 16, 8, 4);
    OpenedYuvFile first;
    CHECK(openYuvFile(path.string(), first));
    CHECK(first.info.known && first.info.frameCount == 4);
    OpenedYuvFile again;
    CHECK(openYuvFile(path.string(), again));
    CHECK(again.info.frameCount == 4 && again.info.format == first.info.format);
    YuvPlanes planes;
    CHECK(readYuvFrame(again.info, 3, again.read, planes, nullptr));
    CHECK(!planes.y.empty() && planes.y[0] == 3);

    // Same size, different geometry: the header bytes differ.
    writeY4m(path, 8, 16, 4);
    OpenedYuvFile swapped;
    CHECK(openYuvFile(path.string(), swapped));
    CHECK(swapped.info.format.width == 8 && swapped.info.format.height == 16);

    writeY4m(path, 8, 16, 6);
    OpenedYuvFile longer;
    CHECK(openYuvFile(path.string(), longer));
    CHECK(longer.info.frameCount == 6);
}

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "mviewer_yuv_fast_paths";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    testSsimMatchesReference();
    testRawThumbnailMatchesPlanes();
    testThreadedConvertMatchesSerial();
    testUnpackReuse();
    testParallelCompare(dir);
    testY4mCache(dir);
    std::filesystem::remove_all(dir, ec);
    if (g_failures != 0)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("yuv fast path tests passed\n");
    return 0;
}
// NOLINTEND
