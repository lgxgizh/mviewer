#pragma once

#include "core/image/ImageBuffer.h"
#include <vector>

// 图像统计算法（独立于 QWidget，接口只暴露 ImageData/std 类型）。
// 内部实现可用 Qt 或 SIMD；此处接口不绑定 Qt。
struct ImageStats
{
    double lumMean = 0;   // 亮度均值
    double rMean = 0, gMean = 0, bMean = 0; // RGB 均值
    int histLum[256] = {0}; // 亮度直方图
    int histR[256] = {0}, histG[256] = {0}, histB[256] = {0}; // RGB 直方图
};

class AnalysisEngine
{
public:
    // 计算单张图的统计信息
    static ImageStats computeStats(const ImageData &img);

    // 计算两张图的差异（要求同尺寸，返回灰度差值图 Grayscale8）
    static ImageData differenceMap(const ImageData &a, const ImageData &b);

    // PSNR（峰值信噪比，单位 dB；同尺寸；完美一致返回 100）
    static double psnr(const ImageData &a, const ImageData &b);

    // SSIM（结构相似性；同尺寸；返回 [-1,1]，1 为完全相同；简化版灰度）
    static double ssim(const ImageData &a, const ImageData &b);

    // 伪彩色热力图：把灰度差异映射到蓝-绿-红（返回 RGB24）
    static ImageData heatMap(const ImageData &gray);
};
