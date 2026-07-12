#pragma once

#include "ImageObject.h"
#include <QImage>
#include <QString>
#include <QStringList>
#include <vector>
#include <cmath>

// 图像统计算法(完全独立于 QWidget)
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
    static ImageStats computeStats(const QImage &img);

    // 计算两张图的差异(要求同尺寸,返回灰度差值图)
    static QImage differenceMap(const QImage &a, const QImage &b);

    // PSNR (峰值信噪比,单位 dB; 同尺寸; 完美一致返回 +inf)
    static double psnr(const QImage &a, const QImage &b);

    // SSIM (结构相似性; 同尺寸; 返回 [-1,1],1 为完全相同; 简化版灰度)
    static double ssim(const QImage &a, const QImage &b);

    // 伪彩色热力图:把灰度差异映射到蓝-绿-红
    static QImage heatMap(const QImage &gray);
};
