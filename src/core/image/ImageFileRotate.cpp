#include "core/image/ImageFileRotate.h"

#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <cctype>
#include <vector>

namespace mviewer::core
{
namespace
{

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string fileSuffixOf(const std::string &path)
{
    const QString q = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    return lowerAscii(QFileInfo(q).suffix().toStdString());
}

int normalizeDegreesCw(int degreesCw)
{
    int n = degreesCw % 360;
    if (n < 0)
        n += 360;
    if (n % 90 != 0)
        return -1;
    return n;
}

ImageData rotatePixelsExact(const ImageData &src, int degreesCw)
{
    switch (normalizeDegreesCw(degreesCw))
    {
    case 0:
        return src;
    case 90:
        return rotate90CW(src);
    case 180:
        return rotate180(src);
    case 270:
        return rotate90CCW(src);
    default:
        return ImageData{};
    }
}

bool atomicWriteBytes(const QString &path, const std::vector<uint8_t> &bytes, std::string *err)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
    {
        if (err)
            *err = out.errorString().toStdString();
        return false;
    }
    const qint64 n = static_cast<qint64>(bytes.size());
    if (out.write(reinterpret_cast<const char *>(bytes.data()), n) != n)
    {
        out.cancelWriting();
        if (err)
            *err = "short write";
        return false;
    }
    if (!out.commit())
    {
        if (err)
            *err = out.errorString().toStdString();
        return false;
    }
    return true;
}

// Load with auto-orientation, then destroy the reader before any overwrite so
// Windows releases the share lock (QImageReader can keep the file open).
bool loadOrientedImage(const QString &path, QImage *outImage, std::string *formatOut,
                       std::string *err)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
    {
        if (err)
            *err = "cannot read image: " + reader.errorString().toStdString();
        return false;
    }
    if (formatOut)
    {
        QByteArray fmt = reader.format();
        *formatOut = fmt.isEmpty() ? std::string() : lowerAscii(fmt.toStdString());
    }
    *outImage = std::move(image);
    return true;
}

std::string resolveEncodeFormat(const std::string &readerFormat, const std::string &suffix)
{
    std::string format = readerFormat;
    if (format.empty())
        format = Encoder::formatForExtension(suffix);
    if (format == "jpg")
        format = "jpeg";
    return format;
}

bool encodeRotated(const ImageData &rotated, const std::string &format, std::vector<uint8_t> *out,
                   std::string *err)
{
    Encoder::Params params;
    params.quality = 95;
    if (format == "png" || format == "bmp")
        params.quality = -1;
    *out = Encoder::encodeToBuffer(rotated, format, params);
    if (out->empty())
    {
        if (err)
            *err = "encode failed for format " + format;
        return false;
    }
    return true;
}

ImageFileRotateResult failResult(std::string error)
{
    ImageFileRotateResult r;
    r.error = std::move(error);
    return r;
}

} // namespace

bool isWritableRotateFormat(const std::string &suffixOrFormat)
{
    std::string s = lowerAscii(suffixOrFormat);
    if (!s.empty() && s.front() == '.')
        s.erase(s.begin());
    if (s == "jpeg")
        s = "jpg";
    const auto supported = Encoder::supportedOutputFormats();
    return std::find(supported.begin(), supported.end(), s) != supported.end();
}

ImageFileRotateResult rotateImageFile(const std::string &utf8Path, int degreesCw)
{
    if (utf8Path.empty())
        return failResult("empty path");

    const int norm = normalizeDegreesCw(degreesCw);
    if (norm < 0)
        return failResult("angle must be a multiple of 90");
    if (norm == 0)
    {
        ImageFileRotateResult r;
        r.ok = true;
        r.method = ImageRotateMethod::None;
        return r;
    }

    const QString qPath = QString::fromUtf8(utf8Path.data(), static_cast<int>(utf8Path.size()));
    const QFileInfo fi(qPath);
    if (!fi.exists() || !fi.isFile())
        return failResult("file not found");
    if (!fi.isWritable())
        return failResult("file not writable");

    const std::string suffix = fileSuffixOf(utf8Path);
    if (!isWritableRotateFormat(suffix))
        return failResult("unsupported format for rotate: " + (suffix.empty() ? "(none)" : suffix));

    QImage original;
    std::string readerFormat;
    std::string err;
    if (!loadOrientedImage(qPath, &original, &readerFormat, &err))
        return failResult(err);

    const ImageData src = mvcore::fromQImage(original);
    if (src.isNull())
        return failResult("pixel convert failed");

    const ImageData rotated = rotatePixelsExact(src, norm);
    if (rotated.isNull())
        return failResult("rotate failed");

    const std::string format = resolveEncodeFormat(readerFormat, suffix);
    std::vector<uint8_t> encoded;
    if (!encodeRotated(rotated, format, &encoded, &err))
        return failResult(err);

    if (!atomicWriteBytes(qPath, encoded, &err))
        return failResult(err.empty() ? "write failed" : err);

    ImageFileRotateResult r;
    r.ok = true;
    r.method = ImageRotateMethod::PixelRewrite;
    r.width = rotated.width;
    r.height = rotated.height;
    return r;
}

} // namespace mviewer::core
