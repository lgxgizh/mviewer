#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Selection.h"
#include <vector>

// 图像统计算法（独立于 QWidget，接口只暴露 ImageData/std 类型）。
// 内部实现可用 Qt 或 SIMD；此处接口不绑定 Qt。
struct ImageStats {
  double lumMean = 0;                                       // 亮度均值
  double rMean = 0, gMean = 0, bMean = 0;                   // RGB 均值
  int histLum[256] = {0};                                   // 亮度直方图
  int histR[256] = {0}, histG[256] = {0}, histB[256] = {0}; // RGB 直方图
  int pixelCount = 0; // 参与统计的像素数（ROI 时可能小于全图）
};

class AnalysisEngine {
public:
  // 计算单张图的统计信息
  static ImageStats computeStats(const ImageData &img);

  // 计算 ROI 区域的统计信息（region 为图像坐标系，超出边界自动裁剪）
  static ImageStats computeStatsROI(const ImageData &img,
                                    const mviewer::domain::Selection &region);

  // 计算两张图的差异（要求同尺寸，返回灰度差值图 Grayscale8）
  static ImageData differenceMap(const ImageData &a, const ImageData &b);

  // PSNR（峰值信噪比，单位 dB；同尺寸；完美一致返回 100）
  static double psnr(const ImageData &a, const ImageData &b);

  // SSIM（结构相似性；同尺寸；返回 [-1,1]，1 为完全相同；简化版灰度）
  static double ssim(const ImageData &a, const ImageData &b);

  // 噪声估计：拉普拉斯方差法（返回值越大表示噪声越强；典型范围 0..500+）
  // 基于灰度图拉普拉斯算子的方差，对高斯白噪声有较好估计
  static double noiseEstimate(const ImageData &img);

  // 伪彩色热力图：把灰度差异映射到蓝-绿-红（返回 RGB24）
  static ImageData heatMap(const ImageData &gray);
};
