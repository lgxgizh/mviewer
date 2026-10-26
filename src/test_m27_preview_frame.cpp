// M27 4c-4f: selection-preview stable frame (no "small then grows") and the
// centre viewer's provisional geometry. Part of m27_lifetime_tests; runs
// from test_m27_lifetime.cpp's main via runM27PreviewFrameTests().

#include "imageviewer.h"
#include "previewpanel.h"

#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPixmap>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

using Priority = TaskScheduler::Priority;
using PoolType = TaskScheduler::PoolType;
using TaskContext = TaskScheduler::TaskContext;

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
}

// ─── 4c. Selection preview: stable frame, no "small then grows" ────────────
// Tests 1/3-7 for the selection-preview flicker fix (test 2, the provider's
// EXIF-thumb crop, lives in pixelinspector_tests next to the orient helper).
QString makeSolidImage(const QString &dir, const QString &name, int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    const QString path = dir + "/" + name;
    img.save(path, "PNG");
    return path;
}

template <typename F> bool waitFor(F &&done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        pump(10);
    return done();
}

QPixmap solidPixmap(int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    return QPixmap::fromImage(img);
}

QString previewFixtureDir(const QString &tag)
{
    const QString dir = QDir::tempPath() + "/mviewer_m27_" + tag + "_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    return dir;
}

void cleanupPreviewFixture(const QString &dir)
{
    TaskScheduler::instance().drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(50);
    QDir(dir).removeRecursively();
}

void testPreviewStableFrame()
{
    printf("\n[4c. selection preview keeps one frame across stages]\n");
    fflush(stdout);
    const QString dir = previewFixtureDir("frame");
    const QString photo = makeSolidImage(dir, "photo_3x2.png", 300, 200, qRgb(200, 30, 30));

    // Test 1: a gallery thumb whose 4:3 EXIF raster has black bars baked in,
    // inside the transparent square padding. With the source size known the
    // frame is 3:2 at once and no bar is painted at the frame edge.
    QImage square(160, 160, QImage::Format_ARGB32);
    square.fill(Qt::transparent);
    for (int y = 20; y < 140; ++y)
        for (int x = 0; x < 160; ++x)
            square.setPixel(x, y, (y < 26 || y >= 134) ? qRgb(0, 0, 0) : qRgb(200, 30, 30));

    PreviewPanel panel;
    panel.resize(320, 280); // image area 300 x 204
    panel.setImage(photo, QPixmap::fromImage(square), QSize(300, 200));
    CHECK(panel.presentationQuality() == PreviewPanel::PresentationQuality::Thumbnail,
          "PASS barred warm thumb is shown synchronously");
    const QSize thumbFrame = panel.fittedPreviewSize();
    CHECK(thumbFrame == QSize(300, 200), "PASS barred thumb gets the 3:2 source frame");
    {
        QImage shot(panel.size(), QImage::Format_ARGB32);
        shot.fill(Qt::white);
        panel.render(&shot);
        const int top = 10 + (204 - 200) / 2;
        const QRgb edge = shot.pixel(160, top + 2);
        const QRgb bottom = shot.pixel(160, top + 200 - 3);
        CHECK(qRed(edge) > 120 && qGreen(edge) < 90 && qRed(bottom) > 120 && qGreen(bottom) < 90,
              "PASS no baked-in black bar is painted at the frame edges");
    }
    const int changesAfterThumb = panel.frameChangeCountForTest();

    // Test 3: thumb -> sharp preview -> stats never move the frame.
    CHECK(waitFor(
              [&]
              { return panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview; },
              15000),
          "PASS sharp preview replaces the thumb");
    CHECK(panel.fittedPreviewSize() == thumbFrame, "PASS frame unchanged by the sharp preview");
    pump(300); // stats delivery
    CHECK(panel.fittedPreviewSize() == thumbFrame &&
              panel.frameChangeCountForTest() == changesAfterThumb,
          "PASS frame unchanged by the stats delivery (no frame change at all)");
    CHECK(panel.sourceImageSize() == QSize(300, 200), "PASS source size reported");

    // Test 7: device pixel ratio 2 rasterizes the frame at 2x device pixels.
    panel.setDevicePixelRatioForTest(2.0);
    const QPixmap hi = panel.scaledPixmapForTest();
    CHECK(panel.fittedPreviewSize() == QSize(300, 200) && hi.size() == QSize(600, 400) &&
              qFuzzyCompare(hi.devicePixelRatio(), 2.0),
          "PASS DPR 2 keeps the logical frame and paints 2x device pixels");
    panel.setDevicePixelRatioForTest(1.0);
    CHECK(panel.scaledPixmapForTest().size() == QSize(300, 200),
          "PASS DPR 1 paints 1x device pixels");

    cleanupPreviewFixture(dir);
}

void testPreviewRapidSwitchAndCache()
{
    printf("\n[4d. rapid A/B switching: stale results ignored, cache hit is sharp]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    const QString dir = previewFixtureDir("switch");
    const QString a = makeSolidImage(dir, "a_landscape.png", 300, 200, qRgb(200, 30, 30));
    const QString b = makeSolidImage(dir, "b_portrait.png", 200, 300, qRgb(30, 30, 200));
    const QPixmap thumbA = solidPixmap(150, 100, qRgb(200, 30, 30));
    const QPixmap thumbB = solidPixmap(100, 150, qRgb(30, 30, 200));
    const QSize frameA(300, 200);
    const QSize frameB(136, 204);

    // Test 4: A -> B -> A -> B behind a busy Decode worker. Only the newest
    // request may present; older generations of either path are dropped.
    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::milliseconds(600)); });
    PreviewPanel panel;
    panel.resize(320, 280);
    panel.setImage(a, thumbA);
    panel.setImage(b, thumbB);
    panel.setImage(a, thumbA);
    panel.setImage(b, thumbB);
    CHECK(panel.presentedPath() == b && panel.fittedPreviewSize() == frameB,
          "PASS newest warm thumb is presented synchronously in its own frame");
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    CHECK(waitFor(
              [&]
              { return panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview; },
              15000),
          "PASS newest request delivers its sharp preview");
    pump(500);
    CHECK(panel.presentedPath() == b && panel.fittedPreviewSize() == frameB &&
              panel.sourceImageSize() == QSize(200, 300),
          "PASS stale A/B results never overwrite the newest request");

    // Test 5: a memory-cached preview with a known source size goes straight
    // to the Preview stage (no thumb flash), still with one Decode refresh.
    panel.setImage(a, thumbA);
    CHECK(waitFor(
              [&]
              {
                  return panel.presentedPath() == a &&
                         panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview;
              },
              15000),
          "PASS A delivers its sharp preview");
    CHECK(panel.fittedPreviewSize() == frameA, "PASS A's frame follows its landscape source");
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(100);
    const auto submittedBefore = sched.metrics(PoolType::DecodePool).submitted;
    panel.setImage(b, thumbB);
    CHECK(panel.presentedPath() == b &&
              panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview &&
              panel.fittedPreviewSize() == frameB && panel.sourceImageSize() == QSize(200, 300),
          "PASS cached B is sharp synchronously, in its final frame");
    CHECK(sched.metrics(PoolType::DecodePool).submitted == submittedBefore + 1,
          "PASS cache hit still submits exactly one Decode refresh");
    const int changes = panel.frameChangeCountForTest();
    pump(500);
    CHECK(panel.presentedPath() == b && panel.fittedPreviewSize() == frameB &&
              panel.frameChangeCountForTest() == changes,
          "PASS the refresh delivery does not move the frame");

    cleanupPreviewFixture(dir);
}

void testPreviewWrongShapePlaceholder()
{
    printf("\n[4e. wrong-shape placeholder corrects the frame exactly once]\n");
    fflush(stdout);
    const QString dir = previewFixtureDir("shape");
    const QString photo = makeSolidImage(dir, "photo_3x2.png", 300, 200, qRgb(200, 30, 30));

    // Test 6: no source size yet and a 4:3 placeholder for a 3:2 photo. The
    // frame starts 4:3, then changes once (animated) to 3:2 and stays there.
    PreviewPanel panel;
    panel.resize(320, 280);
    panel.show();
    pump(30);
    panel.setImage(photo, solidPixmap(160, 120, qRgb(200, 30, 30)));
    CHECK(panel.fittedPreviewSize() == QSize(272, 204),
          "PASS placeholder frame follows the 4:3 thumb");
    const int before = panel.frameChangeCountForTest();
    CHECK(waitFor(
              [&]
              { return panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview; },
              15000),
          "PASS sharp preview arrives");
    pump(400); // cross-fade + stats
    CHECK(panel.fittedPreviewSize() == QSize(300, 200) &&
              panel.frameChangeCountForTest() == before + 1,
          "PASS exactly one frame change to the true 3:2 aspect");
    CHECK(!panel.isTransitioningForTest(), "PASS the stage cross-fade settles");

    // A later path change is instant (no cross-fade between different photos).
    const QString other = makeSolidImage(dir, "other_3x2.png", 300, 200, qRgb(30, 200, 30));
    panel.setImage(other, solidPixmap(150, 100, qRgb(30, 200, 30)), QSize(300, 200));
    CHECK(!panel.isTransitioningForTest() && panel.fittedPreviewSize() == QSize(300, 200),
          "PASS path changes swap instantly in a stable frame");
    panel.hide();

    cleanupPreviewFixture(dir);
}

// ─── 4f. P6: centre viewer provisional keeps the full image's geometry ─────
QRect fullImageScreenRect(const ImageViewer &viewer, const QSize &source)
{
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    viewer.viewTransform().imageRectToScreen(0, 0, source.width(), source.height(), sx, sy, sw, sh);
    return {sx, sy, sw, sh};
}

void testViewerProvisionalStableGeometry()
{
    printf("\n[4f. viewer provisional: no bars, same rect as the full image]\n");
    fflush(stdout);
    const QString dir = previewFixtureDir("viewer_prov");
    const QString photo = makeSolidImage(dir, "photo_3x2.png", 300, 200, qRgb(200, 30, 30));
    const QString other = makeSolidImage(dir, "other_2x3.png", 200, 300, qRgb(30, 30, 200));

    // 160x120 EXIF-style thumb of a 3:2 photo: 3:2 content inside black bars.
    QImage barred(160, 120, QImage::Format_RGB32);
    barred.fill(Qt::black);
    for (int y = 6; y < 113; ++y)
    {
        QRgb *row = reinterpret_cast<QRgb *>(barred.scanLine(y));
        for (int x = 0; x < 160; ++x)
            row[x] = qRgb(200, 30, 30);
    }

    ImageViewer viewer;
    viewer.resize(640, 480);
    viewer.show();
    pump(30);

    // Rapid A -> B -> A warm thumbs, then the real load of A.
    viewer.setProvisionalImage(photo, barred, QSize(300, 200));
    viewer.setProvisionalImage(other, solidPixmap(107, 160, qRgb(30, 30, 200)).toImage(),
                               QSize(200, 300));
    viewer.setProvisionalImage(photo, barred, QSize(300, 200));

    const QImage shown = viewer.provisionalImageForTest();
    CHECK(!shown.isNull() &&
              std::abs(static_cast<double>(shown.width()) / shown.height() - 1.5) < 0.03,
          "PASS provisional pixels are cropped to the source 3:2 aspect");
    CHECK(!shown.isNull() && qRed(shown.pixel(shown.width() / 2, 0)) > 140 &&
              qRed(shown.pixel(shown.width() / 2, shown.height() - 1)) > 140,
          "PASS provisional shows no baked-in black bars");
    const QRect provisionalRect = viewer.provisionalScreenRect();
    CHECK(provisionalRect.width() > provisionalRect.height() && provisionalRect.width() > 0,
          "PASS provisional rect follows the known 3:2 source");

    viewer.setImage(photo);
    const bool loaded = waitFor(
        [&]
        {
            const auto frame = viewer.frame();
            return frame && frame->isValid() && frame->width() == 300 && frame->height() == 200;
        },
        20000);
    CHECK(loaded, "PASS full image of A arrives (stale B never wins)");
    pump(50);
    const QRect fullRect = fullImageScreenRect(viewer, QSize(300, 200));
    CHECK(qAbs(fullRect.x() - provisionalRect.x()) <= 1 &&
              qAbs(fullRect.y() - provisionalRect.y()) <= 1 &&
              qAbs(fullRect.width() - provisionalRect.width()) <= 1 &&
              qAbs(fullRect.height() - provisionalRect.height()) <= 1,
          "PASS full image lands in exactly the provisional rect (no small-then-big)");

    // A late warm thumb for the image already shown sharp is stale.
    const double scale = viewer.viewTransform().scale;
    const qint64 provisionalBefore = viewer.provisionalImageForTest().cacheKey();
    viewer.setProvisionalImage(photo, solidPixmap(160, 120, qRgb(200, 30, 30)).toImage(), QSize());
    CHECK(viewer.provisionalImageForTest().cacheKey() == provisionalBefore,
          "PASS late same-path warm thumb is ignored once the full image is shown");
    CHECK(std::abs(viewer.viewTransform().scale - scale) < 1e-9,
          "PASS late warm thumb does not re-fit the sharp image");

    // A wildly wrong-shaped thumb is centered, never stretched.
    viewer.setProvisionalImage(other, solidPixmap(160, 40, qRgb(30, 30, 200)).toImage(),
                               QSize(200, 300));
    const QImage padded = viewer.provisionalImageForTest();
    CHECK(!padded.isNull() &&
              std::abs(static_cast<double>(padded.width()) / padded.height() - 2.0 / 3.0) < 0.03,
          "PASS wrong-shape thumb is placed on a canvas of the source aspect");
    CHECK(!padded.isNull() && qAlpha(padded.pixel(0, 0)) == 0,
          "PASS wrong-shape thumb is padded transparently, not stretched");

    viewer.close();
    cleanupPreviewFixture(dir);
}

} // namespace

void runM27PreviewFrameTests(int &pass, int &fail)
{
    testPreviewStableFrame();
    testPreviewRapidSwitchAndCache();
    testPreviewWrongShapePlaceholder();
    testViewerProvisionalStableGeometry();
    pass += g_pass;
    fail += g_fail;
}
