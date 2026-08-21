// M24 Phase 4B — Compare workflow acceptance tests.
//
// Maps the M24 Workflow B acceptance items not covered elsewhere:
//   B#1  enter Compare with 2 / 4 / 8 images from a selection
//   B#2  layout / focus / reference-image clarity (n-up presets)
//   B#4  Blink/Swipe/Overlay/Diff mode switches preserve zoom + ROI state
//   B#6  continuous Next/Prev pair navigation keeps the user's mode
//   B#7  mismatched resolutions and corrupt images degrade cleanly
//   B#8  controls unavailable for the current layout explain why (tooltip)
//
// (2-image B/S/O/K mode semantics, Space, lock-reference, Esc exit and report
// bundles are already covered by workflow_ux_tests / compare_session_tests.)

#include "compareworkspace.h"
#include "core/compare/CompareEngine.h"
#include "core/perf/MemoryTracker.h"
#include "core/scheduler/TaskScheduler.h"
#include "selectionmodel.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

void pump(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

// M28 P1-01: Compare loads are asynchronous - pump until the engine reports the
// requested frame count (or the timeout expires). Deterministic waits replace
// bare pump() calls that could race the async delivery.
void waitForCompareCount(CompareWorkspace *ws, int expected, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (ws->comparedImageCount() != expected && t.elapsed() < timeoutMs)
        pump(25);
}

// Same-count reloads must wait for the terminal UI state as well as the engine
// count. A newer setImages() briefly keeps the old frames in the engine while
// the dedicated loading page is current, so the old count alone can satisfy a
// wait before finishLoad() has restored the grid.
void waitForCompareLoadFinished(CompareWorkspace *ws, int expected, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        QWidget *loading = ws->findChild<QWidget *>(QStringLiteral("compareLoadingPage"));
        QWidget *grid = ws->findChild<QWidget *>(QStringLiteral("compareGridPage"));
        if (ws->comparedImageCount() == expected && loading && !loading->isVisible() && grid &&
            grid->isVisible())
            return;
        pump(25);
    }
}

void waitForCompareAtLeast(CompareWorkspace *ws, int minimum, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (ws->comparedImageCount() < minimum && t.elapsed() < timeoutMs)
        pump(25);
}

// P1 (async histogram contract): wait until a histogram widget carries at least
// one non-empty histogram. Polls widget state while pumping events so queued
// Analysis delivery reaches the UI; never uses fixed sleeps as correctness
// synchronization.
void waitForHistogramPopulated(CompareWorkspace *ws, const QString &objectName,
                               int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        HistogramWidget *hw = ws->findChild<HistogramWidget *>(objectName);
        if (hw && hw->histogramCount() > 0 && hw->histogramTotal(0) > 0)
            return;
        pump(25);
    }
}

// P1 (async histogram contract): wait until a histogram widget's first entry
// reports exactly the expected pixel total (e.g. an ROI-restricted 32x32).
void waitForHistogramTotal(CompareWorkspace *ws, const QString &objectName, long expectedTotal,
                           int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        HistogramWidget *hw = ws->findChild<HistogramWidget *>(objectName);
        if (hw && hw->histogramCount() > 0 && hw->histogramTotal(0) == expectedTotal)
            return;
        pump(25);
    }
}

QString writePng(const QDir &dir, const QString &name, int w, int h, QColor c)
{
    const QString path = dir.filePath(name);
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    img.save(path, "PNG");
    return path;
}

QColor highFrequencyPixel(int x, int y)
{
    // Three-channel, one-pixel-period fixture: every source coordinate carries
    // a deterministic value, so a display LOD sample cannot masquerade as the
    // source pixel. The formula is intentionally not a flat fill or a smooth
    // low-frequency gradient.
    return QColor((x * 37 + y * 17 + 11) & 0xFF, (x * 13 + y * 53 + 29) & 0xFF,
                  (x * 71 + y * 7 + 43) & 0xFF);
}

QString writeHighFrequencyPng(const QDir &dir, const QString &name, int w, int h)
{
    const QString path = dir.filePath(name);
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, highFrequencyPixel(x, y));
    img.save(path, "PNG");
    return path;
}

double highFrequencyNeighborhoodMean(int cx, int cy, int kernel)
{
    long long sum = 0;
    int count = 0;
    const int half = kernel / 2;
    for (int dy = -half; dy <= half; ++dy)
    {
        for (int dx = -half; dx <= half; ++dx)
        {
            const QColor pixel = highFrequencyPixel(cx + dx, cy + dy);
            sum += static_cast<int>(0.2126 * pixel.red() + 0.7152 * pixel.green() +
                                    0.0722 * pixel.blue() + 0.5);
            ++count;
        }
    }
    return static_cast<double>(sum) / count;
}

QCheckBox *findChk(QWidget *root, const QString &textPrefix)
{
    const auto boxes = root->findChildren<QCheckBox *>();
    for (QCheckBox *c : boxes)
        if (c->text().startsWith(textPrefix))
            return c;
    return nullptr;
}

// ─── B#1/B#2/B#8: 4-image and 8-image entry + layout-aware enablement ───────
#include "test_compare_acceptance_cases.inc"
int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    QDir dir(tmp.filePath("cmp"));
    dir.mkpath(".");
    QStringList paths8;
    for (int i = 0; i < 8; ++i)
    {
        paths8 << writePng(dir, QString("cmp_%1.png").arg(i), 160 + i * 8, 120 + i * 4,
                           QColor(20 * i, 40, 255 - 20 * i));
    }
    const QStringList paths6 = paths8.mid(0, 6);

    testMultiImageEntry(paths8);
    testModePreservesState(paths8[0], paths8[1]);
    testContinuousNav(paths6);
    testPaneHistogramConsistency(paths8[0], paths8[1]);
    testDegradedImages(dir);
    testCompareLoadIsAsync(dir);
    testLargeCompareDisplayBounded(dir);
    testInspectorUsesFullResolutionSource(dir);
    testCompareLoadCancellation(dir);
    testInspectorCoalescing(paths8[0], paths8[1]);

    if (g_failures > 0)
    {
        std::cout << "compare_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "compare_acceptance_tests: PASS\n";
    return 0;
}
