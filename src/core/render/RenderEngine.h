#pragma once

#include "core/image/ImageBuffer.h"

// 图像缩放插值算法（独立于 QWidget，接口只暴露 ImageData）。
// 内部实现可用 Qt 或 SIMD/GPU；此处接口不绑定 Qt 类型。
enum class InterpMode { Nearest, Bilinear, Bicubic, Lanczos };

// 目标尺寸与源区域，用 core 自有的 std 结构描述（不依赖 QSize/QRect）。
struct RenderSize {
    int width = 0;
    int height = 0;
};

struct RenderRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool isValid() const { return width > 0 && height > 0; }
};

class RenderEngine
{
public:
    // 高质量缩放
    static ImageData scale(const ImageData &src, const RenderSize &target,
                           InterpMode mode = InterpMode::Bilinear);

    // Difference Overlay：把差异图以指定透明度叠加到原图上
    static ImageData overlayDifference(const ImageData &base,
                                       const ImageData &diff, double alpha = 0.5);

    // 高质量缩放（指定源区域 -> 目标尺寸）
    static ImageData scaleRegion(const ImageData &src, const RenderRect &region,
                                 const RenderSize &target,
                                 InterpMode mode = InterpMode::Bilinear);
};
