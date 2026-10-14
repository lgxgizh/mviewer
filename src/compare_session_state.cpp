#include "compare_session_state.h"

#include <algorithm>

namespace mviewer::ui
{

CompareSessionState::CompareSessionState(QObject *parent) : QObject(parent)
{
}

void CompareSessionState::setComparePaths(const QStringList &paths)
{
    if (m_comparePaths == paths)
        return;
    m_comparePaths = paths;
    if (m_frameIndices.size() != static_cast<size_t>(paths.size()))
    {
        m_frameIndices.assign(static_cast<size_t>(paths.size()), 0);
    }
    emit pathsChanged(m_comparePaths);
}

void CompareSessionState::setFrameIndices(const std::vector<int> &indices)
{
    m_frameIndices = indices;
    if (m_frameIndices.size() < static_cast<size_t>(m_comparePaths.size()))
    {
        m_frameIndices.resize(static_cast<size_t>(m_comparePaths.size()), 0);
    }
}

void CompareSessionState::setNavWindow(int window)
{
    m_navWindow = std::max(1, window);
}

void CompareSessionState::setImagePool(const QStringList &allPaths, const QStringList &currentWindow)
{
    m_allPoolPaths = allPaths;
    if (!currentWindow.isEmpty())
    {
        setComparePaths(currentWindow);
    }
}

void CompareSessionState::setLayoutPreset(int preset)
{
    if (m_layoutPreset == preset)
        return;
    m_layoutPreset = preset;
    emit layoutPresetChanged(m_layoutPreset);
}

void CompareSessionState::setSyncEnabled(bool enabled)
{
    if (m_syncEnabled == enabled)
        return;
    m_syncEnabled = enabled;
    emit syncToggled(m_syncEnabled);
}

void CompareSessionState::setAutoAlignBeforeDiff(bool on)
{
    if (m_autoAlignBeforeDiff == on)
        return;
    m_autoAlignBeforeDiff = on;
    emit autoAlignToggled(m_autoAlignBeforeDiff);
}

void CompareSessionState::setCurrentROI(const mviewer::domain::Selection &roi)
{
    m_currentROI = roi;
    emit roiChanged(m_currentROI);
}

void CompareSessionState::clearROI()
{
    m_currentROI = mviewer::domain::Selection();
    emit roiChanged(m_currentROI);
}

void CompareSessionState::setOverlayMode(mviewer::OverlayMode mode)
{
    if (m_overlayMode == mode)
        return;
    m_overlayMode = mode;
    emit overlayModeChanged(m_overlayMode);
}

mviewer::domain::CompareSession CompareSessionState::toDomainSession() const
{
    mviewer::domain::CompareSession session;
    session.imageIds.reserve(m_comparePaths.size());
    for (const QString &p : m_comparePaths)
    {
        session.imageIds.push_back(p.toStdString());
    }
    session.frameIndices = m_frameIndices;
    session.syncMode =
        m_syncEnabled ? mviewer::domain::SyncMode::All : mviewer::domain::SyncMode::Off;
    session.sharedScale = m_sharedScale;
    session.sharedOffsetX = m_sharedOffset.x();
    session.sharedOffsetY = m_sharedOffset.y();
    session.selection = {m_currentROI.x,
                         m_currentROI.y,
                         m_currentROI.width,
                         m_currentROI.height,
                         !m_currentROI.isEmpty(),
                         m_syncEnabled};
    session.layoutIndex = m_layoutPreset;
    return session;
}

void CompareSessionState::restoreFromDomainSession(const mviewer::domain::CompareSession &session)
{
    QStringList paths;
    paths.reserve(static_cast<int>(session.imageIds.size()));
    for (const std::string &id : session.imageIds)
    {
        paths.append(QString::fromStdString(id));
    }
    setComparePaths(paths);
    setFrameIndices(session.frameIndices);
    setSyncEnabled(session.syncMode != mviewer::domain::SyncMode::Off);
    setSharedScale(session.sharedScale);
    setSharedOffset(QPointF(session.sharedOffsetX, session.sharedOffsetY));
    mviewer::domain::Selection sel;
    sel.x = session.selection.x;
    sel.y = session.selection.y;
    sel.width = session.selection.w;
    sel.height = session.selection.h;
    setCurrentROI(sel);
    setLayoutPreset(session.layoutIndex);
}

} // namespace mviewer::ui
