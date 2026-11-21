// First zoomed-out LOD must Sharp-resample to the device-pixel dest
// ImageViewer::drawDisplayRaster will blit. Generated temp assets only.

#include "imageviewer.h"
#include "imageviewer_displayfit.h"

#include "core/image/ImageFrame.h"
#include "core/render/Viewport.h"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>

static int g_failures = 0;

#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s\n", m);                                                          \
            std::fflush(stdout);                                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

namespace
{

constexpr int kReadyMs = 30000;

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

QSize independentPaintDest(int sourceW, int sourceH, int widgetW, int widgetH, double dpr,
                           bool maximizeFit)
{
    Viewport view;
    view.screenW = widgetW;
    view.screenH = widgetH;
    view.fit(sourceW, sourceH, maximizeFit ? FitPolicy::MaximizeClient : FitPolicy::Comfortable);
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    view.imageRectToScreen(0, 0, sourceW, sourceH, sx, sy, sw, sh);
    const double ratio = dpr > 1.0 ? dpr : 1.0;
    return QSize(std::max(1, static_cast<int>(std::lround(static_cast<double>(sw) * ratio))),
                 std::max(1, static_cast<int>(std::lround(static_cast<double>(sh) * ratio))));
}

DisplayPaintDest helperDest(int sourceW, int sourceH, int widgetW, int widgetH, double dpr,
                            bool maximizeFit)
{
    DisplayPaintDestRequest req;
    req.sourceW = sourceW;
    req.sourceH = sourceH;
    req.widgetW = widgetW;
    req.widgetH = widgetH;
    req.dpr = dpr;
    req.maximizeFit = maximizeFit;
    return planPaintDisplayDest(req);
}

void checkHelperCase(const char *label, int sourceW, int sourceH, int widgetW, int widgetH,
                     double dpr, bool maximizeFit, QSize expect = {})
{
    const DisplayPaintDest dest = helperDest(sourceW, sourceH, widgetW, widgetH, dpr, maximizeFit);
    const QSize oracle = independentPaintDest(sourceW, sourceH, widgetW, widgetH, dpr, maximizeFit);
    CHECK(dest.valid, qPrintable(QStringLiteral("%1: dest valid").arg(label)));
    CHECK(dest.width == oracle.width() && dest.height == oracle.height(),
          qPrintable(QStringLiteral("%1: helper matches paint oracle (%2x%3 vs %4x%5)")
                         .arg(label)
                         .arg(dest.width)
                         .arg(dest.height)
                         .arg(oracle.width())
                         .arg(oracle.height())));
    if (expect.width() > 0 && expect.height() > 0)
    {
        CHECK(dest.width == expect.width() && dest.height == expect.height(),
              qPrintable(QStringLiteral("%1: expected %2x%3, got %4x%5")
                             .arg(label)
                             .arg(expect.width())
                             .arg(expect.height())
                             .arg(dest.width)
                             .arg(dest.height)));
    }
    std::printf("helper %s source=%dx%d widget=%dx%d dpr=%.3f max=%d dest=%dx%d density=%.4f\n",
                label, sourceW, sourceH, widgetW, widgetH, dpr, maximizeFit ? 1 : 0, dest.width,
                dest.height, dest.density);
    std::fflush(stdout);
}

void testHelper()
{
    // FP: Comfortable offsetX is just under 48, so imageRectToScreen width is 1825.
    // devicePixelsForSource is 3648x2049; paint dest is 3650x2050.
    checkHelperCase("3838x2156 Comfortable DPR2", 3838, 2156, 1920, 1080, 2.0, false,
                    QSize(3650, 2050));
    {
        int tw = 0;
        int th = 0;
        Viewport fitted;
        fitted.screenW = 1920;
        fitted.screenH = 1080;
        fitted.fit(3838, 2156, FitPolicy::Comfortable);
        const double density = fitted.scale * 2.0;
        devicePixelsForSource(3838, 2156, density, tw, th);
        CHECK(tw == 3648 && th == 2049, "devicePixelsForSource stays 3648x2049 on this case");
        CHECK(th != 2050, "paint dest height is the floor-to-1025 path, not density lround");
        CHECK(tw != 3650, "paint dest width is 3650 after floor(offsetX<48), not 3648");
    }

    checkHelperCase("4096x3072 Comfortable DPR2", 4096, 3072, 1920, 1080, 2.0, false);
    checkHelperCase("3838x2156 MaximizeClient DPR2", 3838, 2156, 1920, 1080, 2.0, true);
    checkHelperCase("3838x2156 Comfortable DPR1.25", 3838, 2156, 1920, 1080, 1.25, false);
    checkHelperCase("3838x2156 Comfortable DPR1.5", 3838, 2156, 1920, 1080, 1.5, false);
    checkHelperCase("4096x3072 MaximizeClient DPR1.5", 4096, 3072, 1920, 1080, 1.5, true);
    {
        const DisplayPaintDest comfortable = helperDest(3838, 2156, 1920, 1080, 2.0, false);
        const DisplayPaintDest maximize = helperDest(3838, 2156, 1920, 1080, 2.0, true);
        CHECK(comfortable.valid && maximize.valid, "Comfortable and MaximizeClient dests valid");
        CHECK(comfortable.width != maximize.width || comfortable.height != maximize.height,
              "MaximizeClient dest differs from Comfortable");
    }

    const DisplayPaintDest small = helperDest(800, 600, 1920, 1080, 1.0, false);
    CHECK(small.valid, "800x600 dest valid");
    CHECK(small.density >= 1.0, "800x600 Comfortable into 1920x1080 is density>=1");
    const DisplayPaintDest smallDpr2 = helperDest(800, 600, 1920, 1080, 2.0, false);
    CHECK(smallDpr2.valid && smallDpr2.density >= 1.0, "800x600 at DPR2 still density>=1");

    const DisplayPaintDest missing = helperDest(3838, 2156, 0, 0, 2.0, false);
    CHECK(!missing.valid, "widget 0x0 cannot plan a paint dest");
}

QString writeJpeg(const QTemporaryDir &tmp, const QString &name, int w, int h,
                  QImage::Format format)
{
    QImage img(w, h, format);
    img.fill(QColor(40, 90, 140));
    const QString path = tmp.filePath(name);
    if (!img.save(path, "JPEG", 90))
        return {};
    return path;
}

bool geometryIsTarget(const ImageViewer &viewer)
{
    return viewer.width() == 1920 && viewer.height() == 1080 && viewer.devicePixelRatioF() >= 1.99;
}

void logGeometry(const char *label, const ImageViewer &viewer)
{
    std::printf("%s widget=%dx%d dpr=%.3f\n", label, viewer.width(), viewer.height(),
                viewer.devicePixelRatioF());
    std::fflush(stdout);
}

void testWidgetLodIdentity(const QTemporaryDir &tmp)
{
    ImageViewer viewer;
    viewer.setMinimumSize(1920, 1080);
    viewer.setFixedSize(1920, 1080);
    viewer.resize(1920, 1080);
    viewer.show();
    pump(50);
    logGeometry("lod-widget", viewer);
    if (!geometryIsTarget(viewer))
    {
        std::printf("FAIL: widget geometry is %dx%d dpr=%.3f; need 1920x1080 and dpr>=2\n",
                    viewer.width(), viewer.height(), viewer.devicePixelRatioF());
        std::fflush(stdout);
        ++g_failures;
        return;
    }

    const QString large =
        writeJpeg(tmp, QStringLiteral("lod_3838x2156.jpg"), 3838, 2156, QImage::Format_RGB888);
    CHECK(!large.isEmpty(), "8-bit 3838x2156 JPEG written");
    if (large.isEmpty())
        return;

    bool display = false;
    QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                     [&](const QSize &) { display = true; });
    QElapsedTimer timer;
    timer.start();
    viewer.setBrowseSequence({large});
    viewer.setImage(large);
    const bool ready = waitTrue([&] { return display && viewer.isLodDisplay(); }, kReadyMs);
    const qint64 firstRasterMs = timer.elapsed();
    CHECK(ready, "above-4MP 8-bit JPEG reaches first stable LOD");
    if (!ready)
    {
        std::printf("lod source=3838x2156 raster=none dest=none firstRasterMs=%lld identity=0\n",
                    static_cast<long long>(firstRasterMs));
        std::fflush(stdout);
        return;
    }

    const DisplayPaintDest dest =
        helperDest(3838, 2156, viewer.width(), viewer.height(), viewer.devicePixelRatioF(), false);
    const QImage raster = viewer.displayRaster();
    const bool identity =
        dest.valid && raster.width() == dest.width && raster.height() == dest.height;
    const double scale = viewer.viewTransform().scale;
    const double dpr = viewer.devicePixelRatioF();
    const bool wouldSmooth = !identity && ImageViewer::lodRasterUsesSmoothing(scale, dpr);
    std::printf("lod widget=%dx%d dpr=%.3f source=3838x2156 raster=%dx%d dest=%dx%d "
                "firstRasterMs=%lld identity=%d\n",
                viewer.width(), viewer.height(), dpr, raster.width(), raster.height(), dest.width,
                dest.height, static_cast<long long>(firstRasterMs), identity ? 1 : 0);
    std::fflush(stdout);
    CHECK(viewer.isLodDisplay(), "above-4MP uses the LOD path");
    CHECK(dest.valid, "helper dest is valid for the widget");
    CHECK(identity, "first stable LOD raster equals paint dest");
    CHECK(!wouldSmooth, "paint identity disables SmoothPixmapTransform");
}

void testSmallImageOpens(const QTemporaryDir &tmp)
{
    ImageViewer viewer;
    viewer.setMinimumSize(1920, 1080);
    viewer.setFixedSize(1920, 1080);
    viewer.resize(1920, 1080);
    viewer.show();
    pump(50);
    logGeometry("small-widget", viewer);
    if (!geometryIsTarget(viewer))
    {
        std::printf("FAIL: widget geometry is %dx%d dpr=%.3f; need 1920x1080 and dpr>=2\n",
                    viewer.width(), viewer.height(), viewer.devicePixelRatioF());
        std::fflush(stdout);
        ++g_failures;
        return;
    }

    const QString small =
        writeJpeg(tmp, QStringLiteral("small_300x200.jpg"), 300, 200, QImage::Format_RGB888);
    CHECK(!small.isEmpty(), "below-cutoff JPEG written");
    if (small.isEmpty())
        return;

    bool image = false;
    QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                     [&](std::shared_ptr<ImageFrame>) { image = true; });
    QElapsedTimer timer;
    timer.start();
    viewer.setBrowseSequence({small});
    viewer.setImage(small);
    const bool ready = waitTrue([&] { return image && viewer.frame(); }, kReadyMs);
    const qint64 firstRasterMs = timer.elapsed();
    CHECK(ready, "below-cutoff image still opens");
    CHECK(!viewer.isLodDisplay(), "below-cutoff image stays on the full-frame path");
    const QSize source = viewer.displaySize();
    std::printf("small widget=%dx%d dpr=%.3f source=%dx%d raster=n/a dest=n/a "
                "firstRasterMs=%lld identity=n/a\n",
                viewer.width(), viewer.height(), viewer.devicePixelRatioF(), source.width(),
                source.height(), static_cast<long long>(firstRasterMs));
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);

    testHelper();

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("FAIL: temp dir\n");
        return 1;
    }
    testWidgetLodIdentity(tmp);
    testSmallImageOpens(tmp);

    std::printf("=== first LOD Sharp paint-dest tests: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
