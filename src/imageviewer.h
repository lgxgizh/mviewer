#pragma once

#include "application/ImageLoadingService.h"
#include "core/async/AsyncLifetimeToken.h"
#include "core/export/ExportJob.h"
#include "core/analysis/ImageOverlay.h"
#include "core/image/ImageFrame.h"
#include "core/render/AsyncTileRequestManager.h"
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"
#include "gpu/GpuTileUploader.h"

#include <QImage>
#include <QOpenGLTextureBlitter>
#include <QOpenGLWidget>
#include <QPointer>
#include <QPixmap>
#include <QStringList>
#include <memory>
#include <optional>
#include <atomic>
#include <functional>
#include <vector>

class QEvent;
class QAction;
class QContextMenuEvent;
class QPainter;
class QTimer;

// Full-image zoomable viewer. Shown in its own window when the user
// double-clicks a thumbnail (or single-clicks the bottom-left preview).
// Supports wheel zoom, left-drag pan, brightness histogram overlay, and
// a LRU cache of decoded pixmaps.
//
// Stage A (M13 / A-8.1): inherits QOpenGLWidget so a real GL context exists
// for GpuTileUploader. When MVIEWER_GPU=1 and a context is current, tiles are
// uploaded once and composited via QPainter::drawTexture; otherwise the CPU
// QPainter path remains the verified default.
class ImageViewer : public QOpenGLWidget
{
    Q_OBJECT

  public:
    explicit ImageViewer(QWidget *parent = nullptr);
    ~ImageViewer() override;

    // Consume the Browse sequence published by ThumbnailPanel/ImageListModel.
    // The viewer never enumerates the filesystem to infer neighbors.
    void setBrowseSequence(const QStringList &paths);
    QStringList browseSequence() const
    {
        return m_fileList;
    }
    int browseIndex() const
    {
        return m_currentIndex;
    }
    // Open from Browse with the first native presentation already fullscreen.
    void showBrowseFullscreen();
    // Display-only warm thumbnail used while the full frame and first visible
    // tiles arrive. It is never used as an analysis/ROI source.
    void setProvisionalImage(const QString &path, const QImage &image,
                             const QSize &sourceSize = QSize());
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
    void viewerClosed();

  public slots:
    // Single source of truth for every ImageViewer fullscreen entry point.
    // The requested property is the deterministic contract on headless Qt;
    // native window state is updated as a side effect, never independently.
    void setFullscreenRequested(bool requested);
    void toggleFullscreen();
    // Copy/save dispatch the worker-side ExportJob. The GUI thread only starts
    // the job and receives the final clipboard/result presentation.
    void copyToClipboard(const QString &path = {});
    void saveToPath(const QString &path);
    void setSelectMode(bool on);
    // Zoom commands (keyboard / menu driven). zoomIn/zoomOut zoom around the
    // widget center; zoomFit fits the whole image into the window and keeps
    // re-fitting on resize; zoomActual restores 100% around the view center.
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomActual();
    void setOverlayMode(mviewer::OverlayMode m); // F4 (M22): live zebra/false-color overlay
    void setZebraThreshold(int t); // F4 (M22): live zebra threshold (shared with prefs/dialog)
    mviewer::OverlayMode overlayMode() const
    {
        return m_overlayMode;
    }

  signals:
    // Emitted when the async decode of a setImage() request fails, so the
    // host can surface the failure (status bar) instead of it being silent.
    void loadFailed(const QString &path);
    void exportFinished(bool success, const QString &message);

    void regionStats(const QString &text);
    void selectionChanged(const QRect &sel); // image coords (may be null rect)
    void requestPrev();
    void requestNext();

    // Pixel Inspector (P1 #6): emitted on mouse move with the pixel under the
    // cursor, read directly from the ImageFrame (not QImage). x/y are image
    // pixel coordinates; valid=false when the cursor is outside the image.
    void pixelInfo(int x, int y, int r, int g, int b, int a, int r16, int g16, int b16, int rawKind,
                   bool valid);

    // P0 #①: live zoom factor (percent) for the status bar. Emitted on wheel
    // zoom and on fit/resize.
    void zoomChanged(int percent);

    // A-7.3: user picked an analyzer from the context-menu "分析" submenu.
    // MainWindow shows the AnalysisPanel and routes the run through it so
    // results land in the unified Result tab (not a QMessageBox).
    void analysisRequested(const QString &analyzerId);

  protected:
    void initializeGL() override;
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
    void leaveEvent(QEvent *event) override;

  private:
    using ImageLoadResult = mviewer::application::ImageLoadingService::Result;
    using ImageLoadGuard = std::shared_ptr<QPointer<ImageViewer>>;
    using ImageLoadCallback = std::function<void(const ImageLoadResult &)>;

    void emitZoom();
    void advanceViewportRevision();
    void beginImageGeneration();
    void fitToWidget();
    void preloadNeighbors(const QString &path);
    void drawHistogram(QPainter &painter) const;
    void computeHistogram();
    // M29: cancel the outstanding foreground load and any neighbor preloads.
    // Called at the start of setImage() and from closeEvent()/the destructor so
    // obsolete full-resolution work from a superseded navigation is dropped.
    void cancelCurrentLoad();
    void cancelPreloads();
    void cancelExportJob();
    void startExportJob(mviewer::exportjob::ExportJobConfig cfg, bool clipboard,
                        const QString &destination = {});
    // Pixel Inspector lifecycle: emit a single pixelInfo with valid=false
    // (x/y=-1, zero channels) so a stale sample never lingers. Called
    // synchronously at the top of setImage() and closeEvent(), and from
    // leaveEvent(); never emitted from the destructor.
    void clearPixelInfo();
    void cancelRoiStats();
    void scheduleRoiStats(const QRect &selection);
    ImageLoadCallback makeImageLoadCallback(const QString &path, uint64_t generation);
    // M46: static delivery helpers. They marshal the load outcome to the UI
    // thread through the QPointer guard and never touch `this`, so the
    // worker-thread completion lambda captures NO raw `this` — a late
    // completion can never begin a viewer-visible access.
    static void queueImageLoadFailure(const QString &path, uint64_t generation,
                                      const ImageLoadGuard &guard);
    static void queueLoadedImage(const QString &path, uint64_t generation,
                                 const ImageLoadGuard &guard, const ImageLoadResult &result);
    void applyLoadedImage(const QString &path, const ImageLoadResult &result);
    void applyPendingView();
    void clearLoadedGpu();
    void scheduleLoadedRefit(const QString &path, uint64_t generation,
                             const ImageLoadGuard &guard);
    void drawProvisional(QPainter &painter) const;
    AsyncTileRequestManager::VisibleTiles requestVisibleTiles();
    void scheduleOverlayTiles(std::vector<TileCache::ReadyTile> &ready);
    void drawGpuTiles(QPainter &painter, const std::vector<TileCache::ReadyTile> &ready,
                      const Viewport &tileView);
    void drawCpuTiles(QPainter &painter, const std::vector<TileCache::ReadyTile> &ready,
                      const Viewport &tileView);
    void drawEmptyState(QPainter &painter);
    void drawSelection(QPainter &painter);
    bool handleNavigationKey(int key);
    bool handleZoomKey(int key, Qt::KeyboardModifiers modifiers);
    bool handleModeKey(int key, Qt::KeyboardModifiers modifiers);
    bool handleContextCopyAction(QAction *chosen, QAction *copy, QAction *copyPath,
                                 QAction *copyColor, QContextMenuEvent *event);
    bool handleContextImageAction(QAction *chosen, QAction *saveAs, QAction *zoomInAction,
                                  QAction *zoomOutAction, QAction *zoomFitAction,
                                  QAction *zoomActualAction, QAction *selectRegion);
    bool handleContextNavigationAction(QAction *chosen, QAction *next, QAction *prev,
                                       QAction *overlayNone, QAction *overlayZebra,
                                       QAction *overlayFalse, QAction *fullscreen);
    // Preload promotion: consume the neighbor preload handle that matches
    // `path` and cancel all others, so a navigation back to a preloaded
    // neighbor can be promoted to the foreground decode without re-queuing.
    mviewer::application::ImageLoadingService::AsyncRequestHandle takeMatchingPreload(
        const QString &path);
    QString m_currentPath;
    QStringList m_fileList;
    int m_currentIndex = -1;
    // M27: request generation — bumped on every setImage() so a late delivery
    // from an older request can never overwrite the current image, even for
    // the same path (A -> B -> A where the first A completes last).
    uint64_t m_requestGen = 0;
    // Image lifetime and viewport churn are deliberately separate. A pan,
    // zoom or resize changes presentation geometry but must not invalidate
    // reusable in-flight tile work for the same image.
    uint64_t m_imageGeneration = 0;
    uint64_t m_viewRevision = 0;
    uint64_t m_roiRevision = 0;
    uint64_t m_overlayGeneration = 0;

    // M29: cancellable request handles. m_foregroundRequest tracks the current
    // setImage() decode; m_neighborPreloads tracks at most the previous/next
    // preloads, each bound to the path it prefetches so the same path can be
    // promoted to the foreground decode on navigation. Navigation cancels the
    // foreground request and every nonmatching neighbor preload, while a
    // matching neighbor may be promoted; close / destruction cancel all. A
    // cancelled request's transient Result is released, and decoded frame cache
    // ownership remains budgeted by CacheManager.
    struct NeighborPreload
    {
        QString path;
        mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
    };
    mviewer::application::ImageLoadingService::AsyncRequestHandle m_foregroundRequest;
    std::vector<NeighborPreload> m_neighborPreloads;
    // M46: consumer-lifetime token. Created in the constructor, invalidated in
    // the destructor BEFORE the outstanding requests are cancelled, and passed
    // to every async load/preload. The repository suppresses the client
    // callback of any request whose token is dead, so a worker completion that
    // races viewer destruction can never begin a new callback that touches
    // this viewer.
    std::shared_ptr<mviewer::core::AsyncLifetimeToken> m_lifetime;

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
    // Derived overlay tiles are separate from the display base cache and are
    // invalidated whenever the overlay policy changes.
    TileCache m_overlayCache;
    AsyncTileRequestManager m_tileRequests;
    AsyncTileRequestManager m_overlayRequests;

    // Warm thumbnail shown while the full frame and first visible tiles arrive.
    // This display-only surface is never exposed as m_frame.
    QString m_provisionalPath;
    QImage m_provisionalImage;
    QSize m_provisionalSourceSize;
    bool m_tileRepaintQueued = false;
    bool m_loading = false;
    TaskScheduler::TaskHandle m_roiStatsRequest;
    uint64_t m_exportGeneration = 0;
    TaskScheduler::TaskHandle m_exportTask;
    std::shared_ptr<std::atomic<bool>> m_exportCancel;

    // M16 / Stage A: GPU upload tier (opt-in, capability-gated). When enabled
    // (real GL context + MVIEWER_GPU=1), decoded tiles are uploaded to
    // GL textures once and composited via QOpenGLTextureBlitter; otherwise
    // this stays idle and the CPU QPainter path is used. Bookkeeping is
    // unit-tested headlessly; the actual GL upload runs only where a
    // context exists.
    GpuTileUploader m_gpu;
    QOpenGLTextureBlitter m_blitter;
    bool m_blitterReady = false;

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

    // F4 (M22): live analysis overlay (zebra / false-color) applied on a
    // deep-copied tile so the TileCache buffer is never mutated.
    mviewer::OverlayMode m_overlayMode = mviewer::OverlayMode::None;
    int m_zebraThreshold = 2;

    bool m_selecting = false;
    bool m_selectMode = false;
    QPoint m_selStart, m_selEnd;

    // Auto-hide cursor in fullscreen after inactivity.
    QTimer *m_cursorHideTimer = nullptr;
    bool m_cursorHidden = false;

    QRect selectedRegion() const;
};
