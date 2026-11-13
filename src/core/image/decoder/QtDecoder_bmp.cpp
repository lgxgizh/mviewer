// Bounded BMP display LOD (perf review P2-4). Qt's BMP plugin decodes the full
// raster then smooth-scales it; this path streams rows and box-averages into the
// target QImage so thumbnails, preview, and viewer LOD stay memory-bounded.
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

struct BmpInfo
{
    int width = 0;
    int height = 0;
    bool topDown = false;
    int bpp = 0;
    uint32_t dataOffset = 0;
    uint32_t stride = 0;
    int paletteOffset = 0;
    int paletteEntries = 0;
};

uint16_t leU16(const QByteArray &b, int off)
{
    if (off < 0 || off + 2 > static_cast<int>(b.size()))
        return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(b.constData() + off);
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t leU32(const QByteArray &b, int off)
{
    if (off < 0 || off + 4 > static_cast<int>(b.size()))
        return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(b.constData() + off);
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int32_t leI32(const QByteArray &b, int off)
{
    return static_cast<int32_t>(leU32(b, off));
}

bool validHeaderSize(uint32_t s)
{
    return s == 40 || s == 52 || s == 56 || s == 108 || s == 124;
}

bool validBpp(uint16_t bpp)
{
    return bpp == 8 || bpp == 24 || bpp == 32;
}

bool masksMatch(const QByteArray &hdr, int off, bool hasAlpha)
{
    if (hdr.size() < off + 12)
        return false;
    if (leU32(hdr, off) != 0x00FF0000u)
        return false;
    if (leU32(hdr, off + 4) != 0x0000FF00u)
        return false;
    if (leU32(hdr, off + 8) != 0x000000FFu)
        return false;
    if (!hasAlpha)
        return true;
    if (hdr.size() < off + 16)
        return false;
    return leU32(hdr, off + 12) == 0;
}

bool compressionOk(const QByteArray &hdr, uint32_t headerSize, uint16_t bpp, uint32_t compression)
{
    // V4/V5 BI_RGB 32 bpp with an alpha mask carries real alpha: leave it to Qt.
    if (compression == 0)
        return bpp != 32 || headerSize < 56 || leU32(hdr, 66) == 0;
    if (compression != 3)
        return false;
    if (bpp != 32)
        return false;
    return masksMatch(hdr, 54, headerSize >= 56);
}

bool dimsOk(int32_t w, int64_t hAbs)
{
    if (w < 1 || w > 65535)
        return false;
    return hAbs >= 1 && hAbs <= 65535;
}

bool pixelRangeOk(uint32_t dataOffset, uint64_t stride, int64_t hAbs, qint64 fileSize)
{
    if (stride == 0 || fileSize < 0)
        return false;
    const uint64_t start = dataOffset;
    const uint64_t need = stride * static_cast<uint64_t>(hAbs);
    if (start > static_cast<uint64_t>(fileSize))
        return false;
    return need <= static_cast<uint64_t>(fileSize) - start;
}

int paletteCount(const QByteArray &hdr, uint16_t bpp)
{
    if (bpp != 8)
        return 0;
    uint32_t used = leU32(hdr, 46);
    if (used == 0)
        used = 256;
    if (used > 256)
        used = 256;
    return static_cast<int>(used);
}

bool fillInfo(const QByteArray &hdr, qint64 fileSize, BmpInfo &info)
{
    if (hdr.size() < 54)
        return false;
    if (hdr[0] != 'B' || hdr[1] != 'M')
        return false;
    const uint32_t headerSize = leU32(hdr, 14);
    if (!validHeaderSize(headerSize))
        return false;
    const int32_t w = leI32(hdr, 18);
    const int32_t hSigned = leI32(hdr, 22);
    const int64_t hAbs =
        hSigned < 0 ? -static_cast<int64_t>(hSigned) : static_cast<int64_t>(hSigned);
    if (!dimsOk(w, hAbs))
        return false;
    const uint16_t planes = leU16(hdr, 26);
    const uint16_t bpp = leU16(hdr, 28);
    if (planes != 1 || !validBpp(bpp))
        return false;
    if (!compressionOk(hdr, headerSize, bpp, leU32(hdr, 30)))
        return false;
    const uint32_t dataOffset = leU32(hdr, 10);
    const uint64_t stride = ((static_cast<uint64_t>(w) * bpp + 31) / 32) * 4;
    if (!pixelRangeOk(dataOffset, stride, hAbs, fileSize))
        return false;
    info.width = w;
    info.height = static_cast<int>(hAbs);
    info.topDown = hSigned < 0;
    info.bpp = static_cast<int>(bpp);
    info.dataOffset = dataOffset;
    info.stride = static_cast<uint32_t>(stride);
    info.paletteOffset = 14 + static_cast<int>(headerSize);
    info.paletteEntries = paletteCount(hdr, bpp);
    return true;
}

bool readHeader(QFile &f, QByteArray &hdr, qint64 &fileSize)
{
    fileSize = f.size();
    hdr = f.read(14 + 124 + 1024);
    return hdr.size() >= 54;
}

QString pathToQString(const std::string &path)
{
    return QString::fromUtf8(path.data(), static_cast<int>(path.size()));
}

bool openAndParse(const std::string &path, BmpInfo &info)
{
    QFile f(pathToQString(path));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray hdr;
    qint64 fileSize = 0;
    if (!readHeader(f, hdr, fileSize))
        return false;
    return fillInfo(hdr, fileSize, info);
}

bool loadPalette(const QByteArray &hdr, const BmpInfo &info, uint8_t pal[256][3])
{
    std::memset(pal, 0, sizeof(uint8_t[256][3]));
    if (info.bpp != 8)
        return true;
    const int need = info.paletteOffset + info.paletteEntries * 4;
    if (need > static_cast<int>(hdr.size()))
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(hdr.constData());
    for (int i = 0; i < info.paletteEntries; ++i)
    {
        const int o = info.paletteOffset + i * 4;
        pal[i][0] = p[o + 2];
        pal[i][1] = p[o + 1];
        pal[i][2] = p[o + 0];
    }
    return true;
}

void makeColMap(int w, int tw, std::vector<int> &colMap)
{
    colMap.assign(static_cast<size_t>(w), 0);
    for (int ox = 0; ox < tw; ++ox)
    {
        const int64_t x0 = static_cast<int64_t>(ox) * w / tw;
        const int64_t next = static_cast<int64_t>(ox + 1) * w / tw;
        const int64_t x1 = std::max(x0 + 1, next);
        for (int64_t x = x0; x < x1 && x < w; ++x)
            colMap[static_cast<size_t>(x)] = ox;
    }
}

void addIndexedRow(const uint8_t *row, int w, const std::vector<int> &colMap,
                   const uint8_t pal[256][3], std::vector<uint64_t> &acc)
{
    for (int x = 0; x < w; ++x)
    {
        const uint8_t *c = pal[row[x]];
        const size_t b = static_cast<size_t>(colMap[static_cast<size_t>(x)]) * 3;
        acc[b] += c[0];
        acc[b + 1] += c[1];
        acc[b + 2] += c[2];
    }
}

void addBgrRow(const uint8_t *row, int w, int step, const std::vector<int> &colMap,
               std::vector<uint64_t> &acc)
{
    const uint8_t *p = row;
    for (int x = 0; x < w; ++x, p += step)
    {
        const size_t b = static_cast<size_t>(colMap[static_cast<size_t>(x)]) * 3;
        acc[b] += p[2];
        acc[b + 1] += p[1];
        acc[b + 2] += p[0];
    }
}

void addSourceRow(const uint8_t *row, const BmpInfo &info, const std::vector<int> &colMap,
                  const uint8_t pal[256][3], std::vector<uint64_t> &acc)
{
    // Specialized per depth: no per-pixel depth branch in the hot loop.
    if (info.bpp == 8)
        addIndexedRow(row, info.width, colMap, pal, acc);
    else
        addBgrRow(row, info.width, info.bpp == 32 ? 4 : 3, colMap, acc);
}

void makeColCount(const std::vector<int> &colMap, std::vector<uint32_t> &colCount)
{
    std::fill(colCount.begin(), colCount.end(), 0);
    for (const int ox : colMap)
        colCount[static_cast<size_t>(ox)] += 1;
}

void storeOutputRow(uint8_t *dst, int tw, uint32_t rows, const std::vector<uint64_t> &acc,
                    const std::vector<uint32_t> &colCount)
{
    for (int ox = 0; ox < tw; ++ox)
    {
        const uint64_t n = static_cast<uint64_t>(colCount[static_cast<size_t>(ox)]) * rows;
        const size_t b = static_cast<size_t>(ox) * 3;
        if (n == 0)
        {
            dst[b] = dst[b + 1] = dst[b + 2] = 0;
            continue;
        }
        dst[b] = static_cast<uint8_t>((acc[b] + n / 2) / n);
        dst[b + 1] = static_cast<uint8_t>((acc[b + 1] + n / 2) / n);
        dst[b + 2] = static_cast<uint8_t>((acc[b + 2] + n / 2) / n);
    }
}

bool readFileRow(QFile &f, const BmpInfo &info, int displayY, std::vector<uint8_t> &row)
{
    const int fileRow = info.topDown ? displayY : (info.height - 1 - displayY);
    const qint64 pos = static_cast<qint64>(info.dataOffset) +
                       static_cast<qint64>(fileRow) * static_cast<qint64>(info.stride);
    // Rows are visited in file order, so this seek only fires once.
    if (f.pos() != pos && !f.seek(pos))
        return false;
    const qint64 n = f.read(reinterpret_cast<char *>(row.data()), static_cast<qint64>(info.stride));
    return n == static_cast<qint64>(info.stride);
}

struct RowScratch
{
    std::vector<int> colMap;
    std::vector<uint32_t> colCount;
    std::vector<uint8_t> row;
    std::vector<uint64_t> acc;
};

bool decodeOutRow(QFile &f, const BmpInfo &info, int oy, int th, const uint8_t pal[256][3],
                  RowScratch &s, uint8_t *dst)
{
    const int tw = static_cast<int>(s.colCount.size());
    std::fill(s.acc.begin(), s.acc.end(), 0);
    const int64_t y0 = static_cast<int64_t>(oy) * info.height / th;
    const int64_t y1 = std::max(y0 + 1, static_cast<int64_t>(oy + 1) * info.height / th);
    // Bottom-up files store the last display row first: walk y backwards so
    // reads stay sequential.
    for (int64_t i = 0; i < y1 - y0; ++i)
    {
        const int64_t y = info.topDown ? y0 + i : y1 - 1 - i;
        if (!readFileRow(f, info, static_cast<int>(y), s.row))
            return false;
        addSourceRow(s.row.data(), info, s.colMap, pal, s.acc);
    }
    storeOutputRow(dst, tw, static_cast<uint32_t>(y1 - y0), s.acc, s.colCount);
    return true;
}

QImage decodeFromFile(QFile &f, const QByteArray &hdr, const BmpInfo &info, const QSize &target)
{
    const int tw = target.width();
    const int th = target.height();
    if (tw <= 0 || th <= 0)
        return QImage();
    if (tw > info.width || th > info.height)
        return QImage();
    uint8_t pal[256][3];
    if (!loadPalette(hdr, info, pal))
        return QImage();
    RowScratch s;
    makeColMap(info.width, tw, s.colMap);
    s.colCount.assign(static_cast<size_t>(tw), 0);
    makeColCount(s.colMap, s.colCount);
    s.row.assign(info.stride, 0);
    s.acc.assign(static_cast<size_t>(tw) * 3u, 0);
    QImage out(tw, th, QImage::Format_RGB888);
    if (out.isNull())
        return QImage();
    for (int i = 0; i < th; ++i)
    {
        const int oy = info.topDown ? i : th - 1 - i;
        uchar *dst = out.scanLine(oy);
        if (dst == nullptr)
            return QImage();
        if (!decodeOutRow(f, info, oy, th, pal, s, reinterpret_cast<uint8_t *>(dst)))
            return QImage();
    }
    return out;
}

QImage decodeBmpBoxLodUnchecked(const std::string &path, const QSize &target)
{
    if (!target.isValid() || target.isEmpty())
        return QImage();
    QFile f(pathToQString(path));
    if (!f.open(QIODevice::ReadOnly))
        return QImage();
    QByteArray hdr;
    qint64 fileSize = 0;
    if (!readHeader(f, hdr, fileSize))
        return QImage();
    BmpInfo info;
    if (!fillInfo(hdr, fileSize, info))
        return QImage();
    return decodeFromFile(f, hdr, info, target);
}

} // namespace

namespace mviewer::core::detail
{

// Declared in QtDecoder.cpp next to the WIC adapter (core headers stay Qt-free).
bool bmpBoxLodEligible(const std::string &path, QSize *size);
QImage decodeBmpBoxLod(const std::string &path, const QSize &target);

bool bmpBoxLodEligible(const std::string &path, QSize *size)
{
    try
    {
        BmpInfo info;
        if (!openAndParse(path, info))
            return false;
        if (size != nullptr)
            *size = QSize(info.width, info.height);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

QImage decodeBmpBoxLod(const std::string &path, const QSize &target)
{
    try
    {
        return decodeBmpBoxLodUnchecked(path, target);
    }
    catch (...)
    {
        return QImage();
    }
}

} // namespace mviewer::core::detail
