// Switching-photo stand-in. Keep region geometry and device-pixel identity.
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"
#include "imageviewer.h"
#include "imageviewer_transitionpaint.h"
#include <QPainter>

void ImageViewer::prepareTransitionImage(const QString &path, bool keepProvisional)
{
    if (path.isEmpty() || keepProvisional)
    {
        m_transitionImage = {};
        m_transitionSourceSize = {};
        m_transitionSourceRect = {};
    }
    else if (m_lodMode && !m_raster.image.isNull())
    {
        m_transitionImage = m_raster.image;
        m_transitionSourceSize = m_raster.sourceSize;
        m_transitionSourceRect = m_raster.sourceRect;
    }
    else if (m_frame && m_frame->isValid())
    {
        m_transitionImage = ownedTransitionSnapshot(mvcore::toQImageFast(m_frame->pixels()));
        m_transitionSourceSize = QSize(m_frame->width(), m_frame->height());
        m_transitionSourceRect = QRect(QPoint(), m_transitionSourceSize);
    }
    else if (!m_provisionalImage.isNull())
    {
        m_transitionImage = m_provisionalImage;
        m_transitionSourceSize = m_provisionalSourceSize;
        if (m_transitionSourceSize.isEmpty())
            m_transitionSourceSize = m_provisionalImage.size();
        m_transitionSourceRect = QRect(QPoint(), m_transitionSourceSize);
    }
}

QImage ImageViewer::ownedTransitionSnapshot(const QImage &view) const
{
    if (view.isNull())
        return {};
    // Own only a window-sized snapshot rather than pin a full decode buffer.
    const int edge = std::max(64, std::max(width(), height()));
    if (view.width() <= edge && view.height() <= edge)
        return view.copy();
    return view.scaled(edge, edge, Qt::KeepAspectRatio, Qt::FastTransformation);
}

void ImageViewer::drawTransition(QPainter &painter) const
{
    if (m_transitionImage.isNull())
        return;
    const QRect source = m_transitionSourceRect.isEmpty() ? QRect(QPoint(), m_transitionSourceSize)
                                                          : m_transitionSourceRect;
    int sx = 0, sy = 0, sw = 0, sh = 0;
    m_view.imageRectToScreen(source.x(), source.y(), source.width(), source.height(), sx, sy, sw,
                             sh);
    if (sw <= 0 || sh <= 0)
        return;
    const QRect target(sx, sy, sw, sh);
    const QRect visible = target.intersected(rect());
    if (visible.isEmpty())
        return;
    const double scaleX = static_cast<double>(m_transitionImage.width()) / sw;
    const double scaleY = static_cast<double>(m_transitionImage.height()) / sh;
    const QRectF crop((visible.x() - sx) * scaleX, (visible.y() - sy) * scaleY,
                      visible.width() * scaleX, visible.height() * scaleY);
    painter.save();
    // Sharp identity rasters need only a blit, including panned/clipped views.
    // Resize/DPR races and genuinely scaled snapshots still use smooth sampling.
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                          transitionRasterNeedsSmoothing(m_transitionImage.size(), QSize(sw, sh),
                                                         devicePixelRatioF()));
    painter.drawImage(visible, m_transitionImage, crop);
    painter.restore();
}
