#include "core/batch/BatchRenameInPlace.h"

#include "core/batch/BatchRename.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QUuid>

namespace mviewer::core
{
namespace
{

QString fromUtf8(const std::string &text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string toUtf8(const QString &text)
{
    return text.toUtf8().toStdString();
}

// Identity of a path on the file system: Windows names are case-insensitive.
QString pathKey(const QString &path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return clean.toCaseFolded();
#else
    return clean;
#endif
}

struct PlannedMove
{
    size_t index = 0;
    QString source;
    QString target;
    QString temp;
    bool external = false; // target is an unrelated existing file
};

bool validateEntry(const InPlaceRenameEntry &entry, InPlaceRenameItemResult &item, QString &target)
{
    const QString source = fromUtf8(entry.sourcePath);
    const QFileInfo info(source);
    target = QDir(info.absolutePath()).filePath(fromUtf8(entry.targetName));
    item.sourcePath = entry.sourcePath;
    item.targetPath = toUtf8(target);
    if (const std::string error = fileNameError(entry.targetName); !error.empty())
    {
        item.errorMessage = error;
        return false;
    }
    if (!info.isFile())
    {
        item.errorMessage = "Source file does not exist: " + entry.sourcePath;
        return false;
    }
    return true;
}

QString uniqueTempPath(const QString &source, size_t index)
{
    const QString dir = QFileInfo(source).absolutePath();
    const QString token = QUuid::createUuid().toString(QUuid::Id128).left(12);
    return QDir(dir).filePath(QStringLiteral(".mviewer-rename-%1-%2.tmp").arg(token).arg(index));
}

void restoreSource(const PlannedMove &move, InPlaceRenameItemResult &item)
{
    if (QFile::rename(move.temp, move.source))
        return;
    item.errorMessage += " (the file was left as " + toUtf8(move.temp) + ")";
}

void tallyReport(InPlaceRenameReport &report)
{
    for (const InPlaceRenameItemResult &item : report.items)
    {
        report.renamed += item.outcome == InPlaceRenameOutcome::Renamed ? 1 : 0;
        report.unchanged += item.outcome == InPlaceRenameOutcome::Unchanged ? 1 : 0;
        report.failed += item.outcome == InPlaceRenameOutcome::Failed ? 1 : 0;
        report.replaced += item.replacedExisting ? 1 : 0;
    }
}

} // namespace

bool moveFileToSystemTrash(const std::string &path)
{
    return QFile::moveToTrash(fromUtf8(path));
}

std::vector<std::string>
inPlaceRenameExternalCollisions(const std::vector<InPlaceRenameEntry> &plan)
{
    QSet<QString> sources;
    for (const InPlaceRenameEntry &entry : plan)
        sources.insert(pathKey(fromUtf8(entry.sourcePath)));
    std::vector<std::string> collisions;
    for (const InPlaceRenameEntry &entry : plan)
    {
        const QString source = fromUtf8(entry.sourcePath);
        const QString target =
            QDir(QFileInfo(source).absolutePath()).filePath(fromUtf8(entry.targetName));
        if (!sources.contains(pathKey(target)) && QFileInfo::exists(target))
            collisions.push_back(toUtf8(target));
    }
    return collisions;
}

InPlaceRenameReport renameInPlace(const std::vector<InPlaceRenameEntry> &plan,
                                  const TrashFunction &trash)
{
    InPlaceRenameReport report;
    report.items.resize(plan.size());
    std::vector<PlannedMove> moves;
    QSet<QString> planSources;
    QSet<QString> movingSources;
    QSet<QString> targets;

    // Pass 1: validate, drop no-op entries and duplicate targets.
    for (size_t i = 0; i < plan.size(); ++i)
    {
        InPlaceRenameItemResult &item = report.items[i];
        planSources.insert(pathKey(fromUtf8(plan[i].sourcePath)));
        QString target;
        if (!validateEntry(plan[i], item, target))
            continue;
        const QString source = QFileInfo(fromUtf8(plan[i].sourcePath)).absoluteFilePath();
        const QString targetKey = pathKey(target);
        if (targets.contains(targetKey))
        {
            item.errorMessage = "Duplicate target file name: " + plan[i].targetName;
            continue;
        }
        targets.insert(targetKey);
        if (QFileInfo(source).fileName() == fromUtf8(plan[i].targetName))
        {
            item.outcome = InPlaceRenameOutcome::Unchanged;
            continue;
        }
        movingSources.insert(pathKey(source));
        moves.push_back({i, source, target, {}, false});
    }

    // Pass 2: classify targets that already exist. A batch file that does not
    // move is never replaced; an unrelated file is replaced via the trash.
    std::vector<PlannedMove> ready;
    for (PlannedMove &move : moves)
    {
        const QString targetKey = pathKey(move.target);
        if (!movingSources.contains(targetKey) && QFileInfo::exists(move.target))
        {
            if (planSources.contains(targetKey))
            {
                report.items[move.index].errorMessage =
                    "Target name is used by another file in the batch: " + toUtf8(move.target);
                continue;
            }
            move.external = true;
        }
        ready.push_back(move);
    }

    // Pass 3: every mover takes a unique temporary name first.
    std::vector<PlannedMove> parked;
    for (PlannedMove &move : ready)
    {
        move.temp = uniqueTempPath(move.source, move.index);
        if (!QFile::rename(move.source, move.temp))
        {
            report.items[move.index].errorMessage = "Cannot rename " + toUtf8(move.source);
            continue;
        }
        parked.push_back(move);
    }

    // Pass 4: replace unrelated files, then move temporaries onto targets.
    for (const PlannedMove &move : parked)
    {
        InPlaceRenameItemResult &item = report.items[move.index];
        if (move.external && QFileInfo::exists(move.target))
        {
            if (!trash || !trash(toUtf8(move.target)))
            {
                item.errorMessage =
                    "Cannot move the existing file to the Recycle Bin: " + toUtf8(move.target);
                restoreSource(move, item);
                continue;
            }
            item.replacedExisting = true;
        }
        if (!QFile::rename(move.temp, move.target))
        {
            item.errorMessage = "Cannot rename to " + toUtf8(move.target);
            restoreSource(move, item);
            continue;
        }
        item.outcome = InPlaceRenameOutcome::Renamed;
    }

    tallyReport(report);
    return report;
}

} // namespace mviewer::core
