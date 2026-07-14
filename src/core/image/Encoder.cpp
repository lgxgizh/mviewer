#include "core/image/Encoder.h"

#include "core/image/QtConvert.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <algorithm>
#include <unordered_map>

bool Encoder::encode(const ImageData& img, const std::string& path, const Params& params)
{
    if (img.isNull())
        return false;
    QImage image = mvcore::toQImage(img);
    if (image.isNull())
        return false;

    QImageWriter writer(QString::fromStdString(path));
    QString ext = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    QString fmt = QString::fromStdString(formatForExtension(ext.toStdString()));
    writer.setFormat(fmt.toUtf8());

    if (fmt == "jpg" || fmt == "webp")
    {
        writer.setQuality(params.quality);
    }
    else if (fmt == "png")
    {
        writer.setCompression(params.pngCompression);
    }

    return writer.write(image);
}

std::vector<uint8_t>
Encoder::encodeToBuffer(const ImageData& img, const std::string& format, const Params& params)
{
    if (img.isNull())
        return {};
    QImage image = mvcore::toQImage(img);
    if (image.isNull())
        return {};

    QByteArray buffer;
    QBuffer qbuf(&buffer);
    qbuf.open(QIODevice::WriteOnly);

    QImageWriter writer(&qbuf, QString::fromStdString(format).toUtf8());
    if (format == "jpg" || format == "jpeg" || format == "webp")
    {
        writer.setQuality(params.quality);
    }
    else if (format == "png")
    {
        writer.setCompression(params.pngCompression);
    }

    if (!writer.write(image))
        return {};
    return std::vector<uint8_t>(buffer.constData(), buffer.constData() + buffer.size());
}

std::string Encoder::formatForExtension(const std::string& ext)
{
    static const std::unordered_map<std::string, std::string> map = {
        {"jpg", "jpeg"},
        {"jpeg", "jpeg"},
        {"png", "png"},
        {"bmp", "bmp"},
        {"webp", "webp"},
        {"tiff", "tiff"},
        {"tif", "tiff"},
    };
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto it = map.find(lower);
    return it != map.end() ? it->second : "png";
}

std::vector<std::string> Encoder::supportedOutputFormats()
{
    return {"png", "jpg", "bmp", "webp"};
}
