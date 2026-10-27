// M27 4g-4l: selection-preview identity switch (no stale A pixels while B
// decodes), small-thumbnail deferral, stable stats text, cross-path aspect
// tolerance, the provider's source-size registry and the centre viewer's
// cross-path stand-in expiry. Part of m27_lifetime_tests; runs from
// test_m27_lifetime.cpp's main via runM27PreviewIdentityTests().

#include "imageviewer.h"
#include "previewpanel.h"
#include "thumbnailprovider.h"

#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>

#include <chrono>
#include <cstdio>

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

using PoolType = TaskScheduler::PoolType;
using Quality = PreviewPanel::PresentationQuality;

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
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

QString makeSolidImage(const QString &dir, const QString &name, int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    const QString path = dir + "/" + name;
    img.save(path, "PNG");
    return path;
}

QString fixtureDir()
{
    const QString dir = QDir::tempPath() + "/mviewer_m27_identity_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    return dir;
}

QString missing(const char *name)
{
    return QStringLiteral("C:/definitely/missing/identity/") + QString::fromLatin1(name);
}

// Panel 320x280: the well is 300 x 204 (10 px pad, 56 px text block), so a
// 3:2 frame is 300x200 and a 2:3 frame is 136x204.
constexpr int kPanelW = 320;
constexpr int kPanelH = 280;
const QSize kLandscapeFrame(300, 200);
const QSize kPortraitFrame(136, 204);

// ─── 4g. A -> B with no pixels: B's identity in A's frame, never A ─────────
void testIdentityPlaceholder()
{
    printf("\n[4g. identity switch: B's name replaces A within the grace]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.pause(PoolType::DecodePool);
    {
        PreviewPanel panel;
        panel.resize(kPanelW, kPanelH);
        const QString a = missing("a.jpg");
        const QString b = missing("b.jpg");
        const QString c = missing("c.jpg");

        // Empty panel: nothing to protect, the placeholder shows at once.
        panel.setImage(a);
        CHECK(panel.presentedPath() == a && panel.presentationQuality() == Quality::Placeholder,
              "PASS empty panel shows the new identity immediately");
        CHECK(!panel.hasImage() && panel.currentPixmap().isNull(),
              "PASS placeholder carries no pixels");

        panel.setImage(a, solidPixmap(150, 100, qRgb(200, 40, 40)), QSize(3000, 2000));
        CHECK(panel.presentationQuality() == Quality::Thumbnail &&
                  panel.fittedPreviewSize() == kLandscapeFrame,
              "PASS A's thumbnail is presented in a 3:2 frame");
        const int changes = panel.frameChangeCountForTest();

        panel.setImage(b);
        CHECK(panel.identityGracePendingForTest(), "PASS switching to B arms the identity grace");
        pump(PreviewPanel::kIdentityGraceMs + 80);
        CHECK(!panel.identityGracePendingForTest(), "PASS grace expires");
        CHECK(panel.presentedPath() == b, "PASS B's identity replaces A after the grace");
        CHECK(panel.presentationQuality() == Quality::Placeholder && !panel.hasImage() &&
                  panel.currentPixmap().isNull(),
              "PASS no A pixels remain while B decodes");
        CHECK(panel.fittedPreviewSize() == kLandscapeFrame &&
                  panel.frameChangeCountForTest() == changes,
              "PASS unknown B size keeps A's frame (no jump)");
        CHECK(panel.sourceImageSize().isEmpty(), "PASS B's dimensions are not invented");
        CHECK(!panel.previewStatsKnownForTest(), "PASS stats show the dash, not A's values");

        // Known size: the placeholder adopts B's real frame.
        panel.setImage(c, QPixmap(), QSize(2000, 3000), 4096);
        pump(PreviewPanel::kIdentityGraceMs + 80);
        CHECK(panel.presentedPath() == c && panel.presentationQuality() == Quality::Placeholder,
              "PASS known-size placeholder after the grace");
        CHECK(panel.fittedPreviewSize() == kPortraitFrame,
              "PASS known-size placeholder uses the real 2:3 frame");
        CHECK(panel.sourceImageSize() == QSize(2000, 3000),
              "PASS known-size placeholder reports the real dimensions");
    }
    sched.resume(PoolType::DecodePool);
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(50);
}

// ─── 4h. A tiny thumbnail waits for the sharp decode (up to the grace) ─────
void testSmallThumbnailDeferral()
{
    printf("\n[4h. small thumbnail is deferred while the sharp decode runs]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.pause(PoolType::DecodePool);
    {
        PreviewPanel panel;
        panel.resize(kPanelW, kPanelH);
        panel.setDevicePixelRatioForTest(1.0);
        const QString a = missing("defer-a.jpg");
        const QString b = missing("defer-b.jpg");
        const QString c = missing("defer-c.jpg");
        panel.setImage(a, solidPixmap(150, 100, qRgb(30, 30, 200)), QSize(3000, 2000));

        // 40x60 against a 204 px frame: long edge under half -> deferred.
        panel.setImage(b, solidPixmap(40, 60, qRgb(30, 200, 30)), QSize(2000, 3000));
        CHECK(panel.presentedPath() == a && panel.identityGracePendingForTest(),
              "PASS tiny thumbnail is held back during the grace");
        pump(PreviewPanel::kIdentityGraceMs + 80);
        CHECK(panel.presentedPath() == b && panel.presentationQuality() == Quality::Thumbnail &&
                  panel.hasImage(),
              "PASS deferred thumbnail is shown once the grace ends");
        CHECK(panel.fittedPreviewSize() == kPortraitFrame,
              "PASS deferred thumbnail uses the real frame");

        // Late gallery thumbnail (offer) for a placeholder: same rule.
        panel.setImage(c, QPixmap(), QSize(3000, 2000));
        pump(PreviewPanel::kIdentityGraceMs + 80);
        CHECK(panel.presentationQuality() == Quality::Placeholder, "PASS C starts as placeholder");
        panel.offerWarmThumbnail(c, solidPixmap(60, 40, qRgb(200, 200, 30)), QSize(3000, 2000));
        CHECK(!panel.hasImage() && panel.identityGracePendingForTest(),
              "PASS tiny late thumbnail waits for the sharp decode");
        pump(PreviewPanel::kIdentityGraceMs + 80);
        CHECK(panel.hasImage() && panel.presentationQuality() == Quality::Thumbnail,
              "PASS tiny late thumbnail shows when the decode is slow");

        // A large-enough thumbnail is never deferred.
        panel.setImage(a, solidPixmap(240, 160, qRgb(10, 10, 10)), QSize(3000, 2000));
        CHECK(panel.presentedPath() == a && panel.presentationQuality() == Quality::Thumbnail,
              "PASS a sharp-enough thumbnail is shown at once");
    }
    sched.resume(PoolType::DecodePool);
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(50);
}

// ─── 4i. Stats line: dash until B's own statistics arrive ──────────────────
void testStatsStayUnknownUntilDelivered(const QString &dir)
{
    printf("\n[4i. stats text stays a dash until the new path's stats arrive]\n");
    fflush(stdout);
    const QString a = makeSolidImage(dir, "stats_a.png", 300, 200, qRgb(90, 120, 150));
    const QString b = makeSolidImage(dir, "stats_b.png", 200, 300, qRgb(150, 120, 90));
    PreviewPanel panel;
    panel.resize(kPanelW, kPanelH);
    panel.setImage(a);
    CHECK(waitFor([&] { return panel.presentationQuality() == Quality::Preview; }, 20000),
          "PASS A's sharp preview delivered");
    CHECK(waitFor([&] { return panel.previewStatsKnownForTest(); }, 5000),
          "PASS A's stats become known");
    panel.setImage(b, solidPixmap(200, 300, qRgb(150, 120, 90)), QSize(200, 300));
    CHECK(panel.presentedPath() == b && !panel.previewStatsKnownForTest(),
          "PASS switching to B drops A's stats immediately");
    CHECK(waitFor([&] { return panel.presentationQuality() == Quality::Preview; }, 20000),
          "PASS B's sharp preview delivered");
    CHECK(waitFor([&] { return panel.previewStatsKnownForTest(); }, 5000),
          "PASS B's stats become known");
    CHECK(panel.sourceImageSize() == QSize(200, 300), "PASS B's dimensions are reported");
}

// ─── 4j. Thumbnail-derived aspect: ~1.5% tolerance across paths ────────────
void testCrossPathAspectTolerance()
{
    printf("\n[4j. thumbnail-derived aspect keeps the frame across paths]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.pause(PoolType::DecodePool);
    {
        PreviewPanel panel;
        panel.resize(kPanelW, kPanelH);
        panel.setImage(missing("tol-a.jpg"), solidPixmap(150, 100, qRgb(1, 2, 3)),
                       QSize(3000, 2000));
        const int changes = panel.frameChangeCountForTest();
        // 160x107 = 1.495 (0.3% off 1.5), size unknown -> approximate aspect.
        panel.setImage(missing("tol-b.jpg"), solidPixmap(160, 107, qRgb(4, 5, 6)));
        CHECK(panel.presentationQuality() == Quality::Thumbnail,
              "PASS approximate thumbnail presented");
        CHECK(panel.fittedPreviewSize() == kLandscapeFrame &&
                  panel.frameChangeCountForTest() == changes,
              "PASS rounding-level aspect drift does not move the frame");
        // An exact, genuinely different size still reshapes the frame.
        panel.setImage(missing("tol-c.jpg"), solidPixmap(160, 107, qRgb(7, 8, 9)),
                       QSize(3000, 2010));
        CHECK(panel.frameChangeCountForTest() == changes + 1,
              "PASS an exact source size is honoured");
    }
    sched.resume(PoolType::DecodePool);
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(50);
}

// ─── 4k. Provider registry: produce() learns the real source size ──────────
void testProviderSourceSizeRegistry(const QString &dir)
{
    printf("\n[4k. thumbnail provider remembers the real source size]\n");
    fflush(stdout);
    const QString path = makeSolidImage(dir, "registry.png", 300, 200, qRgb(60, 60, 60));
    const std::string sp = path.toStdString();
    ThumbnailProvider::invalidateSource(sp);
    CHECK(!ThumbnailProvider::knownSourceSize(path).isValid(), "PASS unknown before produce");
    const auto thumb = ThumbnailProvider::produce(sp, 128);
    CHECK(!thumb.isNull(), "PASS produce returns a thumbnail");
    CHECK(ThumbnailProvider::knownSourceSize(path) == QSize(300, 200),
          "PASS produce records the source display size, not the thumb size");
    ThumbnailProvider::invalidateSource(sp);
    CHECK(!ThumbnailProvider::knownSourceSize(path).isValid(), "PASS invalidate forgets it");

    const QString manual = missing("manual.jpg");
    ThumbnailProvider::rememberSourceSize(manual, QSize());
    CHECK(!ThumbnailProvider::knownSourceSize(manual).isValid(), "PASS empty sizes are ignored");
    ThumbnailProvider::rememberSourceSize(manual, QSize(3072, 4096));
    CHECK(ThumbnailProvider::knownSourceSize(manual) == QSize(3072, 4096),
          "PASS remembered size is returned");
    ThumbnailProvider::invalidateSource(manual.toStdString());
}

// ─── 4l. Viewer: A's snapshot must not stand in for B indefinitely ─────────
void testViewerStandInExpires(const QString &dir)
{
    printf("\n[4l. viewer drops A's stand-in for a different path]\n");
    fflush(stdout);
    const QString a = makeSolidImage(dir, "viewer_a.png", 800, 600, qRgb(200, 0, 0));
    ImageViewer viewer;
    viewer.setFixedSize(320, 240);
    viewer.show();
    pump(30);
    bool ready = false;
    QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                     [&](std::shared_ptr<ImageFrame>) { ready = true; });
    viewer.setImage(a);
    CHECK(waitFor([&] { return ready; }, 20000), "PASS viewer loads A");

    const QString next = dir + "/viewer_next_missing.png";
    viewer.setImage(next);
    CHECK(!viewer.transitionSnapshotSize().isEmpty(),
          "PASS the switch still snapshots A synchronously");
    pump(ImageViewer::kStandInGraceMs + 150);
    CHECK(viewer.transitionSnapshotSize().isEmpty(),
          "PASS A's snapshot is gone shortly after the switch");

    ready = false;
    viewer.setImage(a);
    CHECK(waitFor([&] { return ready; }, 20000), "PASS viewer reloads A");
    const QString late = dir + "/viewer_late_missing.png";
    viewer.setImage(late);
    QImage warm(160, 120, QImage::Format_RGB32);
    warm.fill(qRgb(0, 200, 0));
    viewer.setProvisionalImage(late, warm, QSize(1600, 1200));
    CHECK(!viewer.provisionalImageForTest().isNull(),
          "PASS a late gallery thumbnail becomes the new path's provisional");
    pump(50);
}

} // namespace

void runM27PreviewIdentityTests(int &pass, int &fail)
{
    const QString dir = fixtureDir();
    testIdentityPlaceholder();
    testSmallThumbnailDeferral();
    testStatsStayUnknownUntilDelivered(dir);
    testCrossPathAspectTolerance();
    testProviderSourceSizeRegistry(dir);
    testViewerStandInExpires(dir);
    TaskScheduler::instance().drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(50);
    QDir(dir).removeRecursively();
    pass += g_pass;
    fail += g_fail;
}
