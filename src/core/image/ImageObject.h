#pragma once

#include "ImageBuffer.h"

#include <string>
#include <QDateTime>

// 一张图片的内存表示：解码后的 ImageData + 元数据。
// 由 Decoder 产出，被 Cache / Viewer / Analysis 共享。
// 接口层不暴露 Qt 类型（ImageData 使用 std 容器）；QDateTime 仅作为
// 数据型元数据保留（非 UI 类型）。
class ImageObject
{
public:
    ImageObject() = default;
    ImageObject(const std::string &path, const ImageData &image);

    bool isValid() const { return !m_image.isNull(); }

    const std::string &path() const { return m_path; }
    const ImageData &image() const { return m_image; }
    int width() const { return m_image.width; }
    int height() const { return m_image.height; }

    // 元数据（解码时填，供缓存/比较用）
    qint64 fileSize() const { return m_fileSize; }
    QDateTime modified() const { return m_modified; }
    const std::string &hash() const { return m_hash; }

    // 亮度均值 + RGB 均值（首次需要时才算，缓存结果）
    double luminanceMean();
    void rgbMeans(int &r, int &g, int &b);

private:
    void computeStats();

    std::string m_path;
    ImageData m_image;
    qint64 m_fileSize = 0;
    QDateTime m_modified;
    std::string m_hash;

    bool m_statsComputed = false;
    double m_lumMean = 0.0;
    int m_rMean = 0, m_gMean = 0, m_bMean = 0;
};
