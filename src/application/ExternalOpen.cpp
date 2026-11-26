#include "application/ExternalOpen.h"

#include "core/image/ImageFormats.h"

#include <QDir>
#include <QFileInfo>

namespace mviewer::application
{

namespace
{

QString unsupportedError(const QString &path)
{
    return QStringLiteral("Unsupported open target: %1").arg(path);
}

#ifdef _WIN32
// The device namespace (\\.\ and \\?\ to anything that is not a drive path) is
// not a location a user browses to. Opening it reaches physical drives, named
// pipes and the like, and bypasses the normal path/length rules. Plain UNC
// shares (\\server\share) stay allowed - browsing network folders is supported.
bool isDeviceNamespacePath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.startsWith(QStringLiteral("\\\\.\\")))
        return true;
    if (lower.startsWith(QStringLiteral("\\\\?\\")))
    {
        const QString rest = path.mid(4);
        const bool drivePath =
            rest.size() >= 2 && rest.at(0).isLetter() && rest.at(1) == QLatin1Char(':');
        return !drivePath;
    }
    return false;
}
#endif

} // namespace

QString normalizeExternalOpenPath(const QString &rawPath)
{
    QString path = rawPath;
    if (path.size() >= 2 && path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
        path = path.mid(1, path.size() - 2);
    if (path.isEmpty())
        return {};

    const QFileInfo info(path);
    return QDir::cleanPath(info.absoluteFilePath());
}

ExternalOpenTarget classifyExternalOpenTarget(const QString &rawPath)
{
    ExternalOpenTarget target;
    target.path = normalizeExternalOpenPath(rawPath);
    if (target.path.isEmpty())
    {
        target.error = QStringLiteral("Open target is empty");
        return target;
    }
#ifdef _WIN32
    if (isDeviceNamespacePath(target.path))
    {
        target.path.clear();
        target.error =
            QStringLiteral("Open target is a device namespace path, not a file or folder: %1")
                .arg(rawPath);
        return target;
    }
#endif

    const QFileInfo info(target.path);
    if (!info.exists())
    {
        target.error = QStringLiteral("Open target does not exist: %1").arg(rawPath);
        return target;
    }
    if (info.isDir())
    {
        target.kind = ExternalOpenKind::Directory;
        return target;
    }
    if (!info.isFile())
    {
        target.error =
            QStringLiteral("Open target is not a regular file or directory: %1").arg(rawPath);
        return target;
    }

    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("mvws"))
        target.kind = ExternalOpenKind::Workspace;
    else if (suffix == QStringLiteral("mvproj"))
        target.kind = ExternalOpenKind::Project;
    else if (mviewer::core::ImageFormats::isSupportedPath(target.path.toUtf8().toStdString()))
        target.kind = ExternalOpenKind::Image;
    else
        target.error = unsupportedError(rawPath);
    return target;
}

ExternalOpenPlan planExternalOpen(const QStringList &rawPaths)
{
    ExternalOpenPlan plan;
    if (rawPaths.isEmpty())
    {
        plan.error = QStringLiteral("Nothing to open");
        return plan;
    }

    QList<ExternalOpenTarget> targets;
    targets.reserve(rawPaths.size());
    for (const QString &rawPath : rawPaths)
    {
        ExternalOpenTarget target = classifyExternalOpenTarget(rawPath);
        if (!target.isValid())
        {
            plan.error = target.error;
            return plan;
        }
        targets.append(target);
    }

    if (targets.size() == 1)
    {
        plan.kind = targets.front().kind;
        plan.paths = {targets.front().path};
        return plan;
    }

    for (const ExternalOpenTarget &target : targets)
    {
        if (target.kind != ExternalOpenKind::Image)
        {
            plan.error = QStringLiteral("Multiple open targets must all be supported images");
            return plan;
        }
        plan.paths.append(target.path);
    }
    plan.kind = ExternalOpenKind::Image;
    return plan;
}

} // namespace mviewer::application
