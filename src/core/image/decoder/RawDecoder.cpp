#include "core/image/decoder/RawDecoder.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/ImageBuffer.h"

#include <QImageReader>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{

std::atomic<size_t> g_lastPreviewFullFileCopyBytes{0};
std::atomic<size_t> g_lastPreviewPeakBufferedBytes{0};

// RAW container extensions we attempt to preview-decode. This list is
// intentionally broad: every entry embeds at least a thumbnail/preview JPEG,
// which is what we extract. Formats without an embedded JPEG simply fall
    QBuffer buf(const_cast<QByteArray *>(&jpeg));
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, "JPEG");
    reader.setAutoTransform(true);
    const QSize fullSize = reader.size();
    if (!fullSize.isValid() || fullSize.isEmpty())
    {
        QImage img = QImage::fromData(jpeg, "JPEG");
        if (img.isNull())
            return ImageData();
        if (maxEdge > 0 && (img.width() > maxEdge || img.height() > maxEdge))
        {
            const double r = static_cast<double>(maxEdge) / std::max(img.width(), img.height());
            img = img.scaled(static_cast<int>(img.width() * r), static_cast<int>(img.height() * r),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return toImageData(img);
    }

    if (maxEdge > 0 && (fullSize.width() > maxEdge || fullSize.height() > maxEdge))
    {
        const double r =
            static_cast<double>(maxEdge) / std::max(fullSize.width(), fullSize.height());
        reader.setScaledSize(QSize(std::max(1, static_cast<int>(fullSize.width() * r)),
                                   std::max(1, static_cast<int>(fullSize.height() * r))));
    }
    const QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    return toImageData(img);
}

ImageData RawDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData RawDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    return extractPreview(path, maxEdge);
}

ImageData RawDecoder::decodeScaled(const std::string &path, int maxEdge,
                                   mviewer::domain::ImageMetadata &outMeta) const
{
    ImageData d = extractPreview(path, maxEdge);
    if (!d.isNull())
    {
        const QFileInfo fi(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
        outMeta.filePath = path;
        outMeta.fileName = fi.fileName().toUtf8().toStdString();
        outMeta.fileSize = static_cast<uint64_t>(qMax<qint64>(0, fi.size()));
        outMeta.width = d.width;
        outMeta.height = d.height;
        outMeta.format = "RAW";
        outMeta.channels = 3;
    }
    return d;
}

ImageData RawDecoder::decodeFull(const std::string &path,
                                 mviewer::domain::ImageMetadata &outMeta) const
{
    ImageData d = extractPreview(path, 0);
    if (!d.isNull())
    {
        outMeta.width = d.width;
        outMeta.height = d.height;
        outMeta.format = "RAW";
        outMeta.channels = 3;
        if (outMeta.filePath.empty())
            outMeta.filePath = path;
    }
    return d;
}

std::vector<std::string> RawDecoder::extensions() const
{
    std::vector<std::string> v;
    for (const char *e : kRawExts)
        v.emplace_back(e);
    return v;
}
