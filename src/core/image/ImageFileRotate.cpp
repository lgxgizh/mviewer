#include "core/image/ImageFileRotate.h"

#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>
#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <vector>

namespace mviewer::core
{
namespace
{

constexpr int kWriteAttempts = 3;
constexpr auto kRetryDelay = std::chrono::milliseconds(80);

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

bool looksLikeSharing(const QString &lower)
{
    return lower.contains(QLatin1String("sharing")) ||
           lower.contains(QLatin1String("being used")) ||
           lower.contains(QLatin1String("used by another")) ||
           lower.contains(QString::fromUtf8("正在使用")) ||
           lower.contains(QString::fromUtf8("已锁定"));
}

bool looksLikeAccessDenied(const QString &lower)
{
    return lower.contains(QLatin1String("denied")) || lower.contains(QLatin1String("permission")) ||
           lower.contains(QString::fromUtf8("拒绝访问"));
}

ImageRotateError classifySaveError(const QSaveFile &file, bool duringCommit)
{
    const QString lower = file.errorString().toLower();
    if (looksLikeSharing(lower))
        return ImageRotateError::SharingViolation;
    if (looksLikeAccessDenied(lower) || file.error() == QFileDevice::PermissionsError)
        return duringCommit ? ImageRotateError::SharingViolation : ImageRotateError::AccessDenied;
#ifdef Q_OS_WIN
    const DWORD native = GetLastError();
    if (native == ERROR_SHARING_VIOLATION || native == ERROR_LOCK_VIOLATION)
        return ImageRotateError::SharingViolation;
    if (native == ERROR_ACCESS_DENIED)
        return duringCommit ? ImageRotateError::SharingViolation : ImageRotateError::AccessDenied;
#endif
    return ImageRotateError::WriteFailed;
}

bool isRetryableWrite(ImageRotateError code)
{
    return code == ImageRotateError::AccessDenied || code == ImageRotateError::SharingViolation;
}

bool atomicWriteBytes(const QString &path, const std::vector<uint8_t> &bytes, std::string *err,
                      ImageRotateError *code)
{
    std::string lastErr = "write failed";
    ImageRotateError lastCode = ImageRotateError::WriteFailed;
    for (int attempt = 0; attempt < kWriteAttempts; ++attempt)
    {
        if (attempt > 0)
            std::this_thread::sleep_for(kRetryDelay);

        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly))
        {
            lastErr = out.errorString().toStdString();
            lastCode = classifySaveError(out, false);
            if (!isRetryableWrite(lastCode))
                break;
            continue;
        }
        const qint64 n = static_cast<qint64>(bytes.size());
        if (out.write(reinterpret_cast<const char *>(bytes.data()), n) != n)
        {
            out.cancelWriting();
            lastErr = "short write";
            lastCode = ImageRotateError::ShortWrite;
            break;
        }
        if (!out.commit())
        {
            lastErr = out.errorString().toStdString();
            lastCode = classifySaveError(out, true);
            if (!isRetryableWrite(lastCode))
                break;
            continue;
        }
        return true;
    }
    if (err)
        *err = lastErr.empty() ? "write failed" : lastErr;
    if (code)
        *code = lastCode;
    return false;
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

ImageFileRotateResult failResult(std::string error, ImageRotateError code)
{
    ImageFileRotateResult r;
    r.error = std::move(error);
    r.errorCode = code;
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
        return failResult("empty path", ImageRotateError::EmptyPath);

    const int norm = normalizeDegreesCw(degreesCw);
    if (norm < 0)
        return failResult("angle must be a multiple of 90", ImageRotateError::InvalidAngle);
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
        return failResult("file not found", ImageRotateError::NotFound);
    if (!fi.isWritable())
        return failResult("file not writable", ImageRotateError::NotWritable);

    const std::string suffix = fileSuffixOf(utf8Path);
    if (!isWritableRotateFormat(suffix))
    {
        const std::string label = suffix.empty() ? std::string("(none)") : suffix;
        return failResult("unsupported format for rotate: " + label,
                          ImageRotateError::UnsupportedFormat);
    }

    QImage original;
    std::string readerFormat;
    std::string err;
    if (!loadOrientedImage(qPath, &original, &readerFormat, &err))
        return failResult(err, ImageRotateError::ReadFailed);

    const ImageData src = mvcore::fromQImage(original);
    if (src.isNull())
        return failResult("pixel convert failed", ImageRotateError::ConvertFailed);

    const ImageData rotated = rotatePixelsExact(src, norm);
    if (rotated.isNull())
        return failResult("rotate failed", ImageRotateError::RotateFailed);

    const std::string format = resolveEncodeFormat(readerFormat, suffix);
    std::vector<uint8_t> encoded;
    if (!encodeRotated(rotated, format, &encoded, &err))
        return failResult(err, ImageRotateError::EncodeFailed);

    ImageRotateError writeCode = ImageRotateError::WriteFailed;
    if (!atomicWriteBytes(qPath, encoded, &err, &writeCode))
        return failResult(err.empty() ? "write failed" : err, writeCode);

    ImageFileRotateResult r;
    r.ok = true;
    r.method = ImageRotateMethod::PixelRewrite;
    r.width = rotated.width;
    r.height = rotated.height;
    return r;
}

} // namespace mviewer::core
