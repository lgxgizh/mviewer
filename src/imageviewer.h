#pragma once

#include "core/image/ImageFrame.h"
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"

#include <QPixmap>
#include <QStringList>
#include <QWidget>
#include <memory>

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

    // Returns the ImageFrame backing the current view (null if none loaded).
    // Lets the analysis panel route ROI analysis through the registry.
    std::shared_ptr<ImageFrame> frame() const
    {
        return m_frame;
    }

  public slots:
    void setSelectMode(bool on);

  signals:
    void regionStats(const QString &text);
    void selectionChanged(const QRect &sel); // image coords (may be null rect)
    void requestPrev();
    void requestNext();

    // Pixel Inspector (P1 #6): emitted on mouse move with the pixel under the
    // cursor, read directly from the ImageFrame (not QImage). x/y are image
    // pixel coordinates; valid=false when the cursor is outside the image.
    void pixelInfo(int x, int y, int r, int g, int b, bool valid);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    void fitToWidget();
    void preloadNeighbors(const QString &path);
    void drawHistogram(QPainter &painter) const;
    void computeHistogram(const QPixmap &pixmap);

    QPixmap loadPixmap(const QString &path);

    static QStringList listImages(const QString &dirPath);

    QPixmap m_pixmap;
    QString m_currentPath;
    QStringList m_fileList;
    int m_currentIndex = -1;

    // View transform (pan/zoom). The math lives in the domain-free Viewport
    // (core/render); the Widget only stores it and feeds screen geometry.
    Viewport m_view;
    // Tile grid for the current image; drives per-tile rendering so large
    // images (100MP/RAW) are rasterized a tile at a time, never one bitmap.
    TileGrid m_tiles;
    // LRU tile cache (memory tier of the Render Pipeline). Visible tiles are
    // decoded once and reused across paints; LOD selection keeps zoomed-out
    // views cheap. No decode happens in the Widget — the cache's decode
    // callback calls RenderEngine (core/), never QWidget.
    TileCache m_tileCache;

    bool m_dragging = false;
    QPoint m_lastMousePos;

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
