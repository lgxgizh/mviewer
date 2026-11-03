// NOLINTBEGIN
#include "core/image/yuv/YuvToRgb.h"
#include "core/simd/CpuFeatures.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace mviewer::core::yuv;

namespace
{

constexpr int kWidth = 1920;
constexpr int kRows = 1080;
constexpr int kPasses = 50;

struct Buffers
{
    std::vector<int16_t> y;
    std::vector<int16_t> u;
    std::vector<int16_t> v;
    std::vector<uint8_t> dst;
    std::vector<uint8_t> ref;
};

Buffers makeBuffers()
{
    Buffers b;
    const size_t n = static_cast<size_t>(kWidth) * static_cast<size_t>(kRows);
    b.y.resize(n);
    b.u.resize(n);
    b.v.resize(n);
    b.dst.resize(n * 3u);
    b.ref.resize(n * 3u);
    std::mt19937 rng(0xB9u);
    std::uniform_int_distribution<int> dist(0, 4095);
    for (size_t i = 0; i < n; ++i)
    {
        b.y[i] = static_cast<int16_t>(dist(rng));
        b.u[i] = static_cast<int16_t>(dist(rng));
        b.v[i] = static_cast<int16_t>(dist(rng));
    }
    return b;
}

void convertAll(RowConverterFn fn, Buffers &b, const YuvCoeffs &c)
{
    for (int row = 0; row < kRows; ++row)
    {
        const size_t off = static_cast<size_t>(row) * static_cast<size_t>(kWidth);
        fn(b.y.data() + off, b.u.data() + off, b.v.data() + off, kWidth, c,
           b.dst.data() + off * 3u);
    }
}

bool matchesScalar(RowConverterFn fn, Buffers &b, const YuvCoeffs &c)
{
    convertAll(fn, b, c);
    for (int row = 0; row < kRows; ++row)
    {
        const size_t off = static_cast<size_t>(row) * static_cast<size_t>(kWidth);
        convertRowScalar(b.y.data() + off, b.u.data() + off, b.v.data() + off, kWidth, c,
                         b.ref.data() + off * 3u);
    }
    return b.dst == b.ref;
}

double megapixelsPerSec(RowConverterFn fn, Buffers &b, const YuvCoeffs &c)
{
    const auto t0 = std::chrono::steady_clock::now();
    for (int pass = 0; pass < kPasses; ++pass)
        convertAll(fn, b, c);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double pixels =
        static_cast<double>(kRows) * static_cast<double>(kWidth) * static_cast<double>(kPasses);
    return pixels / sec / 1.0e6;
}

void benchOne(const char *name, bool available, RowConverterFn fn, Buffers &b, const YuvCoeffs &c,
              int &failures)
{
    if (!available)
    {
        std::printf("note: skip %s (cpu lacks it)\n", name);
        return;
    }
    if (!matchesScalar(fn, b, c))
    {
        std::printf("%s: mismatch vs scalar\n", name);
        ++failures;
        return;
    }
    const double rate = megapixelsPerSec(fn, b, c);
    std::printf("%s: %.2f Mpx/s\n", name, rate);
}

} // namespace

int main()
{
    Buffers b = makeBuffers();
    const YuvCoeffs c = makeYuvCoeffs(Matrix::BT709, Range::Limited, 8);
    int failures = 0;
    benchOne("scalar", true, &convertRowScalar, b, c, failures);
    benchOne("sse2", mviewer::core::CpuFeatures::hasSSE2(), &convertRowSse2, b, c, failures);
    benchOne("avx2", mviewer::core::CpuFeatures::hasAvx2(), &convertRowAvx2, b, c, failures);
    return failures == 0 ? 0 : 1;
}
// NOLINTEND
