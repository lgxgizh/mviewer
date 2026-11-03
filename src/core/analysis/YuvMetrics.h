#pragma once

#include "core/image/yuv/YuvFormat.h"
#include "core/image/yuv/YuvUnpack.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Per-plane PSNR / SSIM on unpacked YUV code values. Qt-free.

namespace mviewer::core::yuv
{

struct PlaneMetrics
{
    double mse = 0.0;
    double psnr = 0.0;
    double ssim = 0.0;
};

struct YuvFrameMetrics
{
    bool valid = false;
    std::string why;
    int planeCount = 0;
    int bitDepth = 8;
    std::array<PlaneMetrics, 3> planes{};
    double psnr611 = 0.0;
    double psnr111 = 0.0;
    double ssim611 = 0.0;
    double ssim111 = 0.0;
};

// Same width, height, subsampling and bit depth. Layout, packing, endian,
// matrix and range may differ. On mismatch writes an English reason to *why.
bool yuvComparable(const Format &a, const Format &b, std::string *why = nullptr);

// Peak is 2^bitDepth - 1. MSE accumulates squared deltas in uint64_t per row.
// SSIM uses 8x8 windows, step 4, uniform weights (Wang et al. 2004, K1=0.01, K2=0.03).
PlaneMetrics comparePlane(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int width,
                          int height, int bitDepth);

// Invalid when luma/chroma size, bit depth or chroma presence differ, or a plane
// is shorter than width * height.
YuvFrameMetrics compareYuvFrames(const YuvPlanes &a, const YuvPlanes &b);

struct YuvSequenceMetrics
{
    bool valid = false;
    std::string why;
    int64_t framesCompared = 0;
    int64_t framesA = 0;
    int64_t framesB = 0;
    std::vector<YuvFrameMetrics> frames;
    YuvFrameMetrics mean;
};

// Return false to cancel the comparison.
using YuvProgressFn = std::function<bool(int64_t done, int64_t total)>;

// Compares frames [0, min(framesA, framesB, maxFrames)). maxFrames <= 0 means all.
// Open/resolve failures are prefixed "A: " / "B: ". Cancel sets why to "cancelled".
YuvSequenceMetrics compareYuvFiles(const std::string &pathA, const std::string &pathB,
                                   int64_t maxFrames = 0, const YuvProgressFn &progress = {});

// Two decimals for dB, four for SSIM. Infinite PSNR prints "inf". Gray prints Y only.
std::string formatYuvFrameMetrics(const YuvFrameMetrics &m);

} // namespace mviewer::core::yuv
