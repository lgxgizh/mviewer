#include "benchmark/corpus.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
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
            const uint8_t r = static_cast<uint8_t>(((y * 255) / h + (rng() & 0x1F)) & 0xFF);
            const uint8_t b = static_cast<uint8_t>((((x + y) * 255) / (w + h)) & 0xFF);
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

Corpus makeCorpus(size_t totalImages, int jpegW, int jpegH, const std::string &outDir,
                  const std::string &format)
{
    Corpus c;
    // Corpus lives on disk; default QTemporaryDir uses the system TEMP which on
    // this machine is the starved C: system drive. Allow redirecting to a large
    // data disk via MVIEWER_BENCH_TMP (benchmark-only, no product impact).
    // If an explicit outDir is given (P3 tier generator), emit there directly
    // and keep the files (no auto-remove, no clear) so they persist as a
    // reusable dataset.
    const bool jpegOnly = (format == "jpeg");
    std::unique_ptr<QTemporaryDir> tmp;
    if (!outDir.empty())
    {
        QDir().mkpath(QString::fromStdString(outDir));
        c.dir = outDir;
    }
    else
    {
        const QString envTmp = qEnvironmentVariable("MVIEWER_BENCH_TMP");
        if (!envTmp.isEmpty())
        {
            QDir().mkpath(envTmp);
            tmp = std::make_unique<QTemporaryDir>(envTmp + QDir::separator());
        }
        else
        {
            tmp = std::make_unique<QTemporaryDir>();
        }
        tmp->setAutoRemove(false);
        c.dir = tmp->path().toStdString();
    }

    QDir().mkpath(QString::fromStdString(c.dir));

    for (size_t i = 0; i < totalImages; ++i)
    {
        // JPEG + PNG at the configured dimensions.
        QImage big(jpegW, jpegH, QImage::Format_RGB888);
        paint(big, static_cast<uint32_t>(i + 1));
        // Already RGB888; kept explicit for clarity / future format changes.
        const QImage rgb = big.convertToFormat(QImage::Format_RGB888);

        const QString prefix = QString::fromStdString(c.dir) +
                               QString("/img_%1").arg(static_cast<qlonglong>(i), 5, 10, QChar('0'));
        const QString jp = prefix + ".jpg";
        const QString pp = prefix + ".png";
        const QString bp = prefix + ".bmp";

        if (format == "mixed")
        {
            // Round-robin: JPEG, PNG, TIFF, BMP.
            const int r = static_cast<int>(i % 4);
            if (r == 0)
            {
                if (write(jp, rgb, "jpg"))
                    c.jpegPaths.push_back(jp.toStdString());
            }
            else if (r == 1)
            {
                if (write(pp, rgb, "png"))
                    c.pngPaths.push_back(pp.toStdString());
            }
            else if (r == 2)
            {
                QImage tiff(512, 512, QImage::Format_RGB888);
                paint(tiff, static_cast<uint32_t>(i + 100000));
                const QString tp = prefix + ".tif";
                if (write(tp, tiff, "tif"))
                    c.tiffPaths.push_back(tp.toStdString());
            }
            else
            {
                if (write(bp, rgb, "bmp"))
                    c.jpegPaths.push_back(bp.toStdString());
            }
        }
        else
        {
            if (write(jp, rgb, "jpg"))
                c.jpegPaths.push_back(jp.toStdString());
            if (!jpegOnly && write(pp, rgb, "png"))
                c.pngPaths.push_back(pp.toStdString());

            // TIFF at 512x512 (matches JPEG/PNG dimensions; keeps the TIFF decode
            // path exercised without the 2000x2000 uncompressed ~12MB/file space
            // bomb that starved the data disk at large corpus sizes).
            if (!jpegOnly)
            {
                QImage tiff(512, 512, QImage::Format_RGB888);
                paint(tiff, static_cast<uint32_t>(i + 100000));
                const QImage trgb = tiff.convertToFormat(QImage::Format_RGB888);
                const QString tp =
                    QString::fromStdString(c.dir) +
                    QString("/img_%1.tif").arg(static_cast<qlonglong>(i), 5, 10, QChar('0'));
                if (write(tp, trgb, "tif"))
                    c.tiffPaths.push_back(tp.toStdString());
            }
        }
    }
    return c;
}

Corpus makeCorpusFromDir(const std::string &dir)
{
    Corpus c;
    c.dir = dir;

    const QDir qdir(QString::fromStdString(dir));
    const QStringList jpegExts = QStringList() << "*.jpg" << "*.jpeg";
    const QStringList pngExts = QStringList() << "*.png";
    const QStringList tiffExts = QStringList() << "*.tif" << "*.tiff";

    for (const QString &f : qdir.entryList(jpegExts, QDir::Files))
        c.jpegPaths.push_back(QFileInfo(qdir, f).absoluteFilePath().toStdString());
    for (const QString &f : qdir.entryList(pngExts, QDir::Files))
        c.pngPaths.push_back(QFileInfo(qdir, f).absoluteFilePath().toStdString());
    for (const QString &f : qdir.entryList(tiffExts, QDir::Files))
        c.tiffPaths.push_back(QFileInfo(qdir, f).absoluteFilePath().toStdString());

    return c;
}

} // namespace mviewer::bench
