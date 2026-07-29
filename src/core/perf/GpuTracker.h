#pragma once

#include <cstdint>

// Best-effort GPU memory probe (Windows / DXGI).
//
// Reports the total dedicated video memory across all adapters — a hardware
// capacity indicator useful for the benchmark scaling report. This is a
// capacity metric, NOT per-process GPU usage (which DXGI does not expose).
// On non-Windows or when DXGI is unavailable, `available` is false and all
// byte counts are 0.

namespace mviewer::perf
{

struct GpuSnapshot
{
    uint64_t dedicatedVideoBytes = 0;
    uint64_t dedicatedSystemBytes = 0;
    uint64_t sharedBytes = 0;
    bool available = false;
};

GpuSnapshot sampleGpu();

} // namespace mviewer::perf
