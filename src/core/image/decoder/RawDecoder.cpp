#include "core/image/decoder/RawDecoder.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/ImageBuffer.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>

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
// through (empty ImageData).
const char *kRawExts[] = {"cr2", "cr3", "nef", "nrw", "arw", "dng", "orf",
                          "rw2", "raf", "pef", "srw", "mrw", "kdc", "dcr",
                          "sr2", "3fr", "fff", "iiq", "mos", "erf", "rwz"};

constexpr qsizetype kScannerChunkBytes = 64 * 1024;

class ChunkReader
{
  public:
    explicit ChunkReader(QFile &file) : m_file(file), m_buffer(kScannerChunkBytes, Qt::Uninitialized)
    {
    }

    bool seek(qint64 offset)
    {
        if (!m_file.seek(offset))
            return false;
        m_offset = 0;
        m_size = 0;
        m_position = offset;
        return true;
    }

    bool readByte(uint8_t &out)
    {
        if (m_offset >= m_size)
        {
            const qint64 read = m_file.read(m_buffer.data(), m_buffer.size());
            if (read <= 0)
                return false;
            m_offset = 0;
            m_size = static_cast<qsizetype>(read);
        }
        out = static_cast<uint8_t>(m_buffer[m_offset++]);
        ++m_position;
        return true;
    }

    qint64 position() const
    {
        return m_position;
    }

  private:
    QFile &m_file;
    QByteArray m_buffer;
    qsizetype m_offset = 0;
    qsizetype m_size = 0;
    qint64 m_position = 0;
};

bool readMarker(ChunkReader &reader, uint8_t &marker)
{
    uint8_t byte = 0;
    while (reader.readByte(byte))
    {
        if (byte != 0xff)
            continue;
        do
        {
            if (!reader.readByte(marker))
                return false;
        } while (marker == 0xff);
        return marker != 0;
    }
    return false;
}

bool skipBytes(ChunkReader &reader, qint64 count)
{
    uint8_t ignored = 0;
    while (count-- > 0)
        if (!reader.readByte(ignored))
            return false;
    return true;
}

bool readSegmentLength(ChunkReader &reader, int &length)
{
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!reader.readByte(hi) || !reader.readByte(lo))
        return false;
    length = (static_cast<int>(hi) << 8) | lo;
    return length >= 2;
}

// Seek through one JPEG without retaining its compressed bytes. The scan-data
// state handles FF00 stuffing and restart markers, so an EOI-like byte sequence
// in entropy data cannot terminate a candidate prematurely.
qint64 jpegEndAt(QFile &file, qint64 start)
{
    ChunkReader reader(file);
    if (!reader.seek(start))
        return -1;

    uint8_t soi0 = 0;
    uint8_t soi1 = 0;
    if (!reader.readByte(soi0) || !reader.readByte(soi1) || soi0 != 0xff || soi1 != 0xd8)
        return -1;

    bool scanData = false;
    for (;;)
    {
        uint8_t marker = 0;
        if (scanData)
        {
            uint8_t byte = 0;
            do
            {
                if (!reader.readByte(byte))
                    return -1;
            } while (byte != 0xff);
            do
            {
                if (!reader.readByte(marker))
                    return -1;
            } while (marker == 0xff);
            if (marker == 0)
                continue; // stuffed FF byte in entropy-coded data
            if (marker == 0xd9)
                return reader.position();
            if (marker == 0xd0 || (marker >= 0xd1 && marker <= 0xd7))
                continue; // restart marker
        }
        else if (!readMarker(reader, marker))
        {
            return -1;
        }

        if (marker == 0xd9)
            return reader.position();
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;

        int length = 0;
        if (!readSegmentLength(reader, length) || !skipBytes(reader, length - 2))
            return -1;
        if (marker == 0xda)
            scanData = true;
    }
}

void updatePeak(size_t bytes)
{
    auto &peak = g_lastPreviewPeakBufferedBytes;
    size_t observed = peak.load(std::memory_order_relaxed);
    while (observed < bytes &&
           !peak.compare_exchange_weak(observed, bytes, std::memory_order_relaxed))
    {
    }
}

QByteArray readRange(QFile &file, qint64 start, qint64 length)
{
    if (start < 0 || length <= 0 || length > std::numeric_limits<qsizetype>::max() ||
        !file.seek(start))
        return QByteArray();
    return file.read(length);
}

// Find the largest structurally valid and decodable JPEG by seeking over the
// container. The scanner and candidate reader have independent handles so a
// seek used to inspect a candidate never invalidates the sequential scan.
QByteArray extractLargestJpeg(QFile &scanFile, QFile &dataFile)
{
    ChunkReader scanner(scanFile);
    if (!scanner.seek(0))
        return QByteArray();
    updatePeak(static_cast<size_t>(2 * kScannerChunkBytes));

    QByteArray best;
    qint64 bestLength = 0;
    bool previousWasFf = false;
    uint8_t byte = 0;
    while (scanner.readByte(byte))
    {
        const qint64 position = scanner.position() - 1;
        if (previousWasFf && byte == 0xd8)
        {
            const qint64 start = position - 1;
            const qint64 end = jpegEndAt(dataFile, start);
            const qint64 length = end - start;
            if (end > start && length > bestLength)
            {
                QByteArray candidate = readRange(dataFile, start, length);
                updatePeak(static_cast<size_t>(2 * kScannerChunkBytes) + best.size() +
                           static_cast<size_t>(candidate.size()));
                if (candidate.size() == length && !QImage::fromData(candidate, "JPEG").isNull())
                {
                    best = std::move(candidate);
                    bestLength = length;
                }
            }
        }
        previousWasFf = byte == 0xff;
    }
    return best;
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
    const QString ext = QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size())))
                            .suffix()
                            .toLower();
    for (const char *e : kRawExts)
    {
        if (ext == QString::fromLatin1(e))
            return true;
    }
    return false;
}

size_t RawDecoder::lastPreviewFullFileCopyBytes()
{
    return g_lastPreviewFullFileCopyBytes.load(std::memory_order_relaxed);
}

size_t RawDecoder::lastPreviewPeakBufferedBytes()
{
    return g_lastPreviewPeakBufferedBytes.load(std::memory_order_relaxed);
}

ImageData RawDecoder::extractPreview(const std::string &path, int maxEdge) const
{
    g_lastPreviewFullFileCopyBytes.store(0, std::memory_order_relaxed);
    g_lastPreviewPeakBufferedBytes.store(0, std::memory_order_relaxed);
    const QString nativePath = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    QFile scanFile(nativePath);
    QFile dataFile(nativePath);
    if (!scanFile.open(QIODevice::ReadOnly) || !dataFile.open(QIODevice::ReadOnly))
        return ImageData();
    const QByteArray jpeg = extractLargestJpeg(scanFile, dataFile);
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
