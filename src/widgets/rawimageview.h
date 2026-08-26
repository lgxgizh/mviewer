#pragma once

#include "domain/Selection.h"

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QWidget>

#include <cstdint>

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
    // The raster currently presented by the view. During momentary Compare
    // this is B's display raster while image() remains A's stable source.
    const QImage &displayImage() const
    {
        return m_transientImage.isNull() ? m_image : m_transientImage;
    }
    // H3: expose the diff/heatmap overlay so the workspace can re-draw it when
    // rendering split/swipe/overlay modes (which hide the cell widgets).
    const QImage &overlay() const
    {
        return m_overlay;
    }
    double overlayOpacity() const
    {
        return m_overlayAlpha;
    }
    // Keep the original overload as an ABI/source-compatibility seam for the
    // single-image analysis view. Compare passes sourceSize when its display
    // image is an LOD rather than a full-resolution raster.
    void setImage(const QImage &img);
    void setImage(const QImage &img, const QSize &sourceSize);
    void setImage(const QImage &img, const QSize &sourceSize, const QRect &sourceRect);
    QSize sourceSize() const
    {
        return m_sourceSize;
    }
    QRect sourceRect() const
    {
        return m_sourceRect;
    }
    // Map a point in the complete source coordinate space to widget-local
    // coordinates. This deliberately uses sourceSize(), not the bounded
    // display raster dimensions, so covered-region/LOD panes keep annotations
    // registered with the source image.
    QPointF sourcePointToWidget(const QPointF &sourcePoint) const;
    QPoint displayPointForSource(int x, int y) const;
    void clear();

    // Momentary Compare changes presentation only. It deliberately leaves the
    // owned image, transform, ROI, overlays, and all CompareEngine state intact.
    void setTransientDisplay(const QImage &img, const QSize &sourceSize,
                             const QRect &sourceRect);
    void clearTransientDisplay();
    bool hasTransientDisplay() const
    {
        return !m_transientImage.isNull();
    }

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

    // A-4.3: Pixel Link markers (image-space points). Drawn as numbered dots so
    // the same index can be correlated across compared cells.
    void setLinkMarkers(const QVector<QPointF> &pts)
    {
        m_linkMarkers = pts;
        update();
    }
    void clearLinkMarkers()
    {
        m_linkMarkers.clear();
        update();
    }

    // H1: when the workspace decides this cell's diff was skipped because its size
    // does not match the base image, show a visible corner badge instead of letting
    // the diff overlay silently disappear (which would read as "no difference").
    void setSizeMismatch(bool on)
    {
        m_sizeMismatch = on;
        update();
    }
    const QVector<QPointF> &linkMarkers() const
    {
        return m_linkMarkers;
    }

    // Map widget coords → image-space pixel coords (top-left origin).
    QPointF widgetToImage(const QPoint &pos) const;

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
    // Viewport-bounded cached base+overlay surface (UI thread only). Holds the
    // transformed base image plus the optional diff overlay so annotation-only
    // repaints (ROI, crosshair, focus border, link markers, size-mismatch badge)
    // never re-scale the full source image. Bounded by widget viewport pixels,
    // never by scaled source dimensions (50x zoom still allocates viewport size).
    void ensureBaseSurface();
    // Single source of truth for base image + diff overlay geometry; used by the
    // cached surface and by the direct-draw fallback (allocation failure or
    // pathological geometry).
    void drawBaseLayer(QPainter &p);
    void releaseBaseSurface();
    QSize renderSourceSize() const;
    QRect renderSourceRect() const;

    QImage m_image;
    QSize m_sourceSize;
    QRect m_sourceRect;
    QImage m_transientImage;
    QSize m_transientSourceSize;
    QRect m_transientSourceRect;
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

    // Cached base surface: rebuilt only when its rendering inputs change.
    QImage m_baseSurface;
    bool m_baseSurfaceValid = false;
    // Diagnostic (dynamic QObject property baseSurfaceRenderCount) incremented
    // whenever the cached surface is actually re-rasterized. Tests distinguish
    // annotation repaints from source rasterization through it.
    uint64_t m_baseSurfaceRenderCount = 0;
    // Cache key inputs the current surface was rasterized from.
    qint64 m_cachedImageKey = -1;
    qint64 m_cachedOverlayKey = -1;
    double m_cachedOverlayAlpha = -1.0;
    double m_cachedScale = 0.0;
    QPointF m_cachedOffset;
    QSize m_cachedViewport;
    QRect m_cachedSourceRect;
    qreal m_cachedDpr = 0.0;

    // M16.1 sync crosshair state (image-space position)
    bool m_crosshairOn = false;
    QPointF m_crosshair;
    // M16.1 focus-lock (reference) flag
    bool m_focused = false;

    // A-4.3: pixel-link markers (image-space, top-left origin)
    QVector<QPointF> m_linkMarkers;

    // H1: size-mismatch badge flag (set by CompareWorkspace::refreshCellDiff).
    bool m_sizeMismatch = false;
};
