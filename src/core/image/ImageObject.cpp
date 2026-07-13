#include "core/image/ImageObject.h"

#include <QCryptographicHash>
#include <QFileInfo>

#include <algorithm>
#include <cstring>

ImageObject::ImageObject(const std::string &path, const ImageData &image)
    : m_path(path)
    , m_image(image)
{
    const QFileInfo fi(QString::fromStdString(path));
    m_fileSize = fi.size();
    m_modified = fi.lastModified();
    const QString key = QString::fromStdString(path) +
                        QString::number(m_fileSize) +
                        QString::number(fi.lastModified().toSecsSinceEpoch());
    const QByteArray raw = QCryptographicHash::hash(
        key.toUtf8(), QCryptographicHash::Sha1);
    m_hash.assign(raw.constData(), static_cast<size_t>(raw.size()));
}

void ImageObject::computeStats()
{
    if (m_statsComputed || m_image.isNull())
        return;
    const ImageBuffer v = m_image.view();
    const int w = v.width;
    const int h = v.height;
    const int cpp = v.channelsPerPixel();
    const long long n = static_cast<long long>(w) * h;
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t *line = v.data + y * v.stride();
        for (int x = 0; x < w; ++x) {
            const uint8_t *p = line + x * cpp;
            const int r = p[0], g = p[1], b = p[2];
            sumR += r; sumG += g; sumB += b;
            sumL += static_cast<long long>(0.299 * r + 0.587 * g + 0.114 * b);
        }
    }
    m_lumMean = static_cast<double>(sumL) / n;
    m_rMean = static_cast<int>(sumR / n);
    m_gMean = static_cast<int>(sumG / n);
    m_bMean = static_cast<int>(sumB / n);
    m_statsComputed = true;
}

double ImageObject::luminanceMean()
{
    computeStats();
    return m_lumMean;
}

void ImageObject::rgbMeans(int &r, int &g, int &b)
{
    computeStats();
    r = m_rMean; g = m_gMean; b = m_bMean;
}

void ImageObject::computeHistogram()
{
    if (m_histogramComputed || m_image.isNull()) return;
    const ImageBuffer v = m_image.view();
    const int w = v.width, h = v.height, cpp = v.channelsPerPixel();
    std::memset(m_histogram, 0, sizeof(m_histogram));
    for (int y = 0; y < h; ++y) {
        const uint8_t *line = v.data + y * v.stride();
        for (int x = 0; x < w; ++x) {
            const uint8_t *p = line + x * cpp;
            ++m_histogram[std::clamp(luminance(p[0], p[1], p[2]), 0, 255)];
        }
    }
    m_histogramComputed = true;
}
