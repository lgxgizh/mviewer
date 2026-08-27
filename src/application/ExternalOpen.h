#pragma once

#include <QString>
#include <QStringList>

namespace mviewer::application
{

// The one contract shared by command-line launch, shell associations, and
// drag-and-drop. Keep this at the application boundary so UI entry points do
// not each grow their own extension and mixed-selection rules.
enum class ExternalOpenKind
{
    Invalid,
    Image,
    Directory,
    Workspace,
    Project,
};

struct ExternalOpenTarget
{
    ExternalOpenKind kind = ExternalOpenKind::Invalid;
    QString path;
    QString error;

    bool isValid() const { return kind != ExternalOpenKind::Invalid; }
};

struct ExternalOpenPlan
{
    ExternalOpenKind kind = ExternalOpenKind::Invalid;
    QStringList paths;
    QString error;

    bool isValid() const { return kind != ExternalOpenKind::Invalid; }
    bool isCompare() const { return kind == ExternalOpenKind::Image && paths.size() >= 2; }
};

// Normalize an argv/drop path without changing its user-visible target. Shell
// quoting is normally removed by the OS, but accepting one pair of quotes
// makes the contract deterministic for programmatic callers and tests.
QString normalizeExternalOpenPath(const QString &rawPath);

ExternalOpenTarget classifyExternalOpenTarget(const QString &rawPath);

// A multi-target open is valid only for two or more existing supported images.
// Directories, workspaces, projects, unsupported files, and missing paths are
// never silently ignored or partially opened.
ExternalOpenPlan planExternalOpen(const QStringList &rawPaths);

} // namespace mviewer::application
