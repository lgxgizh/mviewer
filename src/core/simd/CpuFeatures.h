#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#include <immintrin.h>
#endif

namespace mviewer::core
{

class CpuFeatures
{
  public:
    static bool hasAvx2()
    {
        static const bool supported = []() -> bool
        {
#if defined(_MSC_VER)
            int info[4] = {0};
            __cpuid(info, 1);
            const bool osxsave = (info[2] & (1 << 27)) != 0;
            const bool avx = (info[2] & (1 << 28)) != 0;
            if (!osxsave || !avx)
                return false;

            // Ensure OS supports saving YMM registers
            const unsigned __int64 xcr0 = _xgetbv(0);
            if ((xcr0 & 0x6) != 0x6)
                return false;

            __cpuidex(info, 7, 0);
            const bool avx2 = (info[1] & (1 << 5)) != 0;
            return avx2;
#elif defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
            {
                const bool osxsave = (ecx & (1 << 27)) != 0;
                const bool avx = (ecx & (1 << 28)) != 0;
                if (!osxsave || !avx)
                    return false;
                unsigned long long xcr0 = 0;
                __asm__("xgetbv" : "=A"(xcr0) : "c"(0));
                if ((xcr0 & 0x6) != 0x6)
                    return false;
                if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
                    return (ebx & (1 << 5)) != 0;
            }
            return false;
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
        }();
        return supported;
    }
};

} // namespace mviewer::core
