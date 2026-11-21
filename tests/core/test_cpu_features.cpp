// CpuFeatures probes are cached and must be safe to call repeatedly. AVX2
// requires the OS YMM save (OSXSAVE + XCR0); this does not toggle XCR0, but a
// second call must match and the xgetbv read must not fault.
#include "core/simd/CpuFeatures.h"

#include <cstdio>

int main()
{
    using mviewer::core::CpuFeatures;
    const bool avx2 = CpuFeatures::hasAvx2();
    const bool sse2 = CpuFeatures::hasSSE2();
    const bool sse41 = CpuFeatures::hasSse41();
    const bool ssse3 = CpuFeatures::hasSsse3();
    int failed = 0;
    auto check = [&failed](bool cond, const char *msg)
    {
        if (cond)
        {
            std::printf("  PASS: %s\n", msg);
            return;
        }
        std::printf("  FAIL: %s\n", msg);
        ++failed;
    };
    check(avx2 == CpuFeatures::hasAvx2(), "hasAvx2 is stable");
    check(sse2 == CpuFeatures::hasSSE2(), "hasSSE2 is stable");
    check(sse41 == CpuFeatures::hasSse41(), "hasSse41 is stable");
    check(ssse3 == CpuFeatures::hasSsse3(), "hasSsse3 is stable");
#if defined(__x86_64__) || defined(_M_X64)
    check(sse2, "x86-64 provides SSE2");
#endif
    if (avx2)
    {
        check(sse41, "AVX2 implies SSE4.1");
        check(ssse3, "AVX2 implies SSSE3");
    }
    std::printf("=== CpuFeatures: %d failed ===\n", failed);
    return failed == 0 ? 0 : 1;
}
