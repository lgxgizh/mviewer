#include "core/image/decoder/RawDecoder.h"

#include "core/image/ImageBuffer.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

// RAW container extensions we attempt to preview-decode. This list is
// intentionally broad: every entry embeds at least a thumbnail/preview JPEG,
// which is what we extract. Formats without an embedded JPEG simply fall
// through (empty ImageData).
const char *kRawExts[] = {"cr2", "cr3", "nef", "nrw", "arw", "dng", "orf",
                          "rw2", "raf", "pef", "srw", "mrw", "kdc", "dcr",
                          "sr2", "3fr", "fff", "iiq", "mos", "erf", "rwz"};

// Walk a single JPEG starting at FFD8, returning the index just past EOI
// (FFD9), or -1 if the stream is malformed/truncated. Length fields of marker
// segments are honoured so we do not stop early at a stray FFD9 inside data.
long jpegEnd(const std::vector<uint8_t> &b, long start)
{
    const long n = static_cast<long>(b.size());
    if (start < 0 || start + 2 > n)
        return -1;
    if (b[start] != 0xFF || b[start + 1] != 0xD8)
        return -1;
    long i = start + 2;
    while (i + 1 < n)
    {
        if (b[i] != 0xFF)
        {
            ++i;
            continue;
        }
        const uint8_t m = b[i + 1];
        if (m == 0xD9) // EOI
            return i + 2;
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) // standalone markers
        {
            i += 2;
            continue;
        }
        if (m == 0xFF) // fill byte
        {
            ++i;
            continue;
        }
        if (i + 4 > n) // segment with length field
            return -1;
        const int len = (static_cast<int>(b[i + 2]) << 8) | b[i + 3];
        if (len < 2)
            return -1;
        i += 2 + len;
    }
    return -1;
}

// Find the largest embedded JPEG preview in the buffer (cameras often store
// several; we want the highest-resolution one).
QByteArray extractLargestJpeg(const std::vector<uint8_t> &b)
{
    const long n = static_cast<long>(b.size());
    long bestStart = -1;
    long bestEnd = -1;
    long bestLen = 0;
    for (long i = 0; i + 3 < n; ++i)
    {
        if (b[i] == 0xFF && b[i + 1] == 0xD8 && b[i + 2] == 0xFF)
        {
            const long end = jpegEnd(b, i);
            if (end > 0)
            {
                const long len = end - i;
                if (len > bestLen)
                {
                    bestLen = len;
                    bestStart = i;
                    bestEnd = end;
                }
                i = end - 1; // resume scanning after this JPEG
            }
        }
    }
    if (bestStart < 0)
        return QByteArray();
    return QByteArray(reinterpret_cast<const char *>(b.data() + bestStart), bestEnd - bestStart);
}

ImageData toImageData(const QImage &src)
{
    if (src.isNull())
        return ImageData();
    const QImage img = src.convertToFormat(QImage::Format_RGB888);
    if (img.isNull())
        return ImageData();
    ImageData out = makeImageData(img.width(), img.height(), PixelFormat::RGB24);
    const int w = img.width();
    const int h = img.height();
    const size_t rowBytes = static_cast<size_t>(w) * 3;
    for (int y = 0; y < h; ++y)
    {
        const uchar *s = img.constScanLine(y);
        uint8_t *d = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        std::memcpy(d, s, rowBytes);
    }
    return out;
}

} // namespace

bool RawDecoder::canDecode(const std::string &path) const
{
    const QString ext = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    for (const char *e : kRawExts)
    {
        if (ext == QString::fromLatin1(e))
            return true;
    }
    return false;
}

ImageData RawDecoder::extractPreview(const std::string &path, int maxEdge) const
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return ImageData();
    const QByteArray raw = f.readAll();
    f.close();
    if (raw.isEmpty())
        return ImageData();

    std::vector<uint8_t> buf(reinterpret_cast<const uint8_t *>(raw.constData()),
                             reinterpret_cast<const uint8_t *>(raw.constData()) + raw.size());
    const QByteArray jpeg = extractLargestJpeg(buf);
    if (jpeg.isEmpty())
        return ImageData();

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

ImageData RawDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData RawDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    return extractPreview(path, maxEdge);
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
