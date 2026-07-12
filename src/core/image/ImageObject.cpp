#include "core/image/ImageObject.h"

#include <QCryptographicHash>
#include <QFileInfo>

ImageObject::ImageObject(const QString &path, const QImage &image)
    : m_path(path)
    , m_image(image)
{
    const QFileInfo fi(path);
    m_fileSize = fi.size();
    m_modified = fi.lastModified();
    m_hash = QCryptographicHash::hash(
        (path + QString::number(m_fileSize) +
         QString::number(fi.lastModified().toSecsSinceEpoch()))
            .toUtf8(),
        QCryptographicHash::Sha1);
}

void ImageObject::computeStats()
{
    if (m_statsComputed || m_image.isNull())
        return;
    const int w = m_image.width();
    const int h = m_image.height();
    const long long n = static_cast<long long>(w) * h;
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0;
    for (int y = 0; y < h; ++y) {
        const QRgb *line =
            reinterpret_cast<const QRgb *>(m_image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
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
