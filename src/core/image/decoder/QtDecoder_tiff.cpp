// Bounded TIFF display LOD for builds without WIC (perf review P2-4). Windows
// scales TIFF through WIC; elsewhere Qt's TIFF plugin decodes the full raster
// before scaling. Baseline uncompressed, chunky, strip-organised TIFFs are
// parsed here and streamed row by row into a box-averaged target QImage. Any
// other layout (compressed, tiled, planar, rotated) stays on QImageReader.
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t kMaxStrips = 65535;

enum class AlphaMode
{
    None,
    Associated,
    Unassociated
};

struct TiffInfo
{
    bool bigEndian = false;
    int width = 0;
    int height = 0;
    int samples = 0;
    int bits = 0;
    AlphaMode alpha = AlphaMode::None;
    uint32_t rowsPerStrip = 0;
    uint64_t rowBytes = 0;
    std::vector<uint32_t> stripOffsets;
    std::vector<uint32_t> stripCounts;
};

struct TiffTags
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> bitsPerSample;
    uint32_t compression = 1;
    uint32_t photometric = 0xFFFFFFFFu;
    uint32_t orientation = 1;
    uint32_t samples = 1;
    uint32_t rowsPerStrip = 0xFFFFFFFFu;
    uint32_t planar = 1;
    uint32_t sampleFormat = 1;
    std::vector<uint32_t> extraSamples;
    std::vector<uint32_t> stripOffsets;
    std::vector<uint32_t> stripCounts;
    bool tiled = false;
    bool photometricSet = false;
};

QString pathToQString(const std::string &path)
{
    return QString::fromUtf8(path.data(), static_cast<int>(path.size()));
}

uint32_t rd16(const unsigned char *p, bool be)
{
    return be ? ((static_cast<uint32_t>(p[0]) << 8) | p[1])
              : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8));
}

uint32_t rd32(const unsigned char *p, bool be)
{
    if (be)
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool readAt(QFile &f, qint64 pos, char *dst, qint64 n)
{
    if (pos < 0 || n < 0 || pos + n > f.size())
        return false;
    if (f.pos() != pos && !f.seek(pos))
        return false;
    return f.read(dst, n) == n;
}

// Reads a SHORT/LONG tag's values (inline when they fit in 4 bytes).
bool readValues(QFile &f, const unsigned char *entry, bool be, std::vector<uint32_t> &out)
{
    const uint32_t type = rd16(entry + 2, be);
    const uint32_t count = rd32(entry + 4, be);
    const uint32_t size = type == 3 ? 2u : (type == 4 ? 4u : 0u);
    if (size == 0 || count == 0 || count > kMaxStrips)
        return false;
    const uint64_t bytes = static_cast<uint64_t>(count) * size;
    QByteArray buf;
    const unsigned char *src = entry + 8;
    if (bytes > 4)
    {
        buf.resize(static_cast<qsizetype>(bytes));
        if (!readAt(f, rd32(entry + 8, be), buf.data(), static_cast<qint64>(bytes)))
            return false;
        src = reinterpret_cast<const unsigned char *>(buf.constData());
    }
    out.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        out[i] = size == 2 ? rd16(src + static_cast<size_t>(i) * 2, be)
                           : rd32(src + static_cast<size_t>(i) * 4, be);
    return true;
}

bool readScalar(QFile &f, const unsigned char *entry, bool be, uint32_t &out)
{
    std::vector<uint32_t> v;
    if (!readValues(f, entry, be, v))
        return false;
    out = v[0];
    return true;
}

bool applyScalarTag(QFile &f, uint32_t tag, const unsigned char *e, bool be, TiffTags &t)
{
    switch (tag)
    {
    case 256:
        return readScalar(f, e, be, t.width);
    case 257:
        return readScalar(f, e, be, t.height);
    case 259:
        return readScalar(f, e, be, t.compression);
    case 262:
        t.photometricSet = true;
        return readScalar(f, e, be, t.photometric);
    case 274:
        return readScalar(f, e, be, t.orientation);
    case 277:
        return readScalar(f, e, be, t.samples);
    case 278:
        return readScalar(f, e, be, t.rowsPerStrip);
    case 284:
        return readScalar(f, e, be, t.planar);
    case 339:
        return readScalar(f, e, be, t.sampleFormat);
    default:
        return true;
    }
}

bool applyTag(QFile &f, const unsigned char *e, bool be, TiffTags &t)
{
    const uint32_t tag = rd16(e, be);
    switch (tag)
    {
    case 258:
        return readValues(f, e, be, t.bitsPerSample);
    case 273:
        return readValues(f, e, be, t.stripOffsets);
    case 279:
        return readValues(f, e, be, t.stripCounts);
    case 338:
        return readValues(f, e, be, t.extraSamples);
    case 322:
    case 323:
    case 324:
    case 325:
        t.tiled = true;
        return true;
    default:
        return applyScalarTag(f, tag, e, be, t);
    }
}

bool readTags(QFile &f, TiffTags &t, bool &be)
{
    unsigned char hdr[8];
    if (!readAt(f, 0, reinterpret_cast<char *>(hdr), 8))
        return false;
    if (hdr[0] == 'I' && hdr[1] == 'I')
        be = false;
    else if (hdr[0] == 'M' && hdr[1] == 'M')
        be = true;
    else
        return false;
    if (rd16(hdr + 2, be) != 42) // 43 is BigTIFF: not handled here
        return false;
    const uint32_t ifd = rd32(hdr + 4, be);
    unsigned char cnt[2];
    if (!readAt(f, ifd, reinterpret_cast<char *>(cnt), 2))
        return false;
    const uint32_t n = rd16(cnt, be);
    QByteArray entries(static_cast<qsizetype>(n) * 12, '\0');
    if (n == 0 || !readAt(f, static_cast<qint64>(ifd) + 2, entries.data(), entries.size()))
        return false;
    const auto *e = reinterpret_cast<const unsigned char *>(entries.constData());
    for (uint32_t i = 0; i < n; ++i)
    {
        if (!applyTag(f, e + static_cast<size_t>(i) * 12, be, t))
            return false;
    }
    return true;
}

bool layoutOk(const TiffTags &t)
{
    if (t.compression != 1 || t.planar != 1 || t.tiled || t.sampleFormat != 1)
        return false;
    if (t.orientation != 1 || !t.photometricSet)
        return false;
    if (t.width < 1 || t.width > 65535 || t.height < 1 || t.height > 65535)
        return false;
    if (t.bitsPerSample.size() != t.samples)
        return false;
    const uint32_t bits = t.bitsPerSample[0];
    if (bits != 8 && bits != 16)
        return false;
    return std::all_of(t.bitsPerSample.begin(), t.bitsPerSample.end(),
                       [bits](uint32_t b) { return b == bits; });
}

bool channelsOk(const TiffTags &t, AlphaMode &alpha)
{
    alpha = AlphaMode::None;
    if (t.photometric == 1 && t.samples == 1 && t.extraSamples.empty())
        return true;
    if (t.photometric != 2)
        return false;
    if (t.samples == 3 && t.extraSamples.empty())
        return true;
    if (t.samples != 4 || t.extraSamples.size() != 1)
        return false;
    if (t.extraSamples[0] == 1)
        alpha = AlphaMode::Associated;
    else if (t.extraSamples[0] == 2)
        alpha = AlphaMode::Unassociated;
    else
        return false;
    return true;
}

bool stripsOk(const TiffTags &t, uint64_t rowBytes, uint32_t rps, qint64 fileSize)
{
    const uint64_t strips = (static_cast<uint64_t>(t.height) + rps - 1) / rps;
    if (strips > kMaxStrips || t.stripOffsets.size() != strips || t.stripCounts.size() != strips)
        return false;
    for (uint64_t s = 0; s < strips; ++s)
    {
        const uint64_t rows = std::min<uint64_t>(rps, t.height - s * rps);
        const uint64_t need = rows * rowBytes;
        if (t.stripCounts[s] < need)
            return false;
        if (static_cast<uint64_t>(t.stripOffsets[s]) + need > static_cast<uint64_t>(fileSize))
            return false;
    }
    return true;
}

bool parseTiff(QFile &f, TiffInfo &info)
{
    TiffTags t;
    bool be = false;
    if (!readTags(f, t, be) || !layoutOk(t))
        return false;
    AlphaMode alpha = AlphaMode::None;
    if (!channelsOk(t, alpha))
        return false;
    const uint32_t rps = std::min(t.rowsPerStrip, t.height);
    if (rps == 0)
        return false;
    const uint64_t rowBytes = static_cast<uint64_t>(t.width) * t.samples * (t.bitsPerSample[0] / 8);
    if (!stripsOk(t, rowBytes, rps, f.size()))
        return false;
    info.bigEndian = be;
    info.width = static_cast<int>(t.width);
    info.height = static_cast<int>(t.height);
    info.samples = static_cast<int>(t.samples);
    info.bits = static_cast<int>(t.bitsPerSample[0]);
    info.alpha = alpha;
    info.rowsPerStrip = rps;
    info.rowBytes = rowBytes;
    info.stripOffsets = std::move(t.stripOffsets);
    info.stripCounts = std::move(t.stripCounts);
    return true;
}

bool openAndParse(const std::string &path, QFile &f, TiffInfo &info)
{
    f.setFileName(pathToQString(path));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return parseTiff(f, info);
}

QImage::Format outputFormat(const TiffInfo &info)
{
    const bool deep = info.bits == 16;
    if (info.samples == 1)
        return deep ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8;
    if (info.samples == 3)
        return deep ? QImage::Format_RGBX64 : QImage::Format_RGB888;
    if (info.alpha == AlphaMode::Associated)
        return deep ? QImage::Format_RGBA64_Premultiplied : QImage::Format_RGBA8888_Premultiplied;
    return deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888;
}

struct Scratch
{
    std::vector<int> colMap;
    std::vector<uint32_t> colCount;
    std::vector<unsigned char> row;
    std::vector<uint64_t> acc;
};

void makeColMap(int w, int tw, Scratch &s)
{
    s.colMap.assign(static_cast<size_t>(w), 0);
    s.colCount.assign(static_cast<size_t>(tw), 0);
    for (int ox = 0; ox < tw; ++ox)
    {
        const int64_t x0 = static_cast<int64_t>(ox) * w / tw;
        const int64_t x1 = std::max(x0 + 1, static_cast<int64_t>(ox + 1) * w / tw);
        for (int64_t x = x0; x < x1 && x < w; ++x)
        {
            s.colMap[static_cast<size_t>(x)] = ox;
            s.colCount[static_cast<size_t>(ox)] += 1;
        }
    }
}

bool readRow(QFile &f, const TiffInfo &info, int y, Scratch &s)
{
    // Strip bounds were validated by stripsOk(); rows within a strip are
    // contiguous, so the seek only fires at strip boundaries.
    const uint32_t strip = static_cast<uint32_t>(y) / info.rowsPerStrip;
    const uint64_t within = static_cast<uint32_t>(y) % info.rowsPerStrip;
    const qint64 pos =
        static_cast<qint64>(info.stripOffsets[strip]) + static_cast<qint64>(within * info.rowBytes);
    if (f.pos() != pos && !f.seek(pos))
        return false;
    const auto n = static_cast<qint64>(info.rowBytes);
    return f.read(reinterpret_cast<char *>(s.row.data()), n) == n;
}

uint32_t sampleAt(const unsigned char *row, size_t index, const TiffInfo &info)
{
    if (info.bits == 8)
        return row[index];
    return rd16(row + index * 2, info.bigEndian);
}

void addRow8(const TiffInfo &info, Scratch &s)
{
    const int ch = info.samples;
    const unsigned char *p = s.row.data();
    for (int x = 0; x < info.width; ++x, p += ch)
    {
        uint64_t *acc = s.acc.data() + static_cast<size_t>(s.colMap[static_cast<size_t>(x)]) * ch;
        for (int c = 0; c < ch; ++c)
            acc[c] += p[c];
    }
}

void addRow(const TiffInfo &info, Scratch &s)
{
    const bool weight = info.alpha == AlphaMode::Unassociated;
    if (info.bits == 8 && !weight)
    {
        addRow8(info, s);
        return;
    }
    const int ch = info.samples;
    for (int x = 0; x < info.width; ++x)
    {
        const size_t src = static_cast<size_t>(x) * static_cast<size_t>(ch);
        uint64_t *acc = s.acc.data() + static_cast<size_t>(s.colMap[static_cast<size_t>(x)]) * ch;
        const uint64_t a = weight ? sampleAt(s.row.data(), src + 3, info) : 1u;
        for (int c = 0; c < ch; ++c)
        {
            const uint64_t v = sampleAt(s.row.data(), src + static_cast<size_t>(c), info);
            acc[c] += (weight && c < 3) ? v * a : v;
        }
    }
}

uint32_t averaged(const TiffInfo &info, const uint64_t *acc, int c, uint64_t n)
{
    if (info.alpha == AlphaMode::Unassociated && c < 3)
    {
        const uint64_t sumA = acc[3];
        return sumA == 0 ? 0u : static_cast<uint32_t>((acc[c] + sumA / 2) / sumA);
    }
    return static_cast<uint32_t>((acc[c] + n / 2) / n);
}

void storeRow(const TiffInfo &info, const Scratch &s, uint64_t rows, uchar *dst)
{
    const int ch = info.samples;
    const int outCh = ch == 3 && info.bits == 16 ? 4 : ch; // RGBX64 has a pad channel
    const int tw = static_cast<int>(s.colCount.size());
    for (int ox = 0; ox < tw; ++ox)
    {
        const uint64_t n = static_cast<uint64_t>(s.colCount[static_cast<size_t>(ox)]) * rows;
        const uint64_t *acc = s.acc.data() + static_cast<size_t>(ox) * ch;
        for (int c = 0; c < outCh; ++c)
        {
            const uint32_t v = c < ch ? averaged(info, acc, c, n) : 0xFFFFu;
            const size_t o = static_cast<size_t>(ox) * outCh + static_cast<size_t>(c);
            if (info.bits == 8)
                dst[o] = static_cast<uchar>(v);
            else
                reinterpret_cast<uint16_t *>(dst)[o] = static_cast<uint16_t>(v);
        }
    }
}

QImage decodeBoxed(QFile &f, const TiffInfo &info, const QSize &target)
{
    const int tw = target.width();
    const int th = target.height();
    if (tw <= 0 || th <= 0 || tw > info.width || th > info.height)
        return QImage();
    Scratch s;
    makeColMap(info.width, tw, s);
    s.row.assign(static_cast<size_t>(info.rowBytes), 0);
    s.acc.assign(static_cast<size_t>(tw) * static_cast<size_t>(info.samples), 0);
    QImage out(tw, th, outputFormat(info));
    if (out.isNull())
        return QImage();
    for (int oy = 0; oy < th; ++oy)
    {
        std::fill(s.acc.begin(), s.acc.end(), 0);
        const int64_t y0 = static_cast<int64_t>(oy) * info.height / th;
        const int64_t y1 = std::max(y0 + 1, static_cast<int64_t>(oy + 1) * info.height / th);
        for (int64_t y = y0; y < y1; ++y)
        {
            if (!readRow(f, info, static_cast<int>(y), s))
                return QImage();
            addRow(info, s);
        }
        storeRow(info, s, static_cast<uint64_t>(y1 - y0), out.scanLine(oy));
    }
    return out;
}

} // namespace

namespace mviewer::core::detail
{

// Declared in QtDecoder.cpp next to the BMP/PNG adapters (core headers stay Qt-free).
bool tiffStripLodEligible(const std::string &path);
QImage decodeTiffStripLod(const std::string &path, const QSize &target);

bool tiffStripLodEligible(const std::string &path)
{
    try
    {
        QFile f;
        TiffInfo info;
        return openAndParse(path, f, info);
    }
    catch (...)
    {
        return false;
    }
}

QImage decodeTiffStripLod(const std::string &path, const QSize &target)
{
    try
    {
        if (!target.isValid() || target.isEmpty())
            return QImage();
        QFile f;
        TiffInfo info;
        if (!openAndParse(path, f, info))
            return QImage();
        return decodeBoxed(f, info, target);
    }
    catch (...)
    {
        return QImage();
    }
}

} // namespace mviewer::core::detail
