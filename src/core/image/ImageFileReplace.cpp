#include "core/image/ImageFileReplace.h"

#include <QByteArray>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace mviewer::core
{
namespace
{

unsigned long captureLastError()
{
#ifdef Q_OS_WIN
    return GetLastError();
#else
    return 0;
#endif
}

std::string describeIoError(const QFileDevice &file, unsigned long native)
{
    std::string text = file.errorString().toStdString();
    if (text.empty())
        text = "write failed";
    if (native != 0)
        text += " (GetLastError=" + std::to_string(native) + ")";
    return text;
}

// i18n-allow-begin: the Chinese tokens below match error text produced by a
// zh-CN Windows/Qt runtime; they classify OS messages and are never shown.
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
// i18n-allow-end

ImageRotateError classifySaveError(const QString &errorString, QFileDevice::FileError fileError,
                                   bool duringCommit, unsigned long native)
{
#ifdef Q_OS_WIN
    if (native == ERROR_SHARING_VIOLATION || native == ERROR_LOCK_VIOLATION)
        return ImageRotateError::SharingViolation;
    if (native == ERROR_ACCESS_DENIED)
        return duringCommit ? ImageRotateError::SharingViolation : ImageRotateError::AccessDenied;
#else
    (void)native;
#endif
    const QString lower = errorString.toLower();
    if (looksLikeSharing(lower))
        return ImageRotateError::SharingViolation;
    if (looksLikeAccessDenied(lower) || fileError == QFileDevice::PermissionsError)
        return duringCommit ? ImageRotateError::SharingViolation : ImageRotateError::AccessDenied;
    return ImageRotateError::WriteFailed;
}

bool isRetryableWrite(ImageRotateError code)
{
    return code == ImageRotateError::AccessDenied || code == ImageRotateError::SharingViolation;
}

bool writeOpenFile(QFile &file, const std::vector<uint8_t> &bytes, std::string *err)
{
    if (!file.seek(0))
    {
        const unsigned long native = captureLastError();
        if (err)
            *err = "seek failed: " + describeIoError(file, native);
        return false;
    }
    const qint64 n = static_cast<qint64>(bytes.size());
    if (file.write(reinterpret_cast<const char *>(bytes.data()), n) != n)
    {
        const unsigned long native = captureLastError();
        if (err)
            *err = "short write: " + describeIoError(file, native);
        return false;
    }
    if (!file.resize(n))
    {
        const unsigned long native = captureLastError();
        if (err)
            *err = "resize failed: " + describeIoError(file, native);
        return false;
    }
    if (!file.flush())
    {
        const unsigned long native = captureLastError();
        if (err)
            *err = "flush failed: " + describeIoError(file, native);
        return false;
    }
    return true;
}

bool restoreBackup(const QString &path, QFile &openFile, const std::vector<uint8_t> &backup,
                   std::string *err)
{
    if (writeOpenFile(openFile, backup, err))
        return true;
    openFile.close();
    QFile again(path);
    if (!again.open(QIODevice::ReadWrite))
    {
        const unsigned long native = captureLastError();
        if (err)
            *err = describeIoError(again, native);
        return false;
    }
    return writeOpenFile(again, backup, err);
}

FileReplaceResult tryInPlaceReplace(const QString &path, const std::vector<uint8_t> &bytes)
{
    FileReplaceResult result;
    result.code = ImageRotateError::SharingViolation;

    QFile file(path);
    const bool opened = file.open(QIODevice::ReadWrite);
    const unsigned long openNative = captureLastError();
    if (!opened)
    {
        result.error = describeIoError(file, openNative);
        return result;
    }

    const QByteArray old = file.readAll();
    if (file.error() != QFileDevice::NoError && old.isEmpty())
    {
        result.error = "in-place backup read failed: " + file.errorString().toStdString();
        return result;
    }
    const std::vector<uint8_t> backup(old.begin(), old.end());

    std::string writeErr;
    if (writeOpenFile(file, bytes, &writeErr))
    {
        result.ok = true;
        result.code = ImageRotateError::None;
        result.usedInPlaceFallback = true;
        return result;
    }

    std::string restoreErr;
    if (!restoreBackup(path, file, backup, &restoreErr))
        result.error = writeErr + "; restore also failed: " + restoreErr;
    else
        result.error = writeErr;
    return result;
}

FileReplaceResult tryAtomicSave(const QString &path, const std::vector<uint8_t> &bytes,
                                bool *atCommit)
{
    if (atCommit)
        *atCommit = false;

    FileReplaceResult result;
    QSaveFile out(path);
    out.setDirectWriteFallback(true);

    const bool opened = out.open(QIODevice::WriteOnly);
    const unsigned long openNative = captureLastError();
    if (!opened)
    {
        result.code = classifySaveError(out.errorString(), out.error(), false, openNative);
        result.error = describeIoError(out, openNative);
        return result;
    }

    const qint64 n = static_cast<qint64>(bytes.size());
    const qint64 written = out.write(reinterpret_cast<const char *>(bytes.data()), n);
    const unsigned long writeNative = captureLastError();
    if (written != n)
    {
        out.cancelWriting();
        result.code = ImageRotateError::ShortWrite;
        result.error = "short write";
        if (writeNative != 0)
            result.error += " (GetLastError=" + std::to_string(writeNative) + ")";
        return result;
    }

    const bool committed = out.commit();
    const unsigned long commitNative = captureLastError();
    if (!committed)
    {
        if (atCommit)
            *atCommit = true;
        result.code = classifySaveError(out.errorString(), out.error(), true, commitNative);
        result.error = describeIoError(out, commitNative);
        return result;
    }

    result.ok = true;
    result.code = ImageRotateError::None;
    return result;
}

} // namespace

FileReplaceResult replaceFileContents(const std::string &utf8Path,
                                      const std::vector<uint8_t> &bytes)
{
    const QString path = QString::fromStdString(utf8Path);
    static const int kDelaysMs[] = {50, 100, 200, 300, 400, 500, 500};
    const int attempts = 1 + static_cast<int>(sizeof(kDelaysMs) / sizeof(kDelaysMs[0]));

    FileReplaceResult last;
    last.code = ImageRotateError::WriteFailed;
    last.error = "write failed";
    bool lastCommitRetryable = false;

    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        if (attempt > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kDelaysMs[attempt - 1]));
        }

        bool atCommit = false;
        FileReplaceResult once = tryAtomicSave(path, bytes, &atCommit);
        if (once.ok)
            return once;

        last = once;
        lastCommitRetryable = atCommit && isRetryableWrite(once.code);
        if (!isRetryableWrite(once.code))
            break;
    }

    const QFileInfo info(path);
    if (lastCommitRetryable && info.exists() && info.isFile() && info.isWritable())
        return tryInPlaceReplace(path, bytes);
    return last;
}

} // namespace mviewer::core
