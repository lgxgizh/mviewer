#include "exportmessages.h"

#include <QLatin1String>
#include <QString>

namespace mviewer::ui
{
namespace
{

struct ExportMessagePrefix
{
    const char *english;
    const char *translated;
};

// Longer prefixes precede shorter ones. A prefix that ends in ": " already
// includes that separator in the Chinese text; the remainder is appended raw.
constexpr ExportMessagePrefix kExportMessagePrefixes[] = {
    {"duplicate destination conflict", "Duplicate destination path conflict"},
    {"source/destination conflict", "Source file conflicts with target file"},
    {"no source image decoded", "No source image decoded successfully"},
    {"explicit destination requires one source",
     "Explicit destination requires a single source file"},
    {"cannot create output directory: ", "Cannot create output directory: "},
    {"source directory enumeration failed: ", "Source directory enumeration failed: "},
    {"contact sheet exceeds the compositing budget (",
     "Contact sheet exceeds the compositing budget ("},
    {"staging memory budget exceeded", "Staging memory budget exceeded"},
    {"clipboard source decode failed", "Clipboard source decode failed"},
    {"invalid path encoding: ", "Invalid path encoding: "},
    {"output generation failed", "Output generation failed"},
    {"output commit failed: ", "Output commit failed: "},
    {"clipboard image ready", "Clipboard image ready"},
    {"no output directory", "No output directory"},
    {"unsupported format: ", "Unsupported format: "},
    {"report write failed", "Report write failed"},
    {"cancelled after ", "Cancelled after "},
    {"no sources", "No sources"},
};

QString doneSummary(const mviewer::exportjob::ExportJobResult &result, const QString &message)
{
    QString text = QStringLiteral("Export complete: %1 / %2").arg(result.done).arg(result.total);
    if (result.failed > 0)
        text += QStringLiteral(", %1 failed").arg(result.failed);
    const qsizetype colon = message.indexOf(QStringLiteral(": "));
    if (colon >= 0)
        text += message.mid(colon);
    return text;
}

QString translateKnown(const QString &message)
{
    for (const ExportMessagePrefix &row : kExportMessagePrefixes)
    {
        const QLatin1String prefix(row.english);
        if (!message.startsWith(prefix))
            continue;
        QString text = QString::fromUtf8(row.translated);
        if (prefix.size() < message.size())
            text += message.mid(prefix.size());
        return text;
    }
    return message;
}

} // namespace

QString exportResultMessageZh(const mviewer::exportjob::ExportJobResult &result)
{
    const QString message = QString::fromStdString(result.message);
    if (message.startsWith(QLatin1String("done ")))
        return doneSummary(result, message);
    return translateKnown(message);
}

} // namespace mviewer::ui
