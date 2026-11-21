// Line-profile tool and video-scope panel for ImageViewer.
//
// The scopes read the image that is already on screen (the LOD raster in LOD
// mode, otherwise the loaded frame) and never re-decode, so enabling them costs
// nothing until they are actually shown.
#include "imageviewer.h"

#include "core/image/QtConvert.h"
#include "widgets/scopewidget.h"

#include <QLineF>
#include <QMenu>
#include <QMouseEvent>

bool ImageViewer::fullRawDecodeActive() const
{
    return m_fullRawLoading;
}

bool ImageViewer::isPixelGridEnabled() const
{
    return m_pixelGridEnabled;
}

QStringList ImageViewer::browseSequence() const
{
    return m_fileList;
}

int ImageViewer::browseIndex() const
{
    return m_currentIndex;
}

QString ImageViewer::currentPath() const
{
    return m_currentPath;
}

QImage ImageViewer::displayRaster() const
{
    return m_raster.image;
}

QImage ImageViewer::provisionalImageForTest() const
{
    return m_provisionalImage;
}

const mviewer::core::DisplayColorContext &ImageViewer::displayColorContext() const
{
    return m_displayColorTarget;
}

bool ImageViewer::isLodDisplay() const
{
    return m_lodMode && !m_raster.image.isNull();
}

bool ImageViewer::isFitMode() const
{
    return m_fitMode;
}

bool ImageViewer::isLockZoom() const
{
    return m_lockZoom;
}

void ImageViewer::setProfileMode(bool on)
{
    if (m_profileMode == on)
        return;
    m_profileMode = on;
    if (on)
        setAnnotateMode(false); // one drawing tool at a time
    if (!on)
    {
        m_profiling = false;
        m_profileLineActive = false;
        if (m_scopeWidget)
            m_scopeWidget->clearProfileLine();
    }
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    syncScopeWidget();
    update();
}

bool ImageViewer::isProfileMode() const
{
    return m_profileMode;
}

void ImageViewer::setScopesVisible(bool visible)
{
    if (m_scopesVisible == visible)
        return;
    m_scopesVisible = visible;
    if (visible)
        syncScopeWidget(); // creates the panel on first use
    if (m_scopeWidget)
        m_scopeWidget->setVisible(visible);
    update();
}

bool ImageViewer::isScopesVisible() const
{
    return m_scopesVisible;
}

void ImageViewer::cycleScopeMode()
{
    m_scopeMode = (m_scopeMode + 1) % 3;
    if (m_profileMode && m_profileLineActive)
        m_scopeMode = 0; // a committed line keeps the panel on the line profile
    syncScopeWidget();
    update();
}

int ImageViewer::scopeMode() const
{
    return m_scopeMode;
}

void ImageViewer::addProfileScopeContextActions(QMenu &menu, QAction *&profile, QAction *&scopes,
                                                bool hasView)
{
    profile = menu.addAction(tr("Line Profile (L)"));
    profile->setCheckable(true);
    profile->setChecked(m_profileMode);
    profile->setEnabled(hasView);
    scopes = menu.addAction(tr("Scopes (W)"));
    scopes->setCheckable(true);
    scopes->setChecked(m_scopesVisible);
    scopes->setEnabled(hasView);
}

bool ImageViewer::handleProfileScopeContextAction(QAction *chosen, QAction *profile,
                                                  QAction *scopes)
{
    if (chosen == profile)
    {
        setProfileMode(!m_profileMode);
        return true;
    }
    if (chosen == scopes)
    {
        setScopesVisible(!m_scopesVisible);
        return true;
    }
    return false;
}

void ImageViewer::handleProfileMouseEvent(QMouseEvent *event, int phase)
{
    if (phase == 0)
    {
        if (!m_profileMode || !hasDisplayImage() || m_view.scale <= 0.0)
            return;
        m_profiling = true;
        m_profStart = m_profEnd = event->pos();
        update();
        return;
    }
    if (phase == 1)
    {
        if (!m_profiling)
            return;
        m_profEnd = event->pos();
        update();
        return;
    }
    // phase == 2: release
    if (!m_profiling)
        return;
    m_profiling = false;
    if (hasDisplayImage() && m_view.scale > 0.0)
    {
        const QLineF line(QPointF((m_profStart.x() - m_view.offsetX) / m_view.scale,
                                  (m_profStart.y() - m_view.offsetY) / m_view.scale),
                          QPointF((m_profEnd.x() - m_view.offsetX) / m_view.scale,
                                  (m_profEnd.y() - m_view.offsetY) / m_view.scale));
        if (line.length() >= 2.0)
        {
            m_profileLineImage = line;
            m_profileLineActive = true;
            m_scopeMode = 0;
            if (!m_scopesVisible)
                m_scopesVisible = true;
            syncScopeWidget();
        }
    }
    update();
}

void ImageViewer::syncScopeWidget()
{
    if (!m_scopesVisible)
        return;
    if (!m_scopeWidget)
    {
        m_scopeWidget = new mviewer::ui::ScopeWidget(this);
        m_scopeWidget->hide();
    }

    QImage src;
    if (m_lodMode && !m_raster.image.isNull())
        src = m_raster.image;
    else if (m_frame && m_frame->isValid())
        src = mvcore::toQImageFast(m_frame->pixels());
    if (src.isNull())
        return;
    m_scopeWidget->setImage(src);

    using Mode = mviewer::ui::ScopeWidget::Mode;
    Mode mode = Mode::LumaWaveform;
    switch (m_scopeMode)
    {
    case 0:
        mode = m_profileLineActive ? Mode::LineProfile : Mode::LumaWaveform;
        break;
    case 1:
        mode = Mode::RgbParade;
        break;
    default:
        mode = Mode::Vectorscope;
        break;
    }
    m_scopeWidget->setMode(mode);
    if (m_profileLineActive)
        m_scopeWidget->setProfileLine(m_profileLineImage);

    // Bottom-left slot, lifted clear of the frame pill by the shared layout.
    layoutFloatingOverlays();
    m_scopeWidget->raise();
}
