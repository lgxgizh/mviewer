#include "core/image/decoder/RawDecoder.h"
#include "core/async/CooperativeCancel.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/ImageBuffer.h"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace
{

std::atomic<size_t> g_lastPreviewFullFileCopyBytes{0};
std::atomic<size_t> g_lastPreviewPeakBufferedBytes{0};

// RAW container extensions we attempt to preview-decode. This list is
// intentionally broad: every entry embeds at least a thumbnail/preview JPEG,
// which is what we extract. Formats without an embedded JPEG simply fall

// RAW container extensions we attempt to preview-decode. This list is
// intentionally broad: every entry embeds at least a thumbnail/preview JPEG,
// which is what we extract. Formats without an embedded JPEG simply fall
// through (empty ImageData).
const char *kRawExts[] = {"cr2", "cr3", "nef", "nrw", "arw", "dng", "orf",
                          "rw2", "raf", "pef", "srw", "mrw", "kdc", "dcr",
                          "sr2", "3fr", "fff", "iiq", "mos", "erf", "rwz"};

constexpr qsizetype kScannerChunkBytes = 64 * 1024;
constexpr qint64 kMaxPreviewJpegBytes = 64LL * 1024 * 1024;

class ChunkReader
{
  public:
    explicit ChunkReader(QFile &file)
        : m_file(file), m_buffer(kScannerChunkBytes, Qt::Uninitialized)
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

    bool skip(qint64 count)
    {
        if (count < 0)
            return false;
        const qint64 available = m_size - m_offset;
        if (count <= available)
        {
            m_offset += static_cast<qsizetype>(count);
            m_position += count;
            return true;
        }
        const qint64 remaining = count - available;
        m_offset = 0;
        m_size = 0;
        m_position += available;
        if (!m_file.seek(m_position + remaining))
            return false;
        m_position += remaining;
        return true;
    }

    // Fast-forward to the next 0xFF byte using hardware-accelerated memchr.
    // Leaves reader positioned directly at the 0xFF byte.
    bool skipToNextFF()
    {
        for (;;)
        {
            if (m_offset >= m_size)
            {
                const qint64 read = m_file.read(m_buffer.data(), m_buffer.size());
                if (read <= 0)
                    return false;
                m_offset = 0;
                m_size = static_cast<qsizetype>(read);
            }
            const void *ptr = std::memchr(m_buffer.constData() + m_offset, 0xff,
                                          static_cast<size_t>(m_size - m_offset));
            if (ptr)
            {
                const auto dist = static_cast<qsizetype>(static_cast<const char *>(ptr) -
                                                         (m_buffer.constData() + m_offset));
                m_offset += dist;
                m_position += dist;
                return true;
            }
            const qsizetype jumped = m_size - m_offset;
            m_position += jumped;
            m_offset = m_size;
        }
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
    while (reader.skipToNextFF())
    {
        uint8_t byte = 0;
        if (!reader.readByte(byte))
            return false;
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
    return reader.skip(count);
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

// One embedded JPEG located by marker walking. Dimensions come from SOF; the
// compressed payload is not copied until a caller actually decodes the span.
struct JpegSpan
{
    qint64 start = 0;
    qint64 length = 0;
    int width = 0;
    int height = 0;
};

constexpr int kMaxRetainedSpans = 32;

bool isSofMarker(uint8_t marker)
{
    // SOF0–SOF15 except DHT (C4), the reserved JPG marker (C8) and DAC (CC).
    return marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
}

// `payload` is the segment size after the 2-byte length field. SOF layout:
// precision(1) height(2) width(2) ...
bool readSofDimensions(ChunkReader &reader, int payload, int &width, int &height)
{
    if (payload < 5)
        return false;
    uint8_t precision = 0;
    uint8_t hh = 0;
    uint8_t hl = 0;
    uint8_t wh = 0;
    uint8_t wl = 0;
    if (!reader.readByte(precision) || !reader.readByte(hh) || !reader.readByte(hl) ||
        !reader.readByte(wh) || !reader.readByte(wl))
        return false;
    (void)precision;
    width = (static_cast<int>(wh) << 8) | static_cast<int>(wl);
    height = (static_cast<int>(hh) << 8) | static_cast<int>(hl);
    if (payload > 5 && !skipBytes(reader, static_cast<qint64>(payload - 5)))
        return false;
    return width > 0 && height > 0;
}

bool consumeSegment(ChunkReader &reader, uint8_t marker, int &sofW, int &sofH)
{
    int length = 0;
    if (!readSegmentLength(reader, length))
        return false;
    const int payload = length - 2;
    if (!isSofMarker(marker))
        return skipBytes(reader, payload);
    int width = 0;
    int height = 0;
    if (!readSofDimensions(reader, payload, width, height))
        return false;
    if (sofW <= 0)
    {
        sofW = width;
        sofH = height;
    }
    return true;
}

// Marker 0 means a stuffed FF00 byte inside entropy-coded data.
bool nextJpegMarker(ChunkReader &reader, bool scanData, uint8_t &marker)
{
    if (!scanData)
        return readMarker(reader, marker);
    if (!reader.skipToNextFF())
        return false;
    uint8_t byte = 0;
    if (!reader.readByte(byte))
        return false;
    do
    {
        if (!reader.readByte(marker))
            return false;
    } while (marker == 0xff);
    return true;
}

// Walk one JPEG without retaining its compressed bytes. FF00 stuffing and
// restart markers stay in the scan-data state so an EOI-like sequence in
// entropy data cannot end the candidate early.
bool measureJpeg(QFile &file, qint64 start, JpegSpan &out)
{
    ChunkReader reader(file);
    if (!reader.seek(start))
        return false;

    uint8_t soi0 = 0;
    uint8_t soi1 = 0;
    if (!reader.readByte(soi0) || !reader.readByte(soi1) || soi0 != 0xff || soi1 != 0xd8)
        return false;

    int sofW = 0;
    int sofH = 0;
    bool scanData = false;
    for (;;)
    {
        uint8_t marker = 0;
        if (!nextJpegMarker(reader, scanData, marker))
            return false;
        if (marker == 0)
            continue;
        if (marker == 0xd9)
            break;
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (!consumeSegment(reader, marker, sofW, sofH))
            return false;
        if (marker == 0xda)
            scanData = true;
    }

    if (sofW <= 0 || sofH <= 0)
        return false;
    const qint64 end = reader.position();
    if (end <= start)
        return false;
    out.start = start;
    out.length = end - start;
    out.width = sofW;
    out.height = sofH;
    return out.length > 0 && out.length <= kMaxPreviewJpegBytes;
}

void retainSpan(std::vector<JpegSpan> &spans, const JpegSpan &span)
{
    if (span.length <= 0 || span.width <= 0 || span.height <= 0 ||
        span.length > kMaxPreviewJpegBytes)
        return;
    const auto longer = [](const JpegSpan &a, const JpegSpan &b) { return a.length > b.length; };
    const auto it = std::lower_bound(spans.begin(), spans.end(), span, longer);
    spans.insert(it, span);
    if (static_cast<int>(spans.size()) > kMaxRetainedSpans)
        spans.pop_back();
}

// skipToNextFF leaves the reader on a 0xFF. Consume fill bytes and, on SOI,
// report the offset of the FF D8 pair (not an earlier fill byte).
bool readSoi(ChunkReader &scanner, qint64 &start)
{
    uint8_t ignored = 0;
    if (!scanner.readByte(ignored))
        return false;
    uint8_t byte = 0;
    if (!scanner.readByte(byte))
        return false;
    while (byte == 0xff)
    {
        if (!scanner.readByte(byte))
            return false;
    }
    if (byte != 0xd8)
        return false;
    start = scanner.position() - 2;
    return start >= 0;
}

void updatePeak(size_t bytes);

// Longest-first list of structurally valid JPEGs. Peak storage is the two
// scanner windows (this reader and measureJpeg's); payloads stay on disk.
std::vector<JpegSpan> scanJpegSpans(QFile &scanFile, QFile &dataFile)
{
    std::vector<JpegSpan> spans;
    ChunkReader scanner(scanFile);
    if (!scanner.seek(0))
        return spans;
    updatePeak(static_cast<size_t>(2 * kScannerChunkBytes));

    while (scanner.skipToNextFF())
    {
        const qint64 before = scanner.position();
        qint64 start = 0;
        if (!readSoi(scanner, start))
        {
            if (scanner.position() <= before)
                break;
            continue;
        }
        JpegSpan span;
        if (measureJpeg(dataFile, start, span))
            retainSpan(spans, span);
    }
    return spans;
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

ImageData toImageData(const QImage &src)
{
    if (src.isNull())
        return ImageData();
    const QImage img =
        (src.format() == QImage::Format_RGB888) ? src : src.convertToFormat(QImage::Format_RGB888);
    if (img.isNull())
        return ImageData();
    ImageData out = makeImageData(img.width(), img.height(), PixelFormat::RGB24);
    const int w = img.width();
    const int h = img.height();
    const size_t rowBytes = static_cast<size_t>(w) * 3;
    const qsizetype bytesPerLine = img.bytesPerLine();
    if (bytesPerLine == static_cast<qsizetype>(rowBytes) && out.stride() == rowBytes)
    {
        std::memcpy(out.buffer->data(), img.constBits(), rowBytes * static_cast<size_t>(h));
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uchar *s = img.constScanLine(y);
            uint8_t *d = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            std::memcpy(d, s, rowBytes);
        }
    }
    return out;
}

QSize embeddedFullSize(QImageReader &reader, const JpegSpan &span)
{
    const QSize reported = reader.size();
    if (reported.isValid() && !reported.isEmpty())
        return reported;
    // reader.size() is the unrotated SOF size on Qt 6.8. Fall back to the
    // dimensions the marker walk already recorded, still before pixel decode.
    if (span.width > 0 && span.height > 0)
        return QSize(span.width, span.height);
    return QSize();
}

// Copy and decode a single span. setScaledSize is applied whenever the full
// size is known; QImage::scaled runs only if the plugin ignores that request.
ImageData decodeEmbeddedJpeg(QFile &dataFile, const JpegSpan &span, int maxEdge)
{
    QByteArray jpeg = readRange(dataFile, span.start, span.length);
    if (static_cast<qint64>(jpeg.size()) != span.length)
        return ImageData();
    updatePeak(static_cast<size_t>(2 * kScannerChunkBytes) + static_cast<size_t>(jpeg.size()));

    QBuffer buf(&jpeg);
    if (!buf.open(QIODevice::ReadOnly))
        return ImageData();
    QImageReader reader(&buf, "JPEG");
    reader.setAutoTransform(true);
    const QSize fullSize = embeddedFullSize(reader, span);
    if (maxEdge > 0 && fullSize.isValid() && !fullSize.isEmpty() &&
        (fullSize.width() > maxEdge || fullSize.height() > maxEdge))
    {
        const double r =
            static_cast<double>(maxEdge) / std::max(fullSize.width(), fullSize.height());
        reader.setScaledSize(QSize(std::max(1, static_cast<int>(fullSize.width() * r)),
                                   std::max(1, static_cast<int>(fullSize.height() * r))));
    }
    QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    if (maxEdge > 0 && (img.width() > maxEdge || img.height() > maxEdge))
    {
        const double r = static_cast<double>(maxEdge) / std::max(img.width(), img.height());
        img = img.scaled(std::max(1, static_cast<int>(img.width() * r)),
                         std::max(1, static_cast<int>(img.height() * r)), Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    }
    return toImageData(img);
}

} // namespace

bool RawDecoder::canDecode(const std::string &path) const
{
    const QString ext =
        QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size()))).suffix().toLower();
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
    const std::vector<JpegSpan> spans = scanJpegSpans(scanFile, dataFile);
    for (const JpegSpan &span : spans)
    {
        ImageData decoded = decodeEmbeddedJpeg(dataFile, span, maxEdge);
        if (!decoded.isNull())
            return decoded;
    }
    return ImageData();
}

ImageData RawDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData RawDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    return decodeRawPixels(path, maxEdge, false);
}

ImageData RawDecoder::decodeScaled(const std::string &path, int maxEdge,
                                   mviewer::domain::ImageMetadata &outMeta) const
{
    ImageData d = decodeRawPixels(path, maxEdge, false);
    if (!d.isNull())
    {
        const QFileInfo fi(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
        outMeta.filePath = path;
        outMeta.fileName = fi.fileName().toUtf8().toStdString();
        outMeta.fileSize = std::max<int64_t>(0, static_cast<int64_t>(fi.size()));
        outMeta.width = d.width;
        outMeta.height = d.height;
        outMeta.format = "RAW";
        outMeta.channels = 3;
    }
    return d;
}

mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
RawDecoder::decodeFullResult(const std::string &path, mviewer::domain::ImageMetadata &outMeta) const
{
    bool exists = true;
    try
    {
        const auto stdPath = mviewer::core::pathFromUtf8(path);
        std::error_code ec;
        if (!std::filesystem::exists(stdPath, ec) && !ec)
            exists = false;
    }
    catch (...)
    {
        exists = true;
    }
    const bool isNetworkPath = (path.size() >= 2 && (path[0] == '/' || path[0] == '\\') &&
                                (path[1] == '/' || path[1] == '\\'));
    if (!exists && !isNetworkPath)
    {
        return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::err(
            {mviewer::domain::DecodeErrorCode::FileNotFound, "RAW file not found: " + path, path});
    }

    ImageData d = decodeRawPixels(path, 0, true);
    if (d.isNull() && mviewer::core::cooperativeCancelRequested())
    {
        return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::err(
            {mviewer::domain::DecodeErrorCode::Cancelled, "RawDecoder: decode cancelled: " + path,
             path});
    }
    if (d.isNull())
    {
        return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::err(
            {mviewer::domain::DecodeErrorCode::CorruptData,
             "RawDecoder: failed to extract preview or demosaic RAW image: " + path, path});
    }

    outMeta.width = d.width;
    outMeta.height = d.height;
    outMeta.format = "RAW";
    outMeta.channels = 3;
    if (outMeta.filePath.empty())
        outMeta.filePath = path;

    return mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>::ok(std::move(d));
}

ImageData RawDecoder::decodeFull(const std::string &path,
                                 mviewer::domain::ImageMetadata &outMeta) const
{
    auto res = decodeFullResult(path, outMeta);
    if (res.has_value())
        return std::move(res.value());
    return ImageData();
}

bool RawDecoder::probePreviewSize(const std::string &path, int &outW, int &outH) const
{
    outW = 0;
    outH = 0;
    g_lastPreviewFullFileCopyBytes.store(0, std::memory_order_relaxed);
    g_lastPreviewPeakBufferedBytes.store(0, std::memory_order_relaxed);
    const QString nativePath = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    QFile scanFile(nativePath);
    QFile dataFile(nativePath);
    if (!scanFile.open(QIODevice::ReadOnly) || !dataFile.open(QIODevice::ReadOnly))
        return false;
    const std::vector<JpegSpan> spans = scanJpegSpans(scanFile, dataFile);
    if (spans.empty())
        return false;
    outW = spans.front().width;
    outH = spans.front().height;
    return outW > 0 && outH > 0;
}

bool RawDecoder::canProbe(const std::string &path) const
{
    return canDecode(path);
}

bool RawDecoder::probeMetadata(const std::string &path,
                               mviewer::domain::ImageMetadata &outMeta) const
{
    int w = 0;
    int h = 0;
    if (!probePreviewSize(path, w, h))
        return false;
    const QFileInfo fi(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    outMeta.filePath = path;
    outMeta.fileName = fi.fileName().toUtf8().toStdString();
    outMeta.fileSize = std::max<int64_t>(0, static_cast<int64_t>(fi.size()));
    outMeta.width = w;
    outMeta.height = h;
    outMeta.format = "RAW";
    outMeta.channels = 3;
    outMeta.orientation = 1;
    return true;
}

bool RawDecoder::canNativeLod(const std::string &path) const
{
    // Extension-gated so hasNativeLod() stays cheap (no container scan).
    // decodeLod itself is honest: embedded JPEG when the preview edge covers
    // the request, LibRaw half-size when the preview is short and the request
    // is at most half the sensor, otherwise empty. It does not full-demosaic
    // and then downscale, so a reduced request is not a silent full decode.
    return canDecode(path);
}

bool RawDecoder::canNativeRegion(const std::string &path) const
{
    (void)path;
    // Neither the embedded JPEG nor LibRaw half/full demosaic is random-access
    // tile decode. Region tiles must not claim NativeRegion for RAW.
    return false;
}

ImageData RawDecoder::decodeLod(const std::string &path, int maxEdge,
                                mviewer::domain::ImageMetadata &outMeta) const
{
    ImageData d = decodeRawPixels(path, maxEdge, false);
    if (d.isNull())
        return ImageData();
    const QFileInfo fi(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    if (outMeta.fileName.empty())
        outMeta.fileName = fi.fileName().toUtf8().toStdString();
    outMeta.fileSize = std::max<int64_t>(0, static_cast<int64_t>(fi.size()));
    // Preserve source (preview) geometry in metadata when probe already filled it;
    // otherwise report the decoded raster dims (preview is our display full-res).
    if (outMeta.width <= 0 || outMeta.height <= 0)
    {
        outMeta.width = d.width;
        outMeta.height = d.height;
    }
    outMeta.format = "RAW";
    outMeta.channels = 3;
    return d;
}

std::vector<std::string> RawDecoder::extensions() const
{
    std::vector<std::string> v;
    for (const char *e : kRawExts)
        v.emplace_back(e);
    return v;
}
