#pragma once

#include "core/image/ImageBuffer.h"

#include <functional>

namespace mviewer::core
{

struct UsmParams
{
    double amount = 1.5;   // 锐化强度 [0.0, 5.0]，对应 UI 0% ~ 500%
    double radius = 1.5;   // 高斯模糊标准差 sigma [0.1, 20.0] px
    int threshold = 4;     // 边缘反差触发阈值 [0, 255]
    bool lumaOnly = false; // 是否仅作用于亮度通道

    bool isIdentity() const noexcept
    {
        return amount <= 1e-6 || radius <= 1e-6;
    }
};

struct UsmDiffSummary
{
    double psnr = 0.0;
    double ssim = 0.0;
    double meanDelta = 0.0;
    int maxDelta = 0;
    double changedPercent = 0.0;
};

// 对输入图像执行 USM (Unsharp Mask) 锐化，返回新分配的 ImageData（保持原图格式与尺寸）。
// 支持 RGB24, BGR24, RGBA32, BGRA32, Grayscale8。
// 若 cancelled 回调返回 true，立即中止并返回空 ImageData{}。
ImageData applyUsmSharpen(const ImageData &src, const UsmParams &params,
                          const std::function<bool()> &cancelled = {});

// 计算原图与锐化图之间的量化差异（PSNR, SSIM, Δ均值, Δ最大, 像素变动百分比）
UsmDiffSummary computeUsmDiffSummary(const ImageData &orig, const ImageData &sharpened);

} // namespace mviewer::core
