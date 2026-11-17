#include "check.h"
#include "imageviewer.h"
#include "imageviewer_transitionpaint.h"
#include <QApplication>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <cmath>

// Reuse the existing friend test seam (no extra public widget API).
struct ImageViewerPromotedRasterTestAccess
{
    static void configure(ImageViewer &v, const QRect &region = QRect(0, 0, 3838, 2156))
    {
        v.resize(1920, 1080);
        v.m_view.screenW = v.width();
        v.m_view.screenH = v.height();
        v.m_view.fit(3838, 2156, FitPolicy::Comfortable);
        int x = 0, y = 0, w = 0, h = 0;
        v.m_view.imageRectToScreen(region.x(), region.y(), region.width(), region.height(), x, y, w,
                                   h);
        const double dpr = std::max(1.0, v.devicePixelRatioF());
        QImage raster(static_cast<int>(std::lround(w * dpr)),
                      static_cast<int>(std::lround(h * dpr)), QImage::Format_RGB32);
        raster.fill(Qt::red);
        v.m_lodMode = true;
        v.m_raster = {raster, region, QSize(3838, 2156), 1.0};
        v.prepareTransitionImage(QStringLiteral("next"), false);
        v.m_raster = {};
        v.m_lodMode = false;
        v.m_loading = true;
    }
    static void draw(const ImageViewer &v, QPainter &p)
    {
        v.drawTransition(p);
    }
    static void mismatch(ImageViewer &v)
    {
        v.m_view.scale *= 0.9;
    }
    static void pan(ImageViewer &v)
    {
        v.m_view.offsetX -= 300;
    }
    static QRect region(const ImageViewer &v)
    {
        return v.m_transitionSourceRect;
    }
    static QRect damage(const ImageViewer &v)
    {
        return v.loadingAnimationDamage();
    }
    static void fullBleed(ImageViewer &v)
    {
        v.m_view.offsetY = 0;
    }
    static void clear(ImageViewer &v)
    {
        v.prepareTransitionImage({}, false);
    }
    static void expire(ImageViewer &v)
    {
        v.scheduleStandInExpiry(v.m_requestGen);
    }
    static bool expired(const ImageViewer &v)
    {
        return v.m_transitionImage.isNull();
    }
    static void tick(ImageViewer &v)
    {
        if (v.m_busyTicker)
            QMetaObject::invokeMethod(v.m_busyTicker, "timeout", Qt::DirectConnection);
    }
    static void finish(ImageViewer &v)
    {
        v.m_loading = false;
    }
};

class ImageDrawRecorder final : public QPaintEngine
{
  public:
    ImageDrawRecorder() : QPaintEngine(QPaintEngine::AllFeatures)
    {
    }
    bool begin(QPaintDevice *) override
    {
        return true;
    }
    bool end() override
    {
        return true;
    }
    Type type() const override
    {
        return QPaintEngine::User;
    }
    void updateState(const QPaintEngineState &s) override
    {
        smooth = s.renderHints().testFlag(QPainter::SmoothPixmapTransform);
    }
    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override
    {
    }
    void drawImage(const QRectF &r, const QImage &, const QRectF &s,
                   Qt::ImageConversionFlags) override
    {
        ++images;
        imageSmooth = smooth;
        target = r;
        source = s;
    }
    bool smooth = false;
    bool imageSmooth = false;
    int images = 0;
    QRectF target, source;
};

class RecordingDevice final : public QPaintDevice
{
  public:
    QPaintEngine *paintEngine() const override
    {
        return &recorder;
    }
    int metric(PaintDeviceMetric m) const override
    {
        if (m == PdmWidth || m == PdmHeight)
            return 4096;
        if (m == PdmDpiX || m == PdmDpiY || m == PdmPhysicalDpiX || m == PdmPhysicalDpiY)
            return 96;
        if (m == PdmDepth)
            return 32;
        if (m == PdmDevicePixelRatioScaled)
            return static_cast<int>(QPaintDevice::devicePixelRatioFScale());
        return 1;
    }
    mutable ImageDrawRecorder recorder;
};

void checkSampling()
{
    for (const double dpr : {1.0, 1.25, 1.5, 2.0})
    {
        const QSize logical(800, 600);
        const QSize physical(static_cast<int>(800 * dpr), static_cast<int>(600 * dpr));
        MVIEWER_CHECK(!transitionRasterNeedsSmoothing(physical, logical, dpr),
                      "identity device-pixel transition avoids smooth resampling");
        MVIEWER_CHECK(transitionRasterNeedsSmoothing(physical + QSize(1, 0), logical, dpr),
                      "one-pixel resize/DPR mismatch preserves smooth sampling");
    }
    ImageViewer viewer;
    ImageViewerPromotedRasterTestAccess::configure(viewer);
    RecordingDevice device;
    {
        QPainter p(&device);
        ImageViewerPromotedRasterTestAccess::draw(viewer, p);
    }
    MVIEWER_CHECK(device.recorder.images == 1 && !device.recorder.imageSmooth,
                  "actual drawTransition path blits matching Sharp raster without smoothing");
    ImageViewerPromotedRasterTestAccess::mismatch(viewer);
    {
        QPainter p(&device);
        ImageViewerPromotedRasterTestAccess::draw(viewer, p);
    }
    MVIEWER_CHECK(device.recorder.imageSmooth, "actual mismatched transition remains smooth");
}

void checkRegionGeometry()
{
    ImageViewer viewer;
    const QRect sourceRegion(512, 512, 2000, 1000);
    ImageViewerPromotedRasterTestAccess::configure(viewer, sourceRegion);
    MVIEWER_CHECK(ImageViewerPromotedRasterTestAccess::region(viewer) == sourceRegion,
                  "transition retains previous region rather than stretching it over full source");
    ImageViewerPromotedRasterTestAccess::pan(viewer);
    RecordingDevice device;
    QPainter p(&device);
    ImageViewerPromotedRasterTestAccess::draw(viewer, p);
    MVIEWER_CHECK(device.recorder.images == 1 && !device.recorder.target.isEmpty(),
                  "panned region stand-in remains visible at its original coordinates");
    MVIEWER_CHECK(!device.recorder.imageSmooth, "panning alone does not change Sharp density");
}

class PaintProbe final : public ImageViewer
{
  public:
    QRegion damage;
    int paints = 0;

  protected:
    void paintEvent(QPaintEvent *e) override
    {
        ++paints;
        damage += e->region();
        ImageViewer::paintEvent(e);
    }
};

void checkTickerDamage()
{
    PaintProbe viewer;
    viewer.resize(800, 600);
    ImageViewerPromotedRasterTestAccess::configure(viewer);
    viewer.show();
    QApplication::processEvents();
    MVIEWER_CHECK(!viewer.loadingIndicatorVisible(), "loading feedback keeps its grace period");
    MVIEWER_CHECK(ImageViewerPromotedRasterTestAccess::damage(viewer) ==
                      QRect(0, 0, viewer.width(), 3),
                  "fitted image leaves ticker-safe top margin");
    // Grace expiry, not a machine-speed performance assertion.
    mviewer::test::waitFor([&viewer] { return viewer.loadingIndicatorVisible(); }, 3000);
    QApplication::processEvents();
    viewer.damage = {};
    viewer.paints = 0;
    ImageViewerPromotedRasterTestAccess::tick(viewer);
    MVIEWER_CHECK(mviewer::test::waitFor([&viewer] { return viewer.paints > 0; }, 3000),
                  "loading animation remains visible and schedules a paint");
    const QRect band(0, 0, viewer.width(), 3);
    MVIEWER_CHECK(viewer.damage.subtracted(QRegion(band)).isEmpty(),
                  "loading ticker only invalidates three-pixel band in safe fitted view");
    ImageViewerPromotedRasterTestAccess::fullBleed(viewer);
    MVIEWER_CHECK(ImageViewerPromotedRasterTestAccess::damage(viewer) == viewer.rect(),
                  "full-bleed/zoomed view preserves ordinary content painting");
    ImageViewerPromotedRasterTestAccess::finish(viewer);
    viewer.damage = {};
    viewer.paints = 0;
    ImageViewerPromotedRasterTestAccess::tick(viewer);
    mviewer::test::waitFor([&viewer] { return viewer.paints > 0; }, 3000);
    MVIEWER_CHECK(viewer.damage.contains(viewer.rect()),
                  "completion repaints content to clear bar");
}

void checkExpiry()
{
    ImageViewer viewer;
    ImageViewerPromotedRasterTestAccess::configure(viewer);
    ImageViewerPromotedRasterTestAccess::expire(viewer);
    MVIEWER_CHECK(
        mviewer::test::waitFor(
            [&viewer] { return ImageViewerPromotedRasterTestAccess::expired(viewer); }, 3000),
        "cross-path stand-in still expires without waiting for next decode");
    ImageViewerPromotedRasterTestAccess::clear(viewer);
    MVIEWER_CHECK(ImageViewerPromotedRasterTestAccess::expired(viewer),
                  "empty path clears transition");
}

int main(int argc, char **argv)
{
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);
    checkSampling();
    checkRegionGeometry();
    checkTickerDamage();
    checkExpiry();
    return mviewer::test::failures() ? 1 : 0;
}
