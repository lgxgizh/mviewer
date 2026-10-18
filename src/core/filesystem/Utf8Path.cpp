#include "core/filesystem/Utf8Path.h"

#include <QByteArrayView>
#include <QDir>
#include <QString>

#include <limits>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace mviewer::core
{

std::filesystem::path pathFromUtf8(const std::string &path)
{
#ifdef _WIN32
    if (path.empty())
        return {};
    if (path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("UTF-8 path is too long");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                                           static_cast<int>(path.size()), nullptr, 0);
    if (length <= 0)
        throw std::runtime_error("invalid UTF-8 filesystem path");
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                            static_cast<int>(path.size()), wide.data(), length) != length)
        throw std::runtime_error("UTF-8 filesystem path conversion failed");

    // On Windows, Win32 file APIs and std::filesystem require UNC network paths
    // (e.g. "//server/share" or "\\server\share") to use backslashes.
    if (wide.size() >= 2 && (wide[0] == L'/' || wide[0] == L'\\') &&
        (wide[1] == L'/' || wide[1] == L'\\'))
    {
        for (wchar_t &wc : wide)
        {
            if (wc == L'/')
                wc = L'\\';
        }
    }
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(path);
#endif
}

std::string pathToUtf8(const std::filesystem::path &path)
{
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty())
        return {};
    if (wide.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("filesystem path is too long");
    const int length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                            static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        throw std::runtime_error("filesystem path is not valid Unicode");
    std::string utf8(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                            static_cast<int>(wide.size()), utf8.data(), length, nullptr,
                            nullptr) != length)
        throw std::runtime_error("filesystem path conversion failed");
    return utf8;
#else
    return path.string();
#endif
}

std::string normalizeKey(std::string path)
{
    while (!path.empty() && (path.back() == '\r' || path.back() == '\n'))
        path.pop_back();
    if (path.empty())
        return {};

    // QString::fromUtf8 takes a signed size. A path that cannot be represented
    // is not a usable store key; keep a slash-folded copy rather than truncate.
    if (path.size() > static_cast<size_t>((std::numeric_limits<qsizetype>::max)()))
    {
        for (char &ch : path)
        {
            if (ch == '\\')
                ch = '/';
        }
        return path;
    }

    QString q = QString::fromUtf8(QByteArrayView(path.data(), static_cast<qsizetype>(path.size())));
    // fromNativeSeparators folds '\' only on Windows (and strips the \\?\
    // prefix). Store keys use '/' on every platform, so fold first as well.
    q.replace(QLatin1Char('\\'), QLatin1Char('/'));
    q = QDir::fromNativeSeparators(q);
    q = QDir::cleanPath(q);
#ifdef _WIN32
    q = q.toLower();
#endif
    const QByteArray utf8 = q.toUtf8();
    if (utf8.isEmpty())
        return {};
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

} // namespace mviewer::core
