// Real file-path Session ZIP parser, with per-process private bounded IO.
#include "core/session/SessionPackage.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size)
{
    if (size > 65536)
        return 0;
    static QTemporaryDir directory;
    if (!directory.isValid())
        std::abort();
    const auto input = directory.filePath(QStringLiteral("input.zip"));
    QFile file(input);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(reinterpret_cast<const char *>(data), static_cast<qint64>(size)) !=
            static_cast<qint64>(size))
        std::abort();
    file.close();
    std::string error;
    const auto entries = mviewer::core::readSessionPackage(input.toUtf8().toStdString(), error);
    if (!error.empty() && !entries.empty())
        std::abort(); // Never expose a partially trusted archive.
    if (error.empty())
    {
        const auto output =
            directory.filePath(QStringLiteral("roundtrip.zip")).toUtf8().toStdString();
        if (!mviewer::core::writeSessionPackage(output, entries, error))
            std::abort();
        const auto reread = mviewer::core::readSessionPackage(output, error);
        if (!error.empty() || reread.size() != entries.size())
            std::abort();
        for (size_t i = 0; i < entries.size(); ++i)
            if (reread[i].name != entries[i].name || reread[i].data != entries[i].data)
                std::abort();
    }
    return 0;
}
