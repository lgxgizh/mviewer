#include "core/image/MetadataReader.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QtMath>

#include <cstring>

namespace mviewer::core
{

std::string MetadataReader::key(const std::string &filePath)
{
    const QFileInfo fi(QString::fromStdString(filePath));
    const QString k = QString::fromStdString(filePath) + QString::number(fi.size()) +
                      QString::number(fi.lastModified().toSecsSinceEpoch());
    return k.toStdString();
}

mviewer::domain::ImageMetadata MetadataReader::read(const std::string &filePath)
{
    mviewer::domain::ImageMetadata meta;
    const QFileInfo fi(QString::fromStdString(filePath));
    if (!fi.exists())
        return meta;
    meta.filePath = filePath;
    meta.fileName = fi.fileName().toStdString();
    meta.fileSize = fi.size();
    meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();

    QImageReader reader(QString::fromStdString(filePath));
    const QSize s = reader.size();
    meta.width = s.width();
    meta.height = s.height();

    // ── M18: richer metadata (no extra deps; QImageReader exposes what we need).
    const QString fmt = reader.format();
    meta.format = fmt.toUpper().toStdString();
    // Number of color channels (3 = RGB, 4 = RGBA, 1 = grayscale).
    const QImage::Format imgFmt = reader.imageFormat();
    switch (imgFmt)
    {
    case QImage::Format_Mono:
    case QImage::Format_MonoLSB:
    case QImage::Format_Grayscale8:
    case QImage::Format_Grayscale16:
        meta.channels = 1;
        break;
    case QImage::Format_RGB32:
    case QImage::Format_RGB16:
    case QImage::Format_RGB666:
    case QImage::Format_RGB555:
    case QImage::Format_RGBX8888:
    case QImage::Format_BGR888:
        meta.channels = 3;
        break;
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA32FPx4:
        meta.channels = 4;
        break;
    default:
        meta.channels = 0; // unknown / not yet decoded
        break;
    }
    meta.bitDepth = reader.imageFormat() == QImage::Format_Invalid
                        ? 0
                        : QImage(1, 1, reader.imageFormat()).depth();
    // EXIF orientation (1-8); QImageReader maps the raw tag to a Qt enum.
    const QImageIOHandler::Transformations xf = reader.transformation();
    int orient = 1;
    if (xf & QImageIOHandler::TransformationRotate180)
        orient = 3;
    else if (xf & QImageIOHandler::TransformationRotate90)
        orient = 6;
    else if (xf & QImageIOHandler::TransformationMirror)
        orient = 2;
    else if (xf & QImageIOHandler::TransformationFlip)
        orient = 4;
    meta.orientation = orient;

    // DPI + embedded ICC profile require the decoded image's metadata. Read at
    // a 1x1 scaled size so we get the headers/metadata cheaply without decoding
    // full pixels (important for 100MP originals).
    {
        QImageReader dpiReader(QString::fromStdString(filePath));
        dpiReader.setScaledSize(QSize(1, 1));
        QImage img;
        if (dpiReader.read(&img))
        {
            const int dpmx = img.dotsPerMeterX();
            const int dpmy = img.dotsPerMeterY();
            if (dpmx > 0)
                meta.dpiX = qRound(dpmx * 0.0254);
            if (dpmy > 0)
                meta.dpiY = qRound(dpmy * 0.0254);
            meta.hasIccProfile = !img.colorSpace().iccProfile().isEmpty();
        }
    }

    // Embedded text keys (EXIF/XMP/IPTCCore where the plugin exposes them).
    const QStringList keys = reader.textKeys();
    for (const QString &k : keys)
    {
        const QString v = reader.text(k);
        if (!v.isEmpty())
            meta.textKeys[k.toStdString()] = v.toStdString();
    }
    // Best-effort color space label from the embedded profile if present.
    if (!reader.text("Description").isEmpty())
        meta.colorSpace = reader.text("Description").toStdString();

    // P0: Extract GPS coordinates from EXIF GPS IFD (JPEG only; handles TIFF too).
    readGps(meta, filePath);

    meta.hash = filePath + "|" + std::to_string(meta.fileSize) + "|" +
                std::to_string(meta.modifiedEpochSec);
    return meta;
}

// ── P0: GPS IFD parser ───────────────────────────────────────────────────────

// Read a 2-byte unsigned (little-endian or big-endian).
static uint16_t readU16(const unsigned char *buf, bool little)
{
    if (little)
        return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    return (static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]);
}

// Read a 4-byte unsigned (little-endian or big-endian).
static uint32_t readU32(const unsigned char *buf, bool little)
{
    if (little)
        return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
               (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
    return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
           (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

double MetadataReader::exifToDecimal(const unsigned char *buf, int offset, bool isLittle)
{
    // EXIF rational: 8 bytes (numerator u32 + denominator u32).
    const uint32_t num = readU32(buf + offset, isLittle);
    const uint32_t den = readU32(buf + offset + 4, isLittle);
    if (den == 0)
        return 0.0;
    return static_cast<double>(num) / static_cast<double>(den);
}

void MetadataReader::readGps(mviewer::domain::ImageMetadata &meta, const std::string &filePath)
{
    // Only JPEG has well-defined EXIF APP1; skip other formats.
    // Also block non-JPEG EXIF containers (PNG EXIF not supported here).
    const QString path = QString::fromStdString(filePath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QByteArray header = file.read(4);
    if (header.size() < 4)
        return;

    bool isJpeg = false;
    if (static_cast<unsigned char>(header[0]) == 0xFF &&
        static_cast<unsigned char>(header[1]) == 0xD8)
        isJpeg = true;

    // TIFF header check (for standalone .tif files).
    bool isTiff = false;
    {
        const unsigned char b0 = static_cast<unsigned char>(header[0]);
        const unsigned char b1 = static_cast<unsigned char>(header[1]);
        if ((b0 == 0x49 && b1 == 0x49) || (b0 == 0x4D && b1 == 0x4D))
            isTiff = true;
    }

    if (!isJpeg && !isTiff)
        return;

    QByteArray exifData;
    if (isJpeg)
    {
        file.seek(2); // skip SOI marker
        QByteArray buf;
        while (file.pos() + 4 <= file.size())
        {
            buf = file.read(2);
            if (buf.size() < 2)
                break;
            const unsigned char marker = static_cast<unsigned char>(buf[0]);
            if (marker != 0xFF)
                break;
            const unsigned char code = static_cast<unsigned char>(buf[1]);
            buf = file.read(2);
            if (buf.size() < 2)
                break;
            int segLen =
                (static_cast<unsigned char>(buf[0]) << 8) | static_cast<unsigned char>(buf[1]);
            if (segLen < 2)
                break;
            segLen -= 2; // length includes the 2 length bytes themselves

            if (code == 0xE1 && segLen >= 6)
            {
                // APP1 — check for "Exif\0\0" signature
                QByteArray sig = file.read(6);
                segLen -= 6;
                if (sig.size() >= 6 && sig[0] == 'E' && sig[1] == 'x' && sig[2] == 'i' &&
                    sig[3] == 'f' && sig[4] == 0 && sig[5] == 0)
                {
                    exifData = file.read(segLen);
                    break;
                }
                file.seek(file.pos() + segLen);
            }
            else
            {
                file.seek(file.pos() + segLen);
            }
        }
    }
    else
    {
        // TIFF: read the entire file as EXIF blob.
        file.seek(0);
        exifData = file.readAll();
    }

    if (exifData.size() < 8)
        return;

    const unsigned char *d = reinterpret_cast<const unsigned char *>(exifData.constData());
    const int dSize = exifData.size();

    // Byte order: 0x4949 = little-endian (Intel), 0x4D4D = big-endian (Motorola).
    const bool little = (d[0] == 0x49 && d[1] == 0x49);

    // TIFF magic: 0x002A.
    if (readU16(d + 2, little) != 0x002A)
        return;

    // IFD0 offset (4 bytes at offset 4).
    if (dSize < 8)
        return;
    uint32_t ifd0Off = readU32(d + 4, little);

    // Walk from IFD0 → GPS IFD (tag 0x8825).
    uint32_t gpsIfdOff = 0;
    if (ifd0Off > 0 && ifd0Off + 2 <= static_cast<uint32_t>(dSize))
    {
        uint16_t numEntries = readU16(d + ifd0Off, little);
        const int entrySize = 12; // each IFD entry is 12 bytes
        for (uint16_t i = 0; i < numEntries; ++i)
        {
            int off = ifd0Off + 2 + i * entrySize;
            if (off + entrySize > dSize)
                break;
            uint16_t tag = readU16(d + off, little);
            if (tag == 0x8825) // GPSInfo tag
            {
                // Value is a 4-byte offset to the GPS IFD (stored at off+8).
                gpsIfdOff = readU32(d + off + 8, little);
                break;
            }
        }
    }

    if (gpsIfdOff == 0 || gpsIfdOff + 2 > static_cast<uint32_t>(dSize))
        return;

    // Parse GPS IFD.
    uint16_t gpsEntries = readU16(d + gpsIfdOff, little);
    // Values we want to extract.
    double latDeg = 0, latMin = 0, latSec = 0;
    double lonDeg = 0, lonMin = 0, lonSec = 0;
    bool latRefNorth = true, lonRefEast = true;
    bool hasLat = false, hasLon = false, hasAlt = false;
    double altitude = 0;
    bool altAboveSea = true;

    for (uint16_t i = 0; i < gpsEntries; ++i)
    {
        int off = gpsIfdOff + 2 + i * 12;
        if (off + 12 > dSize)
            break;
        uint16_t tag = readU16(d + off, little);

        switch (tag)
        {
        case 0x0001: // GPSLatitudeRef: "N" or "S" (2 bytes ASCII).
        {
            uint32_t v = readU32(d + off + 8, little);
            if (off + 8 < dSize)
                latRefNorth = (d[off + 8] == 'N');
            break;
        }
        case 0x0002: // GPSLatitude: 3 rationals (degree, minute, second).
        {
            uint32_t refOff = readU32(d + off + 8, little);
            if (refOff > 0 && refOff + 24 <= static_cast<uint32_t>(dSize))
            {
                latDeg = exifToDecimal(d, refOff, little);
                latMin = exifToDecimal(d, refOff + 8, little);
                latSec = exifToDecimal(d, refOff + 16, little);
                hasLat = true;
            }
            break;
        }
        case 0x0003: // GPSLongitudeRef: "E" or "W".
        {
            if (off + 8 < dSize)
                lonRefEast = (d[off + 8] == 'E');
            break;
        }
        case 0x0004: // GPSLongitude: 3 rationals.
        {
            uint32_t refOff = readU32(d + off + 8, little);
            if (refOff > 0 && refOff + 24 <= static_cast<uint32_t>(dSize))
            {
                lonDeg = exifToDecimal(d, refOff, little);
                lonMin = exifToDecimal(d, refOff + 8, little);
                lonSec = exifToDecimal(d, refOff + 16, little);
                hasLon = true;
            }
            break;
        }
        case 0x0005: // GPSAltitudeRef: 0 = above sea level, 1 = below.
        {
            if (off + 8 < dSize)
                altAboveSea = (d[off + 8] == 0);
            break;
        }
        case 0x0006: // GPSAltitude: 1 rational.
        {
            uint32_t refOff = readU32(d + off + 8, little);
            if (refOff > 0 && refOff + 8 <= static_cast<uint32_t>(dSize))
            {
                altitude = exifToDecimal(d, refOff, little);
                if (!altAboveSea)
                    altitude = -altitude;
                hasAlt = true;
            }
            break;
        }
        default:
            break;
        }
    }

    if (hasLat && hasLon)
    {
        meta.hasGps = true;
        meta.gpsLatitude = latDeg + latMin / 60.0 + latSec / 3600.0;
        if (!latRefNorth)
            meta.gpsLatitude = -meta.gpsLatitude;
        meta.gpsLongitude = lonDeg + lonMin / 60.0 + lonSec / 3600.0;
        if (!lonRefEast)
            meta.gpsLongitude = -meta.gpsLongitude;
        if (hasAlt)
            meta.gpsAltitude = altitude;
    }
}

} // namespace mviewer::core
