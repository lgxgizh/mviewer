#include "core/plugin/PluginPathPolicy.h"

#include "core/filesystem/Utf8Path.h"

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <filesystem>
#include <system_error>

namespace mviewer::core
{
namespace
{

std::string pluginExtension()
{
#ifdef _WIN32
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

bool samePath(const std::filesystem::path &left, const std::filesystem::path &right)
{
    return left == right;
}

} // namespace

bool assessPluginDirectory(const std::string &directoryUtf8, std::string &canonicalUtf8,
                           std::string &error)
{
    canonicalUtf8.clear();
    error.clear();
    const std::filesystem::path dir = pathFromUtf8(directoryUtf8);
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(dir, ec);
    if (ec || !std::filesystem::is_directory(status))
    {
        error = "plugin directory is missing or is not a real directory";
        return false;
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(dir, ec);
    if (ec)
    {
        error = "plugin directory cannot be canonicalized";
        return false;
    }
    canonicalUtf8 = pathToUtf8(canonical);
    return true;
}

PluginFileAssessment assessPluginFile(const std::string &fileUtf8, const std::string &directoryUtf8)
{
    PluginFileAssessment out;
    const std::filesystem::path file = pathFromUtf8(fileUtf8);
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(file, ec);
    if (ec)
    {
        out.error = "plugin path cannot be stated";
        return out;
    }
    if (std::filesystem::is_symlink(status))
    {
        out.error = "plugin path is a symlink";
        return out;
    }
    // file_type::junction is an MSVC extension. A junction is not a regular
    // file on libstdc++/libc++ either, so the regular-file check below still
    // refuses it when this enumerator does not exist.
#if defined(_MSC_VER)
    if (status.type() == std::filesystem::file_type::junction)
    {
        out.error = "plugin path is a junction";
        return out;
    }
#endif
    if (!std::filesystem::is_regular_file(status))
    {
        out.error = "plugin path is not a regular file";
        return out;
    }

    const std::filesystem::path canonical = std::filesystem::weakly_canonical(file, ec);
    if (ec)
    {
        out.error = "plugin path cannot be canonicalized";
        return out;
    }
    if (!directoryUtf8.empty())
    {
        std::string directoryCanonical;
        std::string directoryError;
        if (!assessPluginDirectory(directoryUtf8, directoryCanonical, directoryError))
        {
            out.error = directoryError;
            return out;
        }
        if (!samePath(canonical.parent_path(), pathFromUtf8(directoryCanonical)))
        {
            out.error = "plugin path escapes the directory";
            return out;
        }
    }

    out.regularFile = true;
    out.canonicalUtf8 = pathToUtf8(canonical);
    return out;
}

std::vector<std::string> scanPluginCandidates(const std::string &dirPath)
{
    std::vector<std::string> candidates;
    std::string canonical;
    std::string error;
    if (!assessPluginDirectory(dirPath, canonical, error))
        return candidates;

    const std::filesystem::path dir = pathFromUtf8(dirPath);
    const std::string ext = pluginExtension();
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::filesystem::path entry = it->path();
        if (pathToUtf8(entry.extension()) != ext)
            continue;
        const PluginFileAssessment assessed = assessPluginFile(pathToUtf8(entry), dirPath);
        if (!assessed.regularFile)
            continue;
        candidates.push_back(pathToUtf8(entry));
    }
    return candidates;
}

std::string pluginFileSha256(const std::string &fileUtf8, std::string &error)
{
    error.clear();
    const PluginFileAssessment assessed = assessPluginFile(fileUtf8, {});
    if (!assessed.regularFile)
    {
        error = assessed.error.empty() ? "plugin path is not a regular file" : assessed.error;
        return {};
    }

    QFile input(QString::fromUtf8(fileUtf8.data(), static_cast<int>(fileUtf8.size())));
    if (!input.open(QIODevice::ReadOnly))
    {
        error = "plugin file cannot be read";
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input))
    {
        error = "plugin file cannot be hashed";
        return {};
    }
    return hash.result().toHex().toStdString();
}

} // namespace mviewer::core
