#include "workspacemodel.h"

#include <QDir>

WorkspaceModel::WorkspaceModel(QObject *parent) : QObject(parent)
{
}

void WorkspaceModel::setRootPath(const QString &path)
{
    const QString cleaned = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (m_rootPath == cleaned)
        return;
    m_rootPath = cleaned;
    emit rootPathChanged(m_rootPath);
}

void WorkspaceModel::setComparedImages(const QStringList &images)
{
    if (m_comparedImages == images)
        return;
    m_comparedImages = images;
    emit comparedImagesChanged(m_comparedImages);
}

void WorkspaceModel::setCompareSessionJson(const QString &json)
{
    if (m_compareSessionJson == json)
        return;
    m_compareSessionJson = json;
    emit compareSessionJsonChanged(m_compareSessionJson);
}

void WorkspaceModel::setAnalysisVisible(bool visible)
{
    if (m_analysisVisible == visible)
        return;
    m_analysisVisible = visible;
    emit analysisVisibleChanged(m_analysisVisible);
}

void WorkspaceModel::setAnalysisPage(int page)
{
    if (m_analysisPage == page)
        return;
    m_analysisPage = page;
    emit analysisPageChanged(m_analysisPage);
}

void WorkspaceModel::clear()
{
    const bool hadAnything =
        !m_rootPath.isEmpty() || !m_comparedImages.isEmpty() || !m_compareSessionJson.isEmpty();
    m_rootPath.clear();
    m_comparedImages.clear();
    m_compareSessionJson.clear();
    // Keep analysis panel prefs across clear — they are UI chrome, not session
    // content. Emit reset so listeners can drop session-bound state.
    if (hadAnything)
    {
        emit rootPathChanged(m_rootPath);
        emit comparedImagesChanged(m_comparedImages);
        emit compareSessionJsonChanged(m_compareSessionJson);
        emit workspaceReset();
    }
}
