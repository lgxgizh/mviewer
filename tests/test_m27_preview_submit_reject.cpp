// PreviewPanel Decode submit rejection.
// A paused DecodePool refuses the submit (nullptr, backpressure_rejected).
// The panel must retry only the current generation, show a preview when the
// pool accepts again, and otherwise reach a visible terminal state with the
// retry timer stopped. A preview already on screen stays up.
// Part of m27_lifetime_tests. Not executed in this change (CI only).

#include "previewpanel.h"

#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

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

using Pool = TaskScheduler::PoolType;
using Quality = PreviewPanel::PresentationQuality;

constexpr int kQuietMs = (PreviewPanel::kSubmitRetryCap + 2) * PreviewPanel::kSubmitRetryDelayMs;

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

struct ResumeDecode
{
    ~ResumeDecode()
    {
        TaskScheduler::instance().resume(Pool::DecodePool);
    }
};

QString fixtureDir()
{
    const QString dir = QDir::tempPath() + "/mviewer_m27_preview_reject_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    return dir;
}

QString savePng(const QString &dir, const QString &name, int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    const QString path = dir + "/" + name;
    return img.save(path, "PNG") ? path : QString();
}

QImage renderPanel(PreviewPanel &panel)
{
    QImage canvas(panel.size(), QImage::Format_RGB32);
    canvas.fill(Qt::white);
    panel.render(&canvas);
    return canvas;
}

bool isPreview(const PreviewPanel &panel, const QString &path, const QSize &source)
{
    return panel.presentedPath() == path && panel.presentationQuality() == Quality::Preview &&
           panel.hasImage() && panel.sourceImageSize() == source && !panel.currentPixmap().isNull();
}

void testCapacityResumes(const QString &pathA, const QSize &sizeA)
{
    printf("\n[reject: paused submit recovers when Decode accepts]\n");
    fflush(stdout);
    ResumeDecode resume;
    auto &sched = TaskScheduler::instance();
    sched.pause(Pool::DecodePool);
    PreviewPanel panel;
    panel.resize(320, 240);
    const auto rejected0 = sched.metrics(Pool::DecodePool).backpressure_rejected;
    const auto submitted0 = sched.metrics(Pool::DecodePool).submitted;
    panel.setImage(pathA);
    CHECK(panel.presentationQuality() == Quality::Placeholder && !panel.hasImage(),
          "PASS first reject shows the placeholder, not a preview");
    CHECK(panel.previewSubmitAttemptsForTest() == 1, "PASS the refused submit was attempted");
    CHECK(sched.metrics(Pool::DecodePool).backpressure_rejected == rejected0 + 1,
          "PASS the scheduler rejected the first submit");
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0,
          "PASS a refused submit is not counted as accepted");
    CHECK(panel.previewSubmitRetryPendingForTest(), "PASS a delayed retry is armed");
    CHECK(panel.previewSubmitFailureTextForTest().isEmpty(),
          "PASS the failure sentence waits until the budget is spent");

    sched.resume(Pool::DecodePool);
    CHECK(waitFor([&] { return isPreview(panel, pathA, sizeA); }, 15000),
          "PASS the delayed retry delivers A's preview");
    CHECK(panel.previewSubmitAttemptsForTest() == 2,
          "PASS exactly one retry was issued after capacity returned");
    CHECK(!panel.previewSubmitRetryPendingForTest(), "PASS the timer is idle after accept");
    CHECK(!panel.previewSubmitFailedForTest() && panel.previewSubmitFailureTextForTest().isEmpty(),
          "PASS a recovered preview is not a failure");
    const int attempts = panel.previewSubmitAttemptsForTest();
    const auto submitted = sched.metrics(Pool::DecodePool).submitted;
    pump(kQuietMs);
    CHECK(panel.previewSubmitAttemptsForTest() == attempts &&
              sched.metrics(Pool::DecodePool).submitted == submitted &&
              isPreview(panel, pathA, sizeA),
          "PASS no further submit after the preview lands");
}

void testObsoleteGeneration(const QString &pathA, const QString &pathB, const QSize &sizeA)
{
    printf("\n[reject: A->B->A does not issue or apply an obsolete retry]\n");
    fflush(stdout);
    ResumeDecode resume;
    auto &sched = TaskScheduler::instance();
    sched.pause(Pool::DecodePool);
    PreviewPanel panel;
    panel.resize(320, 240);
    const auto submitted0 = sched.metrics(Pool::DecodePool).submitted;
    const auto rejected0 = sched.metrics(Pool::DecodePool).backpressure_rejected;
    // No processEvents between these. Each call retires the previous timer
    // before the next generation can arm one.
    panel.setImage(pathA);
    panel.setImage(pathB);
    panel.setImage(pathA);
    CHECK(panel.previewSubmitAttemptsForTest() == 3,
          "PASS A, B, and A each submitted once while paused");
    CHECK(panel.previewSubmitRetryPendingForTest(), "PASS only the latest generation is armed");
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0,
          "PASS none of the three were accepted");
    CHECK(sched.metrics(Pool::DecodePool).backpressure_rejected == rejected0 + 3,
          "PASS each generation was a real scheduler reject");

    sched.resume(Pool::DecodePool);
    CHECK(waitFor([&] { return isPreview(panel, pathA, sizeA); }, 15000),
          "PASS the surviving retry presents A, not B");
    CHECK(panel.previewSubmitAttemptsForTest() == 4,
          "PASS obsolete generations did not submit again");
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0 + 1,
          "PASS exactly one Decode task was accepted");
    CHECK(panel.sourceImageSize() == sizeA, "PASS the applied pixels are A's source size");
    const auto submitted = sched.metrics(Pool::DecodePool).submitted;
    pump(kQuietMs);
    CHECK(panel.previewSubmitAttemptsForTest() == 4 &&
              sched.metrics(Pool::DecodePool).submitted == submitted &&
              isPreview(panel, pathA, sizeA),
          "PASS a retired generation does not submit after A is shown");
}

void testPermanentReject(const QString &pathA)
{
    printf("\n[reject: a lasting refusal reaches a visible terminal state]\n");
    fflush(stdout);
    ResumeDecode resume;
    auto &sched = TaskScheduler::instance();
    sched.pause(Pool::DecodePool);
    PreviewPanel panel;
    panel.resize(320, 240);
    const auto submitted0 = sched.metrics(Pool::DecodePool).submitted;
    const auto rejected0 = sched.metrics(Pool::DecodePool).backpressure_rejected;
    panel.setImage(pathA);
    const QImage before = renderPanel(panel);
    const int budget = 1 + PreviewPanel::kSubmitRetryCap;
    CHECK(waitFor(
              [&]
              {
                  return panel.previewSubmitAttemptsForTest() == budget &&
                         !panel.previewSubmitRetryPendingForTest() &&
                         panel.previewSubmitFailedForTest();
              },
              2000),
          "PASS the budget ends with the timer inactive");
    CHECK(panel.presentationQuality() == Quality::Placeholder && !panel.hasImage() &&
              panel.previewPixelSize().isEmpty() && panel.sourceImageSize().isEmpty(),
          "PASS the terminal well still has no invented pixels or size");
    CHECK(panel.previewSubmitFailureTextForTest() == PreviewPanel::tr("Unable to load preview"),
          "PASS the terminal sentence is the translatable failure text");
    const QImage after = renderPanel(panel);
    CHECK(before.size() == after.size() && before != after,
          "PASS the terminal failure changes what the panel paints");
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0,
          "PASS every permanent-reject attempt was refused");
    CHECK(sched.metrics(Pool::DecodePool).backpressure_rejected == rejected0 + budget,
          "PASS the scheduler refused each attempt in the budget");
    pump(kQuietMs);
    CHECK(panel.previewSubmitAttemptsForTest() == budget &&
              !panel.previewSubmitRetryPendingForTest() && panel.previewSubmitFailedForTest(),
          "PASS a spent budget does not keep polling while paused");

    sched.resume(Pool::DecodePool);
    pump(kQuietMs);
    CHECK(panel.previewSubmitAttemptsForTest() == budget &&
              sched.metrics(Pool::DecodePool).submitted == submitted0 &&
              panel.presentationQuality() == Quality::Placeholder &&
              panel.previewSubmitFailureTextForTest() == PreviewPanel::tr("Unable to load preview"),
          "PASS resume after the budget does not start a late decode");
    const auto m = sched.metrics(Pool::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0, "PASS the Decode pool stays idle");
}

void testDestroyWhileRetryArmed(const QString &pathA)
{
    printf("\n[reject: destroying the panel cancels the retry]\n");
    fflush(stdout);
    ResumeDecode resume;
    auto &sched = TaskScheduler::instance();
    sched.pause(Pool::DecodePool);
    const auto submitted0 = sched.metrics(Pool::DecodePool).submitted;
    auto rejected = sched.metrics(Pool::DecodePool).backpressure_rejected;
    {
        PreviewPanel panel;
        panel.resize(320, 240);
        panel.setImage(pathA);
        CHECK(panel.previewSubmitRetryPendingForTest(), "PASS the timer is armed before destroy");
        rejected = sched.metrics(Pool::DecodePool).backpressure_rejected;
    }
    pump(kQuietMs);
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0 &&
              sched.metrics(Pool::DecodePool).backpressure_rejected == rejected,
          "PASS a destroyed panel does not submit while the pool is paused");
    sched.resume(Pool::DecodePool);
    pump(kQuietMs);
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0 &&
              sched.metrics(Pool::DecodePool).backpressure_rejected == rejected,
          "PASS a destroyed panel does not submit after the pool resumes");
    const auto m = sched.metrics(Pool::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0, "PASS destroy leaves the Decode pool idle");
}

void testCachedPreviewKept(const QString &pathA, const QString &pathB, const QSize &sizeA,
                           const QSize &sizeB)
{
    printf("\n[reject: a visible cached preview survives a refused refresh]\n");
    fflush(stdout);
    ResumeDecode resume;
    auto &sched = TaskScheduler::instance();
    sched.resume(Pool::DecodePool);
    PreviewPanel panel;
    panel.resize(320, 240);
    panel.setImage(pathA);
    CHECK(waitFor([&]
                  { return isPreview(panel, pathA, sizeA) && panel.previewStatsKnownForTest(); },
                  15000),
          "PASS A is previewed before the refresh is refused");
    panel.setImage(pathB);
    CHECK(waitFor([&]
                  { return isPreview(panel, pathB, sizeB) && panel.previewStatsKnownForTest(); },
                  15000),
          "PASS B is previewed so A's cache is not the current stage");
    CHECK(panel.previewSubmitAttemptsForTest() == 2, "PASS both previews were accepted once");

    sched.pause(Pool::DecodePool);
    const auto submitted0 = sched.metrics(Pool::DecodePool).submitted;
    const int attempts0 = panel.previewSubmitAttemptsForTest();
    panel.setImage(pathA);
    CHECK(isPreview(panel, pathA, sizeA), "PASS A's cached preview is shown before the worker");
    const qint64 key = panel.currentPixmap().cacheKey();
    CHECK(!panel.previewSubmitFailedForTest(), "PASS the synchronous cache hit is not a failure");
    const int budget = attempts0 + 1 + PreviewPanel::kSubmitRetryCap;
    CHECK(waitFor(
              [&]
              {
                  return panel.previewSubmitAttemptsForTest() == budget &&
                         !panel.previewSubmitRetryPendingForTest();
              },
              2000),
          "PASS the refused refresh spends its budget");
    CHECK(isPreview(panel, pathA, sizeA) && panel.currentPixmap().cacheKey() == key,
          "PASS the cached preview stays on screen after the budget");
    CHECK(!panel.previewSubmitFailedForTest() && panel.previewSubmitFailureTextForTest().isEmpty(),
          "PASS a kept preview is not replaced with the failure sentence");
    CHECK(sched.metrics(Pool::DecodePool).submitted == submitted0,
          "PASS the refresh attempts were refused");
    sched.resume(Pool::DecodePool);
    pump(kQuietMs);
    CHECK(panel.previewSubmitAttemptsForTest() == budget &&
              sched.metrics(Pool::DecodePool).submitted == submitted0 &&
              isPreview(panel, pathA, sizeA) && panel.currentPixmap().cacheKey() == key,
          "PASS resume does not replace the kept preview");
}

} // namespace

void runM27PreviewSubmitRejectTests(int &pass, int &fail)
{
    printf("\n[preview submit rejection: bounded retry or visible failure]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.resume(Pool::DecodePool);
    sched.drain(Pool::DecodePool, std::chrono::seconds(15));
    pump(30);

    const QString dir = fixtureDir();
    const QSize sizeA(640, 400);
    const QSize sizeB(480, 720);
    const QString pathA = savePng(dir, "a.png", sizeA.width(), sizeA.height(), qRgb(20, 80, 140));
    const QString pathB = savePng(dir, "b.png", sizeB.width(), sizeB.height(), qRgb(140, 80, 20));
    CHECK(!pathA.isEmpty() && !pathB.isEmpty(), "PASS reject fixtures written");
    if (!pathA.isEmpty() && !pathB.isEmpty())
    {
        testCapacityResumes(pathA, sizeA);
        sched.drain(Pool::DecodePool, std::chrono::seconds(15));
        pump(30);
        testObsoleteGeneration(pathA, pathB, sizeA);
        sched.drain(Pool::DecodePool, std::chrono::seconds(15));
        pump(30);
        testPermanentReject(pathA);
        testDestroyWhileRetryArmed(pathA);
        testCachedPreviewKept(pathA, pathB, sizeA, sizeB);
        sched.drain(Pool::DecodePool, std::chrono::seconds(15));
        pump(30);
    }
    sched.resume(Pool::DecodePool);
    QDir(dir).removeRecursively();
    pass += g_pass;
    fail += g_fail;
}
