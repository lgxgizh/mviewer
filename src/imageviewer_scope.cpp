// Line-profile tool and video-scope panel for ImageViewer.
//
// The scopes read the image that is already on screen (the LOD raster in LOD
// mode, otherwise the loaded frame) and never re-decode, so enabling them costs
// nothing until they are actually shown.
#include "imageviewer.h"
#include "imageviewer_p.h"

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
    if (d->profile.mode == on)
        return;
    d->profile.mode = on;
    if (on)
        setAnnotateMode(false); // one drawing tool at a time
    if (!on)
    {
        d->profile.dragging = false;
        d->profile.lineActive = false;
        if (d->scope.widget)
            d->scope.widget->clearProfileLine();
    }
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    syncScopeWidget();
    update();
}

bool ImageViewer::isProfileMode() const
{
    return d->profile.mode;
}

void ImageViewer::setScopesVisible(bool visible)
{
    if (d->scope.visible == visible)
        return;
    d->scope.visible = visible;
    if (visible)
        syncScopeWidget(); // creates the panel on first use
    if (d->scope.widget)
        d->scope.widget->setVisible(visible);
    update();
}

bool ImageViewer::isScopesVisible() const
{
    return d->scope.visible;
}

void ImageViewer::cycleScopeMode()
{
    d->scope.mode = (d->scope.mode + 1) % 3;
    if (d->profile.mode && d->profile.lineActive)
        d->scope.mode = 0; // a committed line keeps the panel on the line profile
    syncScopeWidget();
    update();
}

int ImageViewer::scopeMode() const
{
    return d->scope.mode;
}

void ImageViewer::addProfileScopeContextActions(QMenu &menu, QAction *&profile, QAction *&scopes,
                                                bool hasView)
{
    profile = menu.addAction(tr("Line Profile (L)"));
    profile->setCheckable(true);
    profile->setChecked(d->profile.mode);
    profile->setEnabled(hasView);
    scopes = menu.addAction(tr("Scopes (W)"));
    scopes->setCheckable(true);
    scopes->setChecked(d->scope.visible);
    scopes->setEnabled(hasView);
}

bool ImageViewer::handleProfileScopeContextAction(QAction *chosen, QAction *profile,
                                                  QAction *scopes)
{
    if (chosen == profile)
    {
        setProfileMode(!d->profile.mode);
        return true;
    }
    if (chosen == scopes)
    {
        setScopesVisible(!d->scope.visible);
        return true;
    }
    return false;
}

void ImageViewer::handleProfileMouseEvent(QMouseEvent *event, int phase)
{
    if (phase == 0)
    {
        if (!d->profile.mode || !hasDisplayImage() || m_view.scale <= 0.0)
            return;
        d->profile.dragging = true;
        d->profile.start = d->profile.end = event->pos();
        update();
        return;
    }
    if (phase == 1)
    {
        if (!d->profile.dragging)
            return;
        d->profile.end = event->pos();
        update();
        return;
    }
    // phase == 2: release
    if (!d->profile.dragging)
        return;
    d->profile.dragging = false;
    if (hasDisplayImage() && m_view.scale > 0.0)
    {
        const QLineF line(QPointF((d->profile.start.x() - m_view.offsetX) / m_view.scale,
                                  (d->profile.start.y() - m_view.offsetY) / m_view.scale),
                          QPointF((d->profile.end.x() - m_view.offsetX) / m_view.scale,
                                  (d->profile.end.y() - m_view.offsetY) / m_view.scale));
        if (line.length() >= 2.0)
        {
            d->profile.lineImage = line;
            d->profile.lineActive = true;
            d->scope.mode = 0;
            if (!d->scope.visible)
                d->scope.visible = true;
            syncScopeWidget();
        }
    }
    update();
}

void ImageViewer::syncScopeWidget()
{
    if (!d->scope.visible)
        return;
    if (!d->scope.widget)
    {
        d->scope.widget = new mviewer::ui::ScopeWidget(this);
        d->scope.widget->hide();
    }

    QImage src;
    if (m_lodMode && !m_raster.image.isNull())
        src = m_raster.image;
    else if (m_frame && m_frame->isValid())
        src = mvcore::toQImageFast(m_frame->pixels());
    if (src.isNull())
        return;
    d->scope.widget->setImage(src);

    using Mode = mviewer::ui::ScopeWidget::Mode;
    Mode mode = Mode::LumaWaveform;
    switch (d->scope.mode)
    {
    case 0:
        mode = d->profile.lineActive ? Mode::LineProfile : Mode::LumaWaveform;
        break;
    case 1:
        mode = Mode::RgbParade;
        break;
    default:
        mode = Mode::Vectorscope;
        break;
    }
    d->scope.widget->setMode(mode);
    if (d->profile.lineActive)
        d->scope.widget->setProfileLine(d->profile.lineImage);

    // Bottom-left slot, lifted clear of the frame pill by the shared layout.
    layoutFloatingOverlays();
    d->scope.widget->raise();
}
