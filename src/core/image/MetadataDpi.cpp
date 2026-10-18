#include "core/image/MetadataReader.h"

#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mviewer::core
{
namespace
{

uint16_t readU16(const unsigned char *buf, bool little)
{
    if (little)
        return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    return (static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]);
}

uint32_t readU32(const unsigned char *buf, bool little)
{
    if (little)
        return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
               (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
    return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
           (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

bool fits(size_t offset, size_t need, size_t size)
{
    return offset <= size && need <= size - offset;
}

// Match QImage::dotsPerMeter -> qRound(dpm * 0.0254), including Qt's two-step
// rounding for a physical resolution that was stored as dpi or dpcm.
int dpiFromDpm(int dpm)
{
    if (dpm <= 0)
        return 0;
    return qRound(static_cast<double>(dpm) * 0.0254);
}

int dpiFromResolution(double res, int unit)
{
    if (!(res > 0.0) || !std::isfinite(res))
        return 0;
    double dpm = 0.0;
    if (unit == 2)
        dpm = res / 0.0254;
    else if (unit == 3)
        dpm = res * 100.0;
    else
        return 0;
    if (!(dpm > 0.0) || dpm > static_cast<double>(std::numeric_limits<int>::max()))
        return 0;
    return dpiFromDpm(qRound(dpm));
}

int dpiFromJfif(int density, int unit)
{
    if (density <= 0)
        return 0;
    if (unit == 1)
        return dpiFromDpm(qRound(static_cast<double>(density) * 100.0 / 2.54));
    if (unit == 2)
    {
        const int64_t dpm = static_cast<int64_t>(density) * 100;
        if (dpm > static_cast<int64_t>(std::numeric_limits<int>::max()))
            return 0;
        return dpiFromDpm(static_cast<int>(dpm));
    }
    return 0;
}

int dpiFromPpm(uint32_t ppm)
{
    if (ppm == 0 || ppm > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        return 0;
    return dpiFromDpm(static_cast<int>(ppm));
}

struct Density
{
    int x = 0;
    int y = 0;
    bool found = false;
};

double readRational(const unsigned char *data, size_t size, uint32_t offset, bool little)
{
    if (!fits(offset, 8, size))
        return 0.0;
    const uint32_t num = readU32(data + offset, little);
    const uint32_t den = readU32(data + offset + 4, little);
    if (den == 0)
        return 0.0;
    return static_cast<double>(num) / static_cast<double>(den);
}

// IFD0 XResolution / YResolution / ResolutionUnit. Unit 2 = inch, 3 = cm.
// Missing unit defaults to inch, matching the TIFF/EXIF default.
bool densityFromIfd0(const unsigned char *data, size_t size, Density &out)
{
    if (data == nullptr || size < 8)
        return false;
    const bool little = data[0] == 'I' && data[1] == 'I';
    const bool big = data[0] == 'M' && data[1] == 'M';
    if (!little && !big)
        return false;
    if (readU16(data + 2, little) != 42)
        return false;
    const uint32_t ifd0 = readU32(data + 4, little);
    if (!fits(ifd0, 2, size))
        return false;
    const uint16_t count = readU16(data + ifd0, little);
    bool hasX = false;
    bool hasY = false;
    bool hasUnit = false;
    uint32_t xOff = 0;
    uint32_t yOff = 0;
    int unit = 2;
    const uint16_t capped = count > 1024 ? 1024 : count;
    for (uint16_t i = 0; i < capped; ++i)
    {
        const size_t entry = static_cast<size_t>(ifd0) + 2 + static_cast<size_t>(i) * 12;
        if (!fits(entry, 12, size))
            break;
        const uint16_t tag = readU16(data + entry, little);
        const uint16_t type = readU16(data + entry + 2, little);
        const uint32_t n = readU32(data + entry + 4, little);
        if (n < 1)
            continue;
        if ((tag == 0x011A || tag == 0x011B) && type == 5)
        {
            const uint32_t off = readU32(data + entry + 8, little);
            if (tag == 0x011A)
            {
                hasX = true;
                xOff = off;
            }
            else
            {
                hasY = true;
                yOff = off;
            }
        }
        else if (tag == 0x0128 && type == 3)
        {
            hasUnit = true;
            unit = readU16(data + entry + 8, little);
        }
    }
    if (!hasX && !hasY)
        return false;
    if (!hasUnit)
        unit = 2;
    if (unit != 2 && unit != 3)
        return false;
    out.x = hasX ? dpiFromResolution(readRational(data, size, xOff, little), unit) : 0;
    out.y = hasY ? dpiFromResolution(readRational(data, size, yOff, little), unit) : 0;
    out.found = out.x > 0 || out.y > 0;
    return out.found;
}

bool parseJfif(const QByteArray &payload, Density &out)
{
    if (payload.size() < 14 || !payload.startsWith("JFIF"))
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(payload.constData());
    const int units = p[7];
    const int x = (static_cast<int>(p[8]) << 8) | static_cast<int>(p[9]);
    const int y = (static_cast<int>(p[10]) << 8) | static_cast<int>(p[11]);
    out.x = dpiFromJfif(x, units);
    out.y = dpiFromJfif(y, units);
    out.found = out.x > 0 || out.y > 0;
    return out.found;
}

bool parseExifApp1(const QByteArray &payload, Density &out)
{
    if (payload.size() < 14 || !payload.startsWith(QByteArray("Exif\0\0", 6)))
        return false;
    const auto *data = reinterpret_cast<const unsigned char *>(payload.constData() + 6);
    const size_t size = static_cast<size_t>(payload.size() - 6);
    return densityFromIfd0(data, size, out);
}

// Reads one marker payload. Standalone markers leave `payload` empty and return
// true. False means the header walk should stop.
bool readJpegSegment(QFile &file, unsigned char code, QByteArray &payload)
{
    payload.clear();
    if (code == 0x01 || (code >= 0xD0 && code <= 0xD7))
        return true;
    const QByteArray lenBytes = file.read(2);
    if (lenBytes.size() < 2)
        return false;
    const int segment = (static_cast<int>(static_cast<unsigned char>(lenBytes[0])) << 8) |
                        static_cast<int>(static_cast<unsigned char>(lenBytes[1]));
    if (segment < 2)
        return false;
    const int payloadBytes = segment - 2;
    const qint64 remain = file.size() - file.pos();
    if (remain < payloadBytes)
        return false;
    const bool want = (code == 0xE0 || code == 0xE1) && payloadBytes <= 2 * 1024 * 1024;
    if (!want)
        return file.seek(file.pos() + payloadBytes);
    payload = file.read(payloadBytes);
    return payload.size() == payloadBytes;
}

bool jpegHeaderDpi(const QString &path, Density &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray soi = file.read(2);
    if (soi.size() != 2 || static_cast<unsigned char>(soi[0]) != 0xFF ||
        static_cast<unsigned char>(soi[1]) != 0xD8)
        return false;

    Density exif;
    Density jfif;
    for (int guard = 0; guard < 64 && !file.atEnd(); ++guard)
    {
        const QByteArray marker = file.read(2);
        if (marker.size() < 2 || static_cast<unsigned char>(marker[0]) != 0xFF)
            break;
        unsigned char code = static_cast<unsigned char>(marker[1]);
        while (code == 0xFF)
        {
            const QByteArray more = file.read(1);
            if (more.size() != 1)
                return false;
            code = static_cast<unsigned char>(more[0]);
        }
        if (code == 0xDA || code == 0xD9)
            break;
        QByteArray payload;
        if (!readJpegSegment(file, code, payload))
            break;
        if (code == 0xE0 && !jfif.found)
            parseJfif(payload, jfif);
        else if (code == 0xE1 && !exif.found)
            parseExifApp1(payload, exif);
        if (exif.found && jfif.found)
            break;
    }
    if (exif.found)
        out = exif;
    else if (jfif.found)
        out = jfif;
    return out.found;
}

bool pngHeaderDpi(const QString &path, Density &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray sig = file.read(8);
    static const unsigned char kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (sig.size() != 8 || std::memcmp(sig.constData(), kSig, 8) != 0)
        return false;
    qint64 scanned = 8;
    constexpr qint64 kMaxScan = static_cast<qint64>(512) * 1024;
    while (scanned + 12 <= file.size() && scanned < kMaxScan)
    {
        const QByteArray hdr = file.read(8);
        if (hdr.size() != 8)
            return false;
        const auto *h = reinterpret_cast<const unsigned char *>(hdr.constData());
        const uint32_t length = (static_cast<uint32_t>(h[0]) << 24) |
                                (static_cast<uint32_t>(h[1]) << 16) |
                                (static_cast<uint32_t>(h[2]) << 8) | static_cast<uint32_t>(h[3]);
        scanned += 8;
        if (length > 16u * 1024u * 1024u)
            return false;
        const bool idat = hdr[4] == 'I' && hdr[5] == 'D' && hdr[6] == 'A' && hdr[7] == 'T';
        const bool iend = hdr[4] == 'I' && hdr[5] == 'E' && hdr[6] == 'N' && hdr[7] == 'D';
        if (idat || iend)
            return false;
        const bool phys = hdr[4] == 'p' && hdr[5] == 'H' && hdr[6] == 'Y' && hdr[7] == 's';
        if (phys && length >= 9)
        {
            const QByteArray data = file.read(9);
            if (data.size() != 9)
                return false;
            const auto *d = reinterpret_cast<const unsigned char *>(data.constData());
            const uint32_t ppmX = (static_cast<uint32_t>(d[0]) << 24) |
                                  (static_cast<uint32_t>(d[1]) << 16) |
                                  (static_cast<uint32_t>(d[2]) << 8) | static_cast<uint32_t>(d[3]);
            const uint32_t ppmY = (static_cast<uint32_t>(d[4]) << 24) |
                                  (static_cast<uint32_t>(d[5]) << 16) |
                                  (static_cast<uint32_t>(d[6]) << 8) | static_cast<uint32_t>(d[7]);
            if (d[8] != 1)
                return false;
            out.x = dpiFromPpm(ppmX);
            out.y = dpiFromPpm(ppmY);
            out.found = out.x > 0 || out.y > 0;
            return out.found;
        }
        if (!file.seek(file.pos() + static_cast<qint64>(length) + 4))
            return false;
        scanned += static_cast<qint64>(length) + 4;
    }
    return false;
}

bool tiffHeaderDpi(const QString &path, Density &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    constexpr qint64 kMax = static_cast<qint64>(1024) * 1024;
    const qint64 n = std::min(file.size(), kMax);
    if (n < 8)
        return false;
    const QByteArray prefix = file.read(n);
    if (prefix.size() < 8)
        return false;
    const auto *data = reinterpret_cast<const unsigned char *>(prefix.constData());
    return densityFromIfd0(data, static_cast<size_t>(prefix.size()), out);
}

bool parsePositive(const std::string &text, double &out)
{
    if (text.empty())
        return false;
    try
    {
        const auto slash = text.find('/');
        if (slash == std::string::npos)
        {
            out = std::stod(text);
        }
        else
        {
            const double num = std::stod(text.substr(0, slash));
            const double den = std::stod(text.substr(slash + 1));
            if (den == 0.0)
                return false;
            out = num / den;
        }
    }
    catch (...)
    {
        return false;
    }
    return out > 0.0 && std::isfinite(out);
}

int unitFromText(const std::string &text)
{
    if (text == "3" || text == "cm" || text == "centimeter")
        return 3;
    if (text == "2" || text == "inch" || text == "inches" || text.empty())
        return 2;
    return 0;
}

bool textKeyDpi(const mviewer::domain::ImageMetadata &meta, Density &out)
{
    const auto xIt = meta.textKeys.find("XResolution");
    const auto yIt = meta.textKeys.find("YResolution");
    if (xIt == meta.textKeys.end() && yIt == meta.textKeys.end())
        return false;
    const auto unitIt = meta.textKeys.find("ResolutionUnit");
    const int unit = unitFromText(unitIt == meta.textKeys.end() ? std::string() : unitIt->second);
    double xRes = 0.0;
    double yRes = 0.0;
    const bool hasX = xIt != meta.textKeys.end() && parsePositive(xIt->second, xRes);
    const bool hasY = yIt != meta.textKeys.end() && parsePositive(yIt->second, yRes);
    if (!hasX && !hasY)
        return false;
    out.x = hasX ? dpiFromResolution(xRes, unit) : 0;
    out.y = hasY ? dpiFromResolution(yRes, unit) : 0;
    out.found = out.x > 0 || out.y > 0;
    return out.found;
}

} // namespace

bool applyHeaderDpi(mviewer::domain::ImageMetadata &meta, const std::string &filePath)
{
    const QString path = QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size()));
    const QString ext = QFileInfo(path).suffix().toLower();
    Density density;
    bool parsed = false;
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("jpe"))
        parsed = jpegHeaderDpi(path, density);
    else if (ext == QLatin1String("png"))
        parsed = pngHeaderDpi(path, density);
    else if (ext == QLatin1String("tif") || ext == QLatin1String("tiff"))
        parsed = tiffHeaderDpi(path, density);
    if (!parsed)
        parsed = textKeyDpi(meta, density);
    if (!parsed || !density.found)
        return false;
    if (density.x > 0)
        meta.dpiX = density.x;
    if (density.y > 0)
        meta.dpiY = density.y;
    return meta.dpiX > 0 || meta.dpiY > 0;
}

} // namespace mviewer::core
