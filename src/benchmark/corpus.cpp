#include "benchmark/corpus.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

namespace mviewer::bench
{

namespace
{
// Deterministic pseudo-random gradient + noise so decoders do real work.
void paint(QImage &img, uint32_t seed)
{
    std::mt19937 rng(seed);
    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y)
    {
        uint8_t *line = img.scanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const int idx = x * 3; // line already points at row y
            const uint8_t g = static_cast<uint8_t>((x * 255) / w);
            const uint8_t r =
                static_cast<uint8_t>(((y * 255) / h + (rng() & 0x1F)) & 0xFF);
            const uint8_t b =
                static_cast<uint8_t>((((x + y) * 255) / (w + h)) & 0xFF);
            line[idx + 0] = r;
            line[idx + 1] = g;
            line[idx + 2] = b;
        }
    }
}

bool write(const QString &path, const QImage &img, const char *fmt)
{
    QImageWriter w(path, fmt);
    w.setQuality(90);
    return w.write(img);
}
} // namespace

std::vector<std::string> Corpus::allPaths() const
{
    std::vector<std::string> v;
    v.reserve(jpegPaths.size() + pngPaths.size() + tiffPaths.size());
    v.insert(v.end(), jpegPaths.begin(), jpegPaths.end());
    v.insert(v.end(), pngPaths.begin(), pngPaths.end());
    v.insert(v.end(), tiffPaths.begin(), tiffPaths.end());
    return v;
}

void Corpus::clear() const
{
    QDir d(QString::fromStdString(dir));
    d.removeRecursively();
}

Corpus makeCorpus(size_t totalImages, int jpegW, int jpegH)
{
    Corpus c;
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    c.dir = tmp.path().toStdString();

    QDir().mkpath(QString::fromStdString(c.dir));

    for (size_t i = 0; i < totalImages; ++i)
    {
        // JPEG + PNG at the configured dimensions.
        QImage big(jpegW, jpegH, QImage::Format_RGB888);
        paint(big, static_cast<uint32_t>(i + 1));
        // Already RGB888; kept explicit for clarity / future format changes.
        const QImage rgb = big.convertToFormat(QImage::Format_RGB888);

        const QString jp = QString::fromStdString(c.dir) + QString("/img_%1.jpg").arg(
            static_cast<qlonglong>(i), 5, 10, QChar('0'));
        const QString pp = QString::fromStdString(c.dir) + QString("/img_%1.png").arg(
            static_cast<qlonglong>(i), 5, 10, QChar('0'));
        if (write(jp, rgb, "jpg"))
            c.jpegPaths.push_back(jp.toStdString());
        if (write(pp, rgb, "png"))
            c.pngPaths.push_back(pp.toStdString());

        // TIFF at 4 MP (codec gated / heavier).
        QImage tiff(2000, 2000, QImage::Format_RGB888);
        paint(tiff, static_cast<uint32_t>(i + 100000));
        const QImage trgb = tiff.convertToFormat(QImage::Format_RGB888);
        const QString tp = QString::fromStdString(c.dir) + QString("/img_%1.tif").arg(
            static_cast<qlonglong>(i), 5, 10, QChar('0'));
        if (write(tp, trgb, "tif"))
            c.tiffPaths.push_back(tp.toStdString());
    }
    return c;
}

} // namespace mviewer::bench
