#include "core/image/ImageFileRotate.h"

#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QIODevice>
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
    ImageFileRotateResult r;
    if (utf8Path.empty())
    {
        r.error = "empty path";
        return r;
    }
    const int norm = normalizeDegreesCw(degreesCw);
    if (norm < 0)
    {
        r.error = "angle must be a multiple of 90";
        return r;
    }
    if (norm == 0)
    {
        r.ok = true;
        r.method = ImageRotateMethod::None;
        return r;
    }

    const QString qPath = QString::fromUtf8(utf8Path.data(), static_cast<int>(utf8Path.size()));
    const QFileInfo fi(qPath);
    if (!fi.exists() || !fi.isFile())
    {
        r.error = "file not found";
        return r;
    }
    if (!fi.isWritable())
    {
        r.error = "file not writable";
        return r;
    }

    const std::string suffix = fileSuffixOf(utf8Path);
    if (!isWritableRotateFormat(suffix))
    {
        r.error = "unsupported format for rotate: " + (suffix.empty() ? "(none)" : suffix);
        return r;
    }

    QImageReader reader(qPath);
    reader.setAutoTransform(true);
    const QImage original = reader.read();
    if (original.isNull())
    {
        r.error = "cannot read image: " + reader.errorString().toStdString();
        return r;
    }

    const ImageData src = mvcore::fromQImage(original);
    if (src.isNull())
    {
        r.error = "pixel convert failed";
        return r;
    }
    const ImageData rotated = rotatePixelsExact(src, norm);
    if (rotated.isNull())
    {
        r.error = "rotate failed";
        return r;
    }

    std::string format = Encoder::formatForExtension(suffix);
    if (format.empty())
        format = suffix;
    if (format == "jpg")
        format = "jpeg";

    Encoder::Params params;
    params.quality = 95;
    if (format == "png" || format == "bmp")
        params.quality = -1;

    const std::vector<uint8_t> encoded = Encoder::encodeToBuffer(rotated, format, params);
    if (encoded.empty())
    {
        r.error = "encode failed for format " + format;
        return r;
    }

    std::string err;
    if (!atomicWriteBytes(qPath, encoded, &err))
    {
        r.error = err.empty() ? "write failed" : err;
        return r;
    }

    r.ok = true;
    r.method = ImageRotateMethod::PixelRewrite;
    r.width = rotated.width;
    r.height = rotated.height;
    return r;
}

} // namespace mviewer::core
