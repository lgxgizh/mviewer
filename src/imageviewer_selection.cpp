// Viewer ROI selection: drag commit, the committed image-space rect, its
// widget projection, and Esc handling. Split out of imageviewer_contextmenu.cpp
// to stay under the file cap.
#include "imageviewer.h"

#include <QRect>
#include <cmath>

bool ImageViewer::hasRealSelection() const
{
    // QRect's inclusive edge makes a click 1×1, and a couple of pixels of
    // jitter is still not a drag. A committed ROI is an image rect: zoom-out
    // can shrink its widget size below that threshold, so the stored rect is
    // the hit test once the drag has ended. A real drag that missed the image
    // keeps its widget points so the first Esc still clears it.
    if (!m_selecting && !m_selectionImage.isEmpty())
        return true;
    const QRect region = QRect(m_selStart, m_selEnd).normalized();
    return region.width() > 5 && region.height() > 5;
}

bool ImageViewer::clearRealSelectionForEscape()
{
    if (!hasRealSelection())
    {
        if (m_selecting || m_selStart != m_selEnd || !m_lockedScreenRoi.isEmpty())
        {
            m_selecting = false;
            m_selStart = m_selEnd = QPoint();
            m_lockedScreenRoi = QRect();
            update();
        }
        return false;
    }
    m_selecting = false;
    m_selStart = m_selEnd = QPoint();
    m_selectionImage = QRect();
    m_lockedScreenRoi = QRect();
    if (m_selectMode)
        setSelectMode(false);
    else
        update();
    return true;
}

QRect ImageViewer::selectionWidgetRect() const
{
    if (m_selecting || m_selectionImage.isEmpty())
    {
        const QRect drag = QRect(m_selStart, m_selEnd).normalized();
        return (m_selecting || (drag.width() > 5 && drag.height() > 5)) ? drag : QRect();
    }
    if (!(m_view.scale > 0.0))
        return {};
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(m_selectionImage.x(), m_selectionImage.y(), m_selectionImage.width(),
                             m_selectionImage.height(), sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return {};
    return QRect(sx, sy, sw, sh);
}

QRect ImageViewer::imageRectFromScreen(const QRect &screen) const
{
    if (!(m_view.scale > 0.0) || screen.isEmpty())
        return {};
    const double scale = m_view.scale;
    return QRect(static_cast<int>(std::floor((screen.x() - m_view.offsetX) / scale)),
                 static_cast<int>(std::floor((screen.y() - m_view.offsetY) / scale)),
                 static_cast<int>(std::round(screen.width() / scale)),
                 static_cast<int>(std::round(screen.height() / scale)))
        .normalized();
}

QRect ImageViewer::clampedImageSelection(const QRect &imageRect) const
{
    const QSize bounds = displaySize();
    if (bounds.isEmpty() || imageRect.isEmpty())
        return {};
    return imageRect.normalized().intersected(QRect(0, 0, bounds.width(), bounds.height()));
}

void ImageViewer::publishImageSelection(const QRect &imageRect)
{
    const QRect valid = clampedImageSelection(imageRect);
    if (!valid.isEmpty())
    {
        m_selectionImage = valid;
        scheduleRoiStats(valid);
        emit selectionChanged(valid);
    }
    else
    {
        m_selectionImage = QRect();
        cancelRoiStats();
        emit selectionChanged(QRect());
    }
    update();
}

void ImageViewer::commitSelectionDrag()
{
    // Capture the drag before clearing it. A tiny release is not a new ROI
    // and must not emit; the press already dropped the previous image rect.
    const QRect widget = QRect(m_selStart, m_selEnd).normalized();
    const bool real = widget.width() > 5 && widget.height() > 5;
    m_selecting = false;
    m_lockedScreenRoi = QRect();
    if (!real)
    {
        m_selStart = m_selEnd = QPoint();
        update();
        return;
    }
    const QRect image = clampedImageSelection(imageRectFromScreen(widget));
    if (image.isEmpty())
    {
        // The drag missed the image (or the view is not laid out yet). Keep
        // the drawn box so Esc clears it first, and fall back to whole-image
        // stats.
        m_selectionImage = QRect();
        cancelRoiStats();
        emit selectionChanged(QRect());
        update();
        return;
    }
    m_selStart = m_selEnd = QPoint();
    publishImageSelection(image);
}

QRect ImageViewer::committedSelectionForTest() const
{
    return m_selectionImage;
}

QRect ImageViewer::selectionScreenRectForTest() const
{
    return selectionWidgetRect();
}

QRect ImageViewer::provisionalScreenRect() const
{
    if (m_provisionalSourceSize.width() <= 0 || m_provisionalSourceSize.height() <= 0)
        return {};
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    m_view.imageRectToScreen(0, 0, m_provisionalSourceSize.width(),
                             m_provisionalSourceSize.height(), sx, sy, sw, sh);
    return QRect(sx, sy, sw, sh);
}
