#pragma once

#include "core/image/ImageFrame.h"
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"
#include "gpu/GpuTileUploader.h"

#include <QPixmap>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <optional>

// Full-image zoomable viewer. Shown in its own window when the user
// double-clicks a thumbnail (or single-clicks the bottom-left preview).
// Supports wheel zoom, left-drag pan, brightness histogram overlay, and
// a LRU cache of decoded pixmaps.
class ImageViewer : public QWidget
{
    Q_OBJECT

  public:
    explicit ImageViewer(QWidget *parent = nullptr);
    ~ImageViewer() override;

    void setImage(const QString &path);

    // P1-7: serialize/restore the current view transform (scale + pan). Used to
    // restore the viewer's zoom level and pan position across sessions. Viewport
    // is domain-free (core/render), so it carries no Qt types.
    Viewport viewTransform() const
    {
        return m_view;
    }
    void setViewTransform(const Viewport &v);

    // Returns the ImageFrame backing the current view (null if none loaded).
    // Lets the analysis panel route ROI analysis through the registry.
    std::shared_ptr<ImageFrame> frame() const
    {
        return m_frame;
    }

  signals:
    // Emitted on the UI thread once an async load (setImage) completes. Carries
    // the decoded ImageFrame so the analysis panel can run without re-decoding.
    void imageReady(std::shared_ptr<ImageFrame> frame);

  public slots:
    void setSelectMode(bool on);
    // Zoom commands (keyboard / menu driven). zoomIn/zoomOut zoom around the
    // widget center; zoomFit fits the whole image into the window and keeps
    // re-fitting on resize; zoomActual restores 100% around the view center.
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomActual();

  signals:
    // Emitted when the async decode of a setImage() request fails, so the
    // host can surface the failure (status bar) instead of it being silent.
    void loadFailed(const QString &path);

    void regionStats(const QString &text);
    void selectionChanged(const QRect &sel); // image coords (may be null rect)
    void requestPrev();
    void requestNext();

    // Pixel Inspector (P1 #6): emitted on mouse move with the pixel under the
    // cursor, read directly from the ImageFrame (not QImage). x/y are image
    // pixel coordinates; valid=false when the cursor is outside the image.
    void pixelInfo(int x, int y, int r, int g, int b, bool valid);

    // P0 #①: live zoom factor (percent) for the status bar. Emitted on wheel
    // zoom and on fit/resize.
    void zoomChanged(int percent);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void emitZoom();
    void fitToWidget();
    void preloadNeighbors(const QString &path);
    void drawHistogram(QPainter &painter) const;
    void computeHistogram(const QPixmap &pixmap);

    static QStringList listImages(const QString &dirPath);

    QPixmap m_pixmap;
    QString m_currentPath;
    QStringList m_fileList;
    int m_currentIndex = -1;

    // View transform (pan/zoom). The math lives in the domain-free Viewport
    // (core/render); the Widget only stores it and feeds screen geometry.
    Viewport m_view;
    // P1-7: a pending transform to apply once the (async) image load completes,
    // since scale/offset are only meaningful after the frame/screen are known.
    std::optional<Viewport> m_pendingView;
    // Tile grid for the current image; drives per-tile rendering so large
    // images (100MP/RAW) are rasterized a tile at a time, never one bitmap.
    TileGrid m_tiles;
    // LRU tile cache (memory tier of the Render Pipeline). Visible tiles are
    // decoded once and reused across paints; LOD selection keeps zoomed-out
    // views cheap. No decode happens in the Widget — the cache's decode
    // callback calls RenderEngine (core/), never QWidget.
    TileCache m_tileCache;

    // M16: GPU upload tier (opt-in, capability-gated). When enabled
    // (real GL context + MVIEWER_GPU=1), decoded tiles are uploaded to
    // GL textures once and composited from resident handles; otherwise this
    // stays idle and the CPU QPainter path above is used. Bookkeeping is
    // unit-tested headlessly; the actual GL upload runs only where a
    // context exists.
    GpuTileUploader m_gpu;

    bool m_dragging = false;
    QPoint m_lastMousePos;

    // Fit mode: while true the image is kept fitted to the window, so a
    // resize re-fits. Cleared by any explicit zoom (wheel / keyboard / menu).
    bool m_fitMode = true;

    // ImageFrame backing the current view. The QWidget itself never decodes;
    // it only renders the QPixmap produced by ImageRepository.
    std::shared_ptr<ImageFrame> m_frame;

    int m_histogram[256] = {0};
    bool m_hasHistogram = false;

    bool m_selecting = false;
    bool m_selectMode = false;
    QPoint m_selStart, m_selEnd;

    QRect selectedRegion() const;
};
