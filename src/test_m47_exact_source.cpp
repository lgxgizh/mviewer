// M47 Phase 4 — exact-source consumers stay full-resolution with source-backed
// panes present.
//
// Phase 3 added source-backed (LOD) panes for infeasible sources. This suite
// proves the analysis consumers NEVER consume the display LOD and never
// trigger implicit materialization:
//   E1 Pixel Inspector with a mixed feasible+infeasible set: the feasible
//      pane's row reports the EXACT full-resolution source pixel (proven
//      different from the display LOD sample), and the infeasible pane's row
//      reports 无效 — the Inspector does not fall back to sampling the LOD
//      raster, even though that pane's engine frame holds only metadata.
//   E2 Diff/PSNR/SSIM on an all-infeasible pair: the batch completes without
//      any decode (zero NativeLod/FullDecode* activity), metrics stay "—", the
//      panes keep their LOD display, and the scheduler drains. Diff needs
//      full-resolution analysis sources; without them it degrades, never
//      materializes.
//   E3 Report export on a mixed set: the bundle carries both panes in the
//      REQUESTED order (placeholder metadata included), the placeholder pair
//      is recorded non-comparable (psnr/ssim stay 0), and no full decode was
//      ever attempted for the infeasible source.
//
// (The standalone Analyzer pipeline consumes ImageFrame pixels and is already
// null-safe by contract — analyzer_pipeline_tests / analyze_acceptance_tests
// cover null/invalid inputs; this suite covers the Compare-side consumers.)

#include "compareworkspace.h"
#include "core/analysis/ExportReport.h"
#include "core/compare/CompareEngine.h"
#include "core/image/ImageFrame.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;

#define CHECK(c, m)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (!(c))                                                                                   \
        {                                                                                           \
            std::printf("FAIL: %s\n", m);                                                           \
            std::fflush(stdout);                                                                    \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

#define MARK(t)                                                                                     \
    do                                                                                              \
    {                                                                                               \
        std::printf("%s\n", t);                                                                     \
        std::fflush(stdout);                                                                        \
    } while (false)

namespace
{

using mviewer::core::SourceDecodeStats;

std::string fixtureRoot()
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large";
}

std::string fixture(const char *name)
{
    return fixtureRoot() + "/" + name;
}

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

RawImageView *paneView(CompareWorkspace *ws, int index)
{
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (v && v->cellIndex() == index)
            return v;
    }
    return nullptr;
}

bool waitForLoadedPanes(CompareWorkspace *ws, int expected, const std::vector<int> &panes,
                        int timeoutMs = 90000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto panesReady = [&]()
    {
        for (int idx : panes)
        {
            RawImageView *v = paneView(ws, idx);
            if (!v || v->image().isNull())
                return false;
        }
        return true;
    };
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (ws->comparedImageCount() == expected && panesReady())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return ws->comparedImageCount() == expected && panesReady();
}

// Adversarial fixture: every source coordinate carries a deterministic value
// that varies at one-pixel period, so a display LOD sample can never masquerade
// as the exact source pixel.
QColor highFrequencyPixel(int x, int y)
{
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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!QDir(QString::fromStdString(fixtureRoot())).exists())
    {
        std::printf("fixture root missing: %s\n", fixtureRoot().c_str());
        std::printf("run: python testdata/generate_large_fixtures.py --ensure\n");
        return 2;
    }

    const QString jpeg100 = QString::fromStdString(fixture("large_jpeg_100mp.jpg"));
    constexpr int kHfW = 1600;
    constexpr int kHfH = 1200;
    QTemporaryDir tmp;
    const QString hfPng = writeHighFrequencyPng(QDir(tmp.path()), "m47_hf.png", kHfW, kHfH);

    // ── E1: Inspector stays exact-source with a source-backed pane present ───
    {
        MARK("E1 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({hfPng, jpeg100});
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}),
              "E1: feasible + infeasible panes both display");
        const ImageFrame *frame0 = ws.engine().imageAt(0);
        const ImageFrame *frame1 = ws.engine().imageAt(1);
        CHECK(frame0 && frame0->width() == kHfW && frame0->height() == kHfH,
              "E1: the feasible pane holds its full-resolution analysis frame");
        CHECK(frame1 && frame1->pixels().isNull(),
              "E1: the infeasible pane holds only a metadata placeholder");
        RawImageView *view0 = paneView(&ws, 0);
        CHECK(view0 && !view0->image().isNull() && view0->image().size() != QSize(kHfW, kHfH),
              "E1: the feasible pane displays a bounded LOD raster");
        if (!view0 || !frame0)
            return 1;

        // Find a source coordinate whose display LOD sample differs from the
        // exact source pixel (guaranteed by the one-pixel-period fixture).
        QPoint sourcePoint(-1, -1);
        for (int y = 17; y < kHfH - 17 && sourcePoint.x() < 0; ++y)
        {
            for (int x = 23; x < kHfW - 23; ++x)
            {
                const QPoint displayPoint = view0->displayPointForSource(x, y);
                if (view0->image().pixel(displayPoint) != highFrequencyPixel(x, y).rgb())
                {
                    sourcePoint = QPoint(x, y);
                    break;
                }
            }
        }
        CHECK(sourcePoint.x() >= 0, "E1: adversarial fixture finds a LOD mismatch coordinate");
        if (sourcePoint.x() < 0)
            return 1;

        auto *sideToggle = ws.findChild<QCheckBox *>("analysisPanelToggle");
        auto *table = ws.findChild<QTableWidget *>("pixelInspectorTable");
        CHECK(sideToggle && table, "E1: Pixel Inspector controls are discoverable");
        if (!sideToggle || !table)
            return 1;
        sideToggle->setChecked(true);
        pump(50);

        // Drive the pane-0 hover with the mismatch coordinate: the inspector
        // must report the exact full-res pixel, never the LOD sample.
        QMetaObject::invokeMethod(view0, "pixelInfo", Qt::DirectConnection,
                                  Q_ARG(int, sourcePoint.x()), Q_ARG(int, sourcePoint.y()),
                                  Q_ARG(int, 0), Q_ARG(int, 0), Q_ARG(int, 0), Q_ARG(bool, true));
        pump(100);
        const QColor expected = highFrequencyPixel(sourcePoint.x(), sourcePoint.y());
        const QString exactR = table->item(0, 2) ? table->item(0, 2)->text() : QString();
        CHECK(exactR == QString::number(expected.red()),
              "E1: Inspector samples the exact full-resolution source pixel");
        CHECK(exactR != QString::number(qRed(view0->image().pixel(
                      view0->displayPointForSource(sourcePoint.x(), sourcePoint.y())))),
              "E1: Inspector RGB is not the display LOD sample");
        // The source-backed pane's row must report invalid — the Inspector may
        // not fall back to sampling its LOD raster.
        CHECK(table->rowCount() >= 2 && table->item(1, 2) &&
                  table->item(1, 2)->text() == QStringLiteral("无效"),
              "E1: the infeasible pane's inspector row is 无效 (never the LOD)");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0 &&
                  c.fullDecode.load() == 0,
              "E1: inspection never materializes the infeasible source");
        CHECK(waitTrue([&] { return TaskScheduler::instance()
                                        .metrics(TaskScheduler::PoolType::AnalysisPool)
                                        .pending == 0 &&
                                    TaskScheduler::instance()
                                            .metrics(TaskScheduler::PoolType::AnalysisPool)
                                            .active_tasks == 0; },
                       15000),
              "E1: analysis pools drain");
    }

    // ── E2: diff/PSNR/SSIM on an all-infeasible pair degrades, never decodes ─
    {
        MARK("E2 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({jpeg100, jpeg100});
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}), "E2: both LOD panes display");
        const uint64_t nativeLodAfterDisplay =
            SourceDecodeStats::instance().counters().nativeLod.load();
        CHECK(nativeLodAfterDisplay >= 2, "E2: display used native LOD only");
        auto *diffToggle = ws.findChild<QCheckBox *>("diffOverlayToggle");
        auto *metricLabel = ws.findChild<QLabel *>("diffMetricsLabel");
        CHECK(diffToggle && metricLabel, "E2: diff controls are discoverable");
        if (diffToggle)
        {
            diffToggle->setChecked(true);
            pump(50);
        }
        CHECK(waitTrue(
                  [&]
                  {
                      return TaskScheduler::instance()
                                 .metrics(TaskScheduler::PoolType::AnalysisPool)
                                 .pending == 0 &&
                             TaskScheduler::instance()
                                     .metrics(TaskScheduler::PoolType::AnalysisPool)
                                     .active_tasks == 0;
                  },
                  15000),
              "E2: the diff batch completes and drains");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.nativeLod.load() == nativeLodAfterDisplay,
              "E2: diff did not decode anything (neither LOD nor full)");
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0 &&
                  c.fullDecode.load() == 0,
              "E2: diff never falls back to full materialization");
        if (metricLabel)
            CHECK(metricLabel->text().contains(QStringLiteral("PSNR: —")),
                  "E2: metrics stay unavailable without full-res analysis sources");
        CHECK(paneView(&ws, 0) && !paneView(&ws, 0)->image().isNull() &&
                  paneView(&ws, 1) && !paneView(&ws, 1)->image().isNull(),
              "E2: panes keep their LOD display after the degraded diff");
    }

    // ── E3: report export stays full-resolution-only ─────────────────────────
    {
        MARK("E3 start");
        SourceDecodeStats::instance().counters().reset();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({hfPng, jpeg100});
        CHECK(waitForLoadedPanes(&ws, 2, {0, 1}), "E3: mixed set displays");
        mviewer::core::CompareReportBundle bundle = ws.buildReportBundle();
        CHECK(bundle.images.size() == 2 &&
                  bundle.images[0] == hfPng.toStdString() &&
                  bundle.images[1] == jpeg100.toStdString(),
              "E3: the bundle carries both panes in the REQUESTED order");
        CHECK(bundle.referenceIndex == 0, "E3: the feasible pane is the reference");
        CHECK(bundle.targets.size() == 1 && !bundle.targets[0].comparable &&
                  bundle.targets[0].psnr == 0.0 && bundle.targets[0].ssim == 0.0,
              "E3: the placeholder pair is recorded non-comparable (no fake metrics)");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.fullDecodeScaled.load() == 0 && c.fullDecodeCrop.load() == 0 &&
                  c.fullDecode.load() == 0,
              "E3: building the report never materialized the infeasible source");
        const ImageFrame *frame0 = ws.engine().imageAt(0);
        CHECK(frame0 && frame0->width() == kHfW && frame0->height() == kHfH,
              "E3: the feasible pane remains the full-res analysis source");
    }

    std::printf("=== M47 exact-source consumer tests: %s ===\n", g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
