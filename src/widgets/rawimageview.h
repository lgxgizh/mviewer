#pragma once

#include "domain/Selection.h"

#include <QImage>
#include <QWidget>

// RawImageView holds a QImage and renders it scaled to fit the widget
// size.  Supports zoom/pan via QPainter transforms in paintEvent:
//   wheel  -> zoom around cursor
//   drag   -> pan
class RawImageView : public QWidget
{
    Q_OBJECT

  public:
    explicit RawImageView(QWidget *parent = nullptr);

    const QImage &image() const
    {
        return m_image;
    }
    void setImage(const QImage &img);
    void clear();

    // Difference/heatmap overlay (compare mode). The workspace computes the overlay
    // from core-layer data and hands it in as a QImage — RawImageView only renders it,
    // it never decodes pixels (see AGENTS.md: no decode in the QWidget layer).
    void setOverlay(const QImage &overlay, double alpha = 0.5);
    void clearOverlay()
    {
        m_overlay = QImage();
        update();
    }

    // 0-based grid index (set by CompareWorkspace so the inspector can label the cell).
    void setCellIndex(int idx)
    {
        m_cellIndex = idx;
    }
    int cellIndex() const
    {
        return m_cellIndex;
    }

    // ROI selection in image coordinates. The widget renders it on top of the
    // fit/pan transform. CompareWorkspace drives this through the SelectionController
    // so a box drawn on one cell is mirrored across the grid.
    void setSelection(const mviewer::domain::Selection &sel)
    {
        m_selection = sel;
        update();
    }
    const mviewer::domain::Selection &selection() const
    {
        return m_selection;
    }
    void clearSelection()
    {
        m_selection = mviewer::domain::Selection{};
        update();
    }

    double scale() const
    {
        return m_scale;
    }
    const QPointF &offset() const
    {
        return m_offset;
    }

    // Apply to transform from image coords -> widget coords
    void setTransform(double scale, const QPointF &offset);
    // Constrain offset so image stays visible
    void clampOffset();

    // M16.1 (cursor-sync crosshair, n/n): a synced crosshair drawn at an
    // image-space position so the same point is marked in every compared cell.
    void setCrosshair(const QPointF &imgPos)
    {
        m_crosshair = imgPos;
        m_crosshairOn = true;
        update();
    }
    void clearCrosshair()
    {
        m_crosshairOn = false;
        update();
    }
    bool hasCrosshair() const
    {
        return m_crosshairOn;
    }

    // M16.1 (focus-lock / reference pin, n/1): mark this cell as the locked
    // reference. The workspace highlights it and uses it as the diff/inspector
    // base when comparing n images against 1 reference.
    void setFocused(bool on)
    {
        m_focused = on;
        update();
    }
    bool isFocused() const
    {
        return m_focused;
    }

  signals:
    void scaleChanged(double scale);
    // Emitted on hover with the image-space pixel under the cursor (RGB + validity).
    // Mirrors ImageViewer::pixelInfo so the compare grid feeds the same inspector.
    void pixelInfo(int x, int y, int r, int g, int b, bool valid);
    // Emitted when the user finishes drawing a selection box (image coords).
    void selectionChanged(const mviewer::domain::Selection &sel);
    // M16.1: emitted on hover with the image-space cursor position so the
    // workspace can mirror a synced crosshair across all cells. A negative x
    // signals "cursor left the cell" (clear the crosshair).
    void crosshairMoved(const QPointF &imgPos);
    // M16.1: double-click toggles this cell as the locked reference (focus).
    void focusRequested(int cellIndex);

  public slots:
    void zoom(double factor, const QPointF &anchor = {});
    void resetFit();

  protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    void computeFit();
    void drawCrosshair(QPainter &p, double cx, double cy, double dw, double dh) const;

    QImage m_image;
    double m_scale = 1.0;
    double m_fitScale = 1.0;
    QPointF m_offset;
    bool m_dragging = false;
    QPoint m_lastMouse;
    int m_cellIndex = -1;
    mviewer::domain::Selection m_selection;
    bool m_selecting = false;
    QPointF m_selectStart;
    QImage m_overlay;
    double m_overlayAlpha = 0.5;

    // M16.1 sync crosshair state (image-space position)
    bool m_crosshairOn = false;
    QPointF m_crosshair;
    // M16.1 focus-lock (reference) flag
    bool m_focused = false;

    QPointF widgetToImage(const QPoint &pos) const;
};
