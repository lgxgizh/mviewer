#pragma once

#include <QImage>
#include <QString>
#include <QDateTime>
#include <QSize>

// 一张图片的内存表示：解码后的 QImage + 元数据。
// 由 Decoder 产出，被 Cache / Viewer / Analysis 共享。
class ImageObject
{
public:
    ImageObject() = default;
    ImageObject(const QString &path, const QImage &image);

    bool isValid() const { return !m_image.isNull(); }

    const QString &path() const { return m_path; }
    const QImage &image() const { return m_image; }
    QSize size() const { return m_image.size(); }

    // 元数据（解码时填，供缓存/比较用）
    qint64 fileSize() const { return m_fileSize; }
    QDateTime modified() const { return m_modified; }
    QByteArray hash() const { return m_hash; }

    // 亮度均值 + RGB 均值（首次需要时才算，缓存结果）
    double luminanceMean();
    void rgbMeans(int &r, int &g, int &b);

private:
    void computeStats();

    QString m_path;
    QImage m_image;
    qint64 m_fileSize = 0;
    QDateTime m_modified;
    QByteArray m_hash;

    bool m_statsComputed = false;
    double m_lumMean = 0.0;
    int m_rMean = 0, m_gMean = 0, m_bMean = 0;
};
