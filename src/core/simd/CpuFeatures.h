#pragma once

#include <cstdint>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#define MV_ARCH_X86 1
#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#include <immintrin.h>
#endif
#endif

#if defined(MV_ARCH_X86) && (defined(__GNUC__) || defined(__clang__))
#define MV_TARGET_AVX2 __attribute__((target("avx2")))
#define MV_TARGET_SSSE3 __attribute__((target("ssse3")))
#define MV_TARGET_SSE41 __attribute__((target("sse4.1")))
#define MV_TARGET_SSSE3_SSE41 __attribute__((target("ssse3,sse4.1")))
#define MV_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define MV_TARGET_AVX2
#define MV_TARGET_SSSE3
#define MV_TARGET_SSE41
#define MV_TARGET_SSSE3_SSE41
#define MV_TARGET_SSE2
#endif

namespace mviewer::core
{

class CpuFeatures
{
  public:
    // XMM-only features (SSE2 / SSSE3 / SSE4.1) do not consult XCR0. AVX, FMA,
    // F16C and AVX-512 need the OS to save YMM (and, for AVX-512, opmask/ZMM)
    // state; those detectors must go through osSupportsYmm() first. This tree
    // has no separate hasFma / hasAvx / hasAvx512 / hasF16C entry points.
    static bool hasAvx2()
    {
        static const bool supported = []() -> bool
        {
#if defined(MV_ARCH_X86)
            if (!osSupportsYmm())
                return false;
#if defined(_MSC_VER)
            int info[4] = {0};
            __cpuidex(info, 7, 0);
            return (info[1] & (1 << 5)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
                return (ebx & (1 << 5)) != 0;
            return false;
#else
            return false;
#endif
#else
            return false;
#endif
        }();
        return supported;
    }

    static bool hasSSE2()
    {
        static const bool supported = []() -> bool
        {
#if defined(MV_ARCH_X86)
#if defined(_M_X64) || defined(__x86_64__)
            return true; // SSE2 is part of the x86-64 ABI
#elif defined(_MSC_VER)
            int info[4] = {0};
            __cpuid(info, 1);
            return (info[3] & (1 << 26)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
                return (edx & (1 << 26)) != 0;
            return false;
#else
            return false;
#endif
#else
            return false;
#endif
        }();
        return supported;
    }

    static bool hasSse41()
    {
        static const bool supported = []() -> bool
        {
#if defined(MV_ARCH_X86)
#if defined(_MSC_VER)
            int info[4] = {0};
            __cpuid(info, 1);
            return (info[2] & (1 << 19)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
                return (ecx & (1 << 19)) != 0;
            return false;
#else
            return false;
#endif
#else
            return false;
#endif
        }();
        return supported;
    }

    static bool hasSsse3()
    {
        static const bool supported = []() -> bool
        {
#if defined(MV_ARCH_X86)
#if defined(_MSC_VER)
            int info[4] = {0};
            __cpuid(info, 1);
            return (info[2] & (1 << 9)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
                return (ecx & (1 << 9)) != 0;
            return false;
#else
            return false;
#endif
#else
            return false;
#endif
        }();
        return supported;
    }

  private:
    // OSXSAVE + CPU AVX + XCR0[1:2] (XMM and YMM state enabled by the OS).
    // Cached. The x86-64 "=A" constraint is not edx:eax, so XCR0 is read from
    // eax and edx explicitly.
    static bool osSupportsYmm()
    {
        static const bool supported = []() -> bool
        {
#if defined(MV_ARCH_X86)
#if defined(_MSC_VER)
            int info[4] = {0};
            __cpuid(info, 1);
            const bool osxsave = (info[2] & (1 << 27)) != 0;
            const bool avx = (info[2] & (1 << 28)) != 0;
            if (!osxsave || !avx)
                return false;
            const unsigned __int64 xcr0 = _xgetbv(0);
            return (xcr0 & 0x6) == 0x6;
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
                return false;
            const bool osxsave = (ecx & (1 << 27)) != 0;
            const bool avx = (ecx & (1 << 28)) != 0;
            if (!osxsave || !avx)
                return false;
            unsigned int xcrEax = 0;
            unsigned int xcrEdx = 0;
            __asm__ volatile("xgetbv" : "=a"(xcrEax), "=d"(xcrEdx) : "c"(0));
            const uint64_t xcr0 = (static_cast<uint64_t>(xcrEdx) << 32) | xcrEax;
            return (xcr0 & 0x6) == 0x6;
#else
            return false;
#endif
#else
            return false;
#endif
        }();
        return supported;
    }
};

} // namespace mviewer::core
