// NOLINTBEGIN
// Bit-exact checks for SSE2 / AVX2 YUV->RGB row converters against convertRowScalar.

#include "core/image/yuv/YuvToRgb.h"
#include "core/simd/CpuFeatures.h"

#include <cstdint>
#include <cstdio>
#include <random>
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

static void compareRow(RowConverterFn fn, const int16_t *y, const int16_t *u, const int16_t *v,
                       int n, const YuvCoeffs &c)
{
    const size_t bytes = static_cast<size_t>(n) * 3u + 16u;
    std::vector<uint8_t> got(bytes, 0xA5);
    std::vector<uint8_t> ref(bytes, 0xA5);
    fn(y, u, v, n, c, got.data());
    convertRowScalar(y, u, v, n, c, ref.data());
    CHECK(got == ref);
}

static void fillRand(std::mt19937 &rng, std::vector<int16_t> &y, std::vector<int16_t> &u,
                     std::vector<int16_t> &v, int n)
{
    std::uniform_int_distribution<int> dist(0, 4095);
    y.resize(static_cast<size_t>(n));
    u.resize(static_cast<size_t>(n));
    v.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        y[static_cast<size_t>(i)] = static_cast<int16_t>(dist(rng));
        u[static_cast<size_t>(i)] = static_cast<int16_t>(dist(rng));
        v[static_cast<size_t>(i)] = static_cast<int16_t>(dist(rng));
    }
}

static void checkExtremes(RowConverterFn fn, int n, const YuvCoeffs &c)
{
    std::vector<int16_t> y(static_cast<size_t>(n));
    std::vector<int16_t> u(static_cast<size_t>(n));
    std::vector<int16_t> v(static_cast<size_t>(n));
    for (int bits = 0; bits < 8; ++bits)
    {
        const int16_t yv = (bits & 1) != 0 ? static_cast<int16_t>(4095) : static_cast<int16_t>(0);
        const int16_t uv = (bits & 2) != 0 ? static_cast<int16_t>(4095) : static_cast<int16_t>(0);
        const int16_t vv = (bits & 4) != 0 ? static_cast<int16_t>(4095) : static_cast<int16_t>(0);
        for (int i = 0; i < n; ++i)
        {
            y[static_cast<size_t>(i)] = yv;
            u[static_cast<size_t>(i)] = uv;
            v[static_cast<size_t>(i)] = vv;
        }
        compareRow(fn, y.data(), u.data(), v.data(), n, c);
        compareRow(fn, y.data(), nullptr, v.data(), n, c);
        compareRow(fn, y.data(), u.data(), nullptr, n, c);
    }
}

static void checkUnaligned(RowConverterFn fn, const std::vector<int16_t> &y,
                           const std::vector<int16_t> &u, const std::vector<int16_t> &v,
                           const YuvCoeffs &c)
{
    const int n = static_cast<int>(y.size());
    std::vector<int16_t> yb(static_cast<size_t>(n) + 1u);
    std::vector<int16_t> ub(static_cast<size_t>(n) + 1u);
    std::vector<int16_t> vb(static_cast<size_t>(n) + 1u);
    for (int i = 0; i < n; ++i)
    {
        yb[static_cast<size_t>(i) + 1u] = y[static_cast<size_t>(i)];
        ub[static_cast<size_t>(i) + 1u] = u[static_cast<size_t>(i)];
        vb[static_cast<size_t>(i) + 1u] = v[static_cast<size_t>(i)];
    }
    const size_t bytes = static_cast<size_t>(n) * 3u + 16u;
    std::vector<uint8_t> got(bytes + 1u, 0xA5);
    std::vector<uint8_t> ref(bytes + 1u, 0xA5);
    fn(yb.data() + 1, ub.data() + 1, vb.data() + 1, n, c, got.data() + 1);
    convertRowScalar(yb.data() + 1, ub.data() + 1, vb.data() + 1, n, c, ref.data() + 1);
    CHECK(got == ref);
}

static void checkKind(RowConverterFn fn)
{
    std::mt19937 rng(0x0B9C0FFEu);
    std::vector<int16_t> y;
    std::vector<int16_t> u;
    std::vector<int16_t> v;
    const Matrix matrices[] = {Matrix::BT601, Matrix::BT709, Matrix::BT2020};
    const Range ranges[] = {Range::Limited, Range::Full};
    const int depths[] = {1, 8, 10, 12, 16};
    for (Matrix m : matrices)
        for (Range r : ranges)
            for (int depth : depths)
            {
                const YuvCoeffs c = makeYuvCoeffs(m, r, depth);
                for (int n = 0; n <= 70; ++n)
                {
                    fillRand(rng, y, u, v, n);
                    const int16_t *yp = n == 0 ? nullptr : y.data();
                    const int16_t *up = n == 0 ? nullptr : u.data();
                    const int16_t *vp = n == 0 ? nullptr : v.data();
                    compareRow(fn, yp, up, vp, n, c);
                    compareRow(fn, yp, nullptr, vp, n, c);
                    if (n > 0)
                        checkUnaligned(fn, y, u, v, c);
                    if (n == 0 || n == 1 || n == 7 || n == 8 || n == 15 || n == 16 || n == 70)
                        checkExtremes(fn, n, c);
                }
            }
}

static void checkFrame()
{
    Format f;
    f.width = 37;
    f.height = 5;
    applyPixFmtName("yuv420p", f);
    f.matrix = Matrix::BT709;
    f.range = Range::Limited;
    YuvPlanes p;
    p.width = 37;
    p.height = 5;
    p.bitDepth = f.bitDepth;
    p.chromaWidth = chromaWidth(f);
    p.chromaHeight = chromaHeight(f);
    std::mt19937 rng(0x420u);
    std::uniform_int_distribution<int> dist(0, (1 << f.bitDepth) - 1);
    p.y.resize(static_cast<size_t>(p.width * p.height));
    p.u.resize(static_cast<size_t>(p.chromaWidth * p.chromaHeight));
    p.v.resize(static_cast<size_t>(p.chromaWidth * p.chromaHeight));
    for (uint16_t &s : p.y)
        s = static_cast<uint16_t>(dist(rng));
    for (uint16_t &s : p.u)
        s = static_cast<uint16_t>(dist(rng));
    for (uint16_t &s : p.v)
        s = static_cast<uint16_t>(dist(rng));
    const size_t stride = static_cast<size_t>(p.width) * 3u;
    std::vector<uint8_t> dst(stride * static_cast<size_t>(p.height), 0);
    CHECK(convertToRgb24(p, f, ChromaUpsample::Nearest, dst.data(), dst.size(), stride));
    const YuvCoeffs c = makeYuvCoeffs(f.matrix, f.range, f.bitDepth);
    std::vector<int16_t> y(static_cast<size_t>(p.width));
    std::vector<int16_t> u(static_cast<size_t>(p.width));
    std::vector<int16_t> v(static_cast<size_t>(p.width));
    std::vector<uint8_t> ref(dst.size());
    for (int row = 0; row < p.height; ++row)
    {
        for (int x = 0; x < p.width; ++x)
            y[static_cast<size_t>(x)] =
                toTwelveBit(p.y[static_cast<size_t>(row * p.width + x)], p.bitDepth);
        const int cy = std::min(row >> 1, p.chromaHeight - 1);
        for (int x = 0; x < p.width; ++x)
        {
            const int cx = std::min(x >> 1, p.chromaWidth - 1);
            const size_t ci = static_cast<size_t>(cy * p.chromaWidth + cx);
            u[static_cast<size_t>(x)] = toTwelveBit(p.u[ci], p.bitDepth);
            v[static_cast<size_t>(x)] = toTwelveBit(p.v[ci], p.bitDepth);
        }
        convertRowScalar(y.data(), u.data(), v.data(), p.width, c,
                         ref.data() + static_cast<size_t>(row) * stride);
    }
    CHECK(dst == ref);
}

static void runKind(const char *name, bool available, RowConverterFn fn)
{
    if (!available)
    {
        std::printf("note: skip %s (cpu lacks it)\n", name);
        return;
    }
    checkKind(fn);
}

int main()
{
    runKind("scalar", true, &convertRowScalar);
    runKind("sse2", mviewer::core::CpuFeatures::hasSSE2(), &convertRowSse2);
    runKind("avx2", mviewer::core::CpuFeatures::hasAvx2(), &convertRowAvx2);
    const RowConverterKind best = bestRowConverterKind();
    CHECK(rowConverterFor(best) == selectRowConverter());
    CHECK(rowConverterFor(RowConverterKind::Scalar) == &convertRowScalar);
    checkFrame();
    if (g_failures != 0)
    {
        std::printf("test_yuv_simd: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_yuv_simd: all passed\n");
    return 0;
}
// NOLINTEND
