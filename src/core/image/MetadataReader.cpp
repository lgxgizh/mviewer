#include "core/image/MetadataReader.h"
#include "core/image/IccProfile.h"

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
    const QString qPath = QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size()));
    const QFileInfo fi(qPath);
    const QString k = qPath + QString::number(fi.size()) +
                      QString::number(fi.lastModified().toMSecsSinceEpoch());
    return k.toStdString();
}

void populateFileIdentity(mviewer::domain::ImageMetadata &meta, const std::string &filePath,
                          const QFileInfo &fileInfo)
{
    meta.filePath = filePath;
    meta.fileName = fileInfo.fileName().toUtf8().toStdString();
    meta.fileSize = fileInfo.size();
    meta.modifiedEpochSec = fileInfo.lastModified().toSecsSinceEpoch();
}

void populateImageInfo(mviewer::domain::ImageMetadata &meta, QImageReader &reader)
{
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
}

void populateDisplayMetadata(mviewer::domain::ImageMetadata &meta, const std::string &filePath)
{
    // DPI + embedded ICC profile require the decoded image's metadata. Read at
    // a 1x1 scaled size so we get the headers/metadata cheaply without decoding
    // full pixels (important for 100MP originals).
    {
        QImageReader dpiReader(
            QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())));
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
            if (meta.hasIccProfile)
            {
                const QByteArray icc = img.colorSpace().iccProfile();
                const QByteArray encoded = icc.toBase64();
                meta.textKeys["MViewer.DisplayICC.Base64"] =
                    std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
                const auto pi =
                    parseIccProfile(reinterpret_cast<const unsigned char *>(icc.constData()),
                                    static_cast<size_t>(icc.size()));
                meta.iccDescription = pi.description;
                meta.iccCopyright = pi.copyright;
                meta.iccColorSpace = pi.colorSpace;
                meta.iccDeviceClass = pi.deviceClass;
                meta.iccPcs = pi.pcs;
                meta.iccRenderingIntent = pi.renderingIntent;
                meta.iccVersion = pi.version;
            }
        }
    }
}

void populateTextMetadata(mviewer::domain::ImageMetadata &meta, QImageReader &reader)
{
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
}
mviewer::domain::ImageMetadata MetadataReader::read(const std::string &filePath)
{
    mviewer::domain::ImageMetadata meta;
    const QFileInfo fi(QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())));
    if (!fi.exists())
        return meta;
    populateFileIdentity(meta, filePath, fi);
    QImageReader reader(QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())));
    populateImageInfo(meta, reader);
    populateDisplayMetadata(meta, filePath);
    populateTextMetadata(meta, reader);
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
    const uint32_t num = readU32(buf + offset, isLittle);
    const uint32_t den = readU32(buf + offset + 4, isLittle);
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

namespace
{
QByteArray readExifPayload(QFile &file, bool isJpeg)
{
    if (!isJpeg)
    {
        file.seek(0);
        return file.readAll();
    }
    file.seek(2);
    while (file.pos() + 4 <= file.size())
    {
        const QByteArray marker = file.read(2);
        if (marker.size() < 2 || static_cast<unsigned char>(marker[0]) != 0xFF)
            break;
        const unsigned char code = static_cast<unsigned char>(marker[1]);
        const QByteArray length = file.read(2);
        if (length.size() < 2)
            break;
        int segmentSize = (static_cast<unsigned char>(length[0]) << 8) |
                          static_cast<unsigned char>(length[1]);
        if (segmentSize < 2)
            break;
        segmentSize -= 2;
        if (code != 0xE1 || segmentSize < 6)
        {
            file.seek(file.pos() + segmentSize);
            continue;
        }
        const QByteArray signature = file.read(6);
        segmentSize -= 6;
        if (signature == QByteArray("Exif\0\0", 6))
            return file.read(segmentSize);
        file.seek(file.pos() + segmentSize);
    }
    return {};
}

uint32_t findGpsIfd(const unsigned char *data, int size, bool little)
{
    const uint32_t ifd0 = readU32(data + 4, little);
    if (ifd0 == 0 || ifd0 + 2 > static_cast<uint32_t>(size))
        return 0;
    const uint16_t count = readU16(data + ifd0, little);
    for (uint16_t i = 0; i < count; ++i)
    {
        const int offset = static_cast<int>(ifd0 + 2 + i * 12);
        if (offset + 12 > size)
            break;
        if (readU16(data + offset, little) == 0x8825)
            return readU32(data + offset + 8, little);
    }
    return 0;
}

struct GpsValues
{
    double lat = 0.0;
    double lon = 0.0;
    double altitude = 0.0;
    bool hasLat = false;
    bool hasLon = false;
    bool hasAlt = false;
};

GpsValues parseGpsIfd(const unsigned char *data, int size, bool little, uint32_t ifd)
{
    GpsValues values;
    const uint16_t count = readU16(data + ifd, little);
    bool north = true;
    bool east = true;
    bool aboveSea = true;
    double lat[3] = {};
    double lon[3] = {};
    for (uint16_t i = 0; i < count; ++i)
    {
        const int offset = static_cast<int>(ifd + 2 + i * 12);
        if (offset + 12 > size)
            break;
        const uint16_t tag = readU16(data + offset, little);
        if (tag == 0x0001 && offset + 8 < size)
            north = data[offset + 8] == 'N';
        else if (tag == 0x0003 && offset + 8 < size)
            east = data[offset + 8] == 'E';
        else if (tag == 0x0005 && offset + 8 < size)
            aboveSea = data[offset + 8] == 0;
        else if (tag == 0x0002 || tag == 0x0004)
        {
            const uint32_t value = readU32(data + offset + 8, little);
            if (value == 0 || value + 24 > static_cast<uint32_t>(size))
                continue;
            double *target = tag == 0x0002 ? lat : lon;
            for (int component = 0; component < 3; ++component)
            {
                const uint32_t numerator = readU32(data + value + component * 8, little);
                const uint32_t denominator = readU32(data + value + component * 8 + 4, little);
                target[component] = denominator == 0
                                         ? 0.0
                                         : static_cast<double>(numerator) / denominator;
            }
            if (tag == 0x0002)
                values.hasLat = true;
            else
                values.hasLon = true;
        }
        else if (tag == 0x0006)
        {
            const uint32_t value = readU32(data + offset + 8, little);
            if (value > 0 && value + 8 <= static_cast<uint32_t>(size))
            {
                const uint32_t numerator = readU32(data + value, little);
                const uint32_t denominator = readU32(data + value + 4, little);
                values.altitude = denominator == 0 ? 0.0
                                                    : static_cast<double>(numerator) / denominator;
                values.hasAlt = true;
            }
        }
    }
    if (values.hasLat)
        values.lat = lat[0] + lat[1] / 60.0 + lat[2] / 3600.0;
    if (values.hasLon)
        values.lon = lon[0] + lon[1] / 60.0 + lon[2] / 3600.0;
    if (!north)
        values.lat = -values.lat;
    if (!east)
        values.lon = -values.lon;
    if (!aboveSea)
        values.altitude = -values.altitude;
    return values;
}
} // namespace

void MetadataReader::readGps(mviewer::domain::ImageMetadata &meta, const std::string &filePath)
{
    QFile file(QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray header = file.read(4);
    if (header.size() < 4)
        return;
    const unsigned char b0 = static_cast<unsigned char>(header[0]);
    const unsigned char b1 = static_cast<unsigned char>(header[1]);
    const bool isJpeg = b0 == 0xFF && b1 == 0xD8;
    const bool isTiff = (b0 == 0x49 && b1 == 0x49) || (b0 == 0x4D && b1 == 0x4D);
    if (!isJpeg && !isTiff)
        return;
    const QByteArray exif = readExifPayload(file, isJpeg);
    if (exif.size() < 8)
        return;
    const auto *data = reinterpret_cast<const unsigned char *>(exif.constData());
    const int size = exif.size();
    const bool little = data[0] == 0x49 && data[1] == 0x49;
    if (readU16(data + 2, little) != 0x002A)
        return;
    const uint32_t gpsIfd = findGpsIfd(data, size, little);
    if (gpsIfd == 0 || gpsIfd + 2 > static_cast<uint32_t>(size))
        return;
    const GpsValues values = parseGpsIfd(data, size, little, gpsIfd);
    if (!values.hasLat || !values.hasLon)
        return;
    meta.hasGps = true;
    meta.gpsLatitude = values.lat;
    meta.gpsLongitude = values.lon;
    if (values.hasAlt)
        meta.gpsAltitude = values.altitude;
}

} // namespace mviewer::core
