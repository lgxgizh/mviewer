// Stress: Compare soft pair reload with two 4096x3072 detail-rich JPEGs at
// high zoom must always end with both panes off "Loading" and showing a raster
// at least as dense as the zoom needs (no sticky thumb / 640 cheap mosaic).
//
// Repro of the intermittent stuck-Loading race: a single-pane request (fast
// preview / per-pane scaleChanged LOD refresh) used to cancel the all-pane
// display batch and strand the other pane on its soft stand-in.

#include "compareworkspace.h"
#include "core/cache/CacheManager.h"
#include "core/image/DisplayMip.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QImage>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

struct CompareSourceDisplayTestAccess
{
    static CompareWorkspace::SourceDisplayResult fromFile(const std::string &path,
                                                          const QSize &target, bool provisional)
    {
        CompareWorkspace::DisplayRequest request;
        request.target = target;
        request.sourceRect = QRect(0, 0, 4096, 3072);
        request.provisional = provisional;
        return CompareWorkspace::materializeSourceDisplay(path, request);
    }
};

namespace
{

int g_failures = 0;

void check(bool ok, const char *msg)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", msg);
        std::fflush(stdout);
        ++g_failures;
    }
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

void pump(int ms)
{
    waitTrue([] { return false; }, ms);
}

RawImageView *paneView(CompareWorkspace *ws, int index)
{
    for (RawImageView *v : ws->findChildren<RawImageView *>())
        if (v && v->cellIndex() == index)
            return v;
    return nullptr;
}

// High-frequency noise + 1px checker so a stretched thumb is obvious.
QString writeFixture(const QTemporaryDir &dir, const char *name, quint32 seed)
{
    QImage img(4096, 3072, QImage::Format_RGB888);
    QRandomGenerator rng(seed);
    for (int y = 0; y < img.height(); ++y)
    {
        uchar *line = img.scanLine(y);
        for (int x = 0; x < img.width(); ++x)
        {
            const int checker = ((x ^ y) & 1) ? 60 : 0;
            const quint32 r = rng.generate();
            line[x * 3 + 0] = static_cast<uchar>(std::min(255u, (r & 0xFF) / 2 + checker));
            line[x * 3 + 1] = static_cast<uchar>(std::min(255u, ((r >> 8) & 0xFF) / 2 + checker));
            line[x * 3 + 2] = static_cast<uchar>(std::min(255u, ((r >> 16) & 0xFF) / 2 + checker));
        }
    }
    const QString path = dir.filePath(QString::fromLatin1(name));
    img.save(path, "JPG", 92);
    return path;
}

// Pane is settled when it is off Loading and its raster has enough density
// for the current zoom (1:1 when zoomed in, else scale * covered width).
bool paneSettled(RawImageView *v)
{
    if (!v || v->softLoading() || v->image().isNull())
        return false;
    if (v->sourceSize() != QSize(4096, 3072))
        return false;
    const QRect covered = v->sourceRect();
    if (!covered.isValid())
        return false;
    const double physical = v->scale() * std::max(1.0, v->devicePixelRatioF());
    const double need = std::min(1.0, physical) * covered.width();
    return v->image().width() + 2 >= static_cast<int>(need * 0.9);
}

void wheel(RawImageView *v, int delta)
{
    const QPointF center(v->rect().center());
    QWheelEvent event(center, v->mapToGlobal(center), QPoint(0, 0), QPoint(0, delta), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(v, &event);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid())
        return 2;
    const QString a = writeFixture(dir, "a.jpg", 1);
    const QString b = writeFixture(dir, "b.jpg", 2);
    const QString c = writeFixture(dir, "c.jpg", 3);
    const QString d = writeFixture(dir, "d.jpg", 4);

    // Root cause of the stuck pane: with lod 0 gone from the cache, a sharp
    // 4096 full-frame request was served the 2048 mip (largest <= 4096) and
    // accepted as final. The sharp pass must decode at full density instead.
    {
        const std::string key = c.toStdString();
        QImage half(2048, 1536, QImage::Format_RGB888);
        half.fill(Qt::gray);
        CacheManager::instance().putMip(key, 1, mvcore::fromQImage(half));
        const auto sharp = CompareSourceDisplayTestAccess::fromFile(key, QSize(4096, 4096), false);
        check(sharp.errorText.isEmpty() && sharp.pixels.width >= 3687,
              "sharp full-frame request is not served a half-size cached mip");
        const auto cheap = CompareSourceDisplayTestAccess::fromFile(key, QSize(640, 640), true);
        check(cheap.errorText.isEmpty() && !cheap.pixels.isNull(), "cheap request still paints");
        mviewer::core::dropMips(key);
    }

    int iterations = 8;
    if (const char *env = std::getenv("MV_STRESS_ITERATIONS"))
        iterations = std::max(1, std::atoi(env));

    CompareWorkspace ws;
    ws.resize(1920, 1040);
    ws.show();
    ws.setImages({a, b});
    check(waitTrue([&] { return paneSettled(paneView(&ws, 0)) && paneSettled(paneView(&ws, 1)); },
                   30000),
          "initial pair settles");

    // Zoom to ~855% through the real wheel path (sync zoom).
    if (RawImageView *v0 = paneView(&ws, 0))
    {
        for (int i = 0; i < 80 && v0->scale() < 8.5; ++i)
        {
            wheel(v0, 120);
            pump(5);
        }
        std::printf("zoom reached %.0f%%\n", v0->scale() * 100.0);
    }
    pump(400);

    int stuck = 0;
    for (int it = 0; it < iterations; ++it)
    {
        const bool odd = (it % 2) != 0;
        const QString p0 = odd ? a : c;
        const QString p1 = odd ? b : d;
        // Browse -> Compare handoff: warm thumbs of varying size (stretched =
        // mosaic), one pane only, or none (cold).
        const int variant = it % 4;
        const QSize thumbSize = variant == 1 ? QSize(1600, 1200) : QSize(512, 384);
        ws.setImages({p0, p1});
        if (variant != 3)
            ws.seedWarmDisplay(p0, QImage(p0).scaled(thumbSize), QSize(4096, 3072), QRect());
        if (variant == 0 || variant == 1)
            ws.seedWarmDisplay(p1, QImage(p1).scaled(thumbSize), QSize(4096, 3072), QRect());
        // Interaction while the reload is in flight (jitter by iteration).
        for (int k = 0; k < (it % 4); ++k)
        {
            pump(3 + it % 7);
            if (RawImageView *v = paneView(&ws, k % 2))
                wheel(v, (k % 2) ? -120 : 120);
        }
        const bool ok = waitTrue(
            [&] { return paneSettled(paneView(&ws, 0)) && paneSettled(paneView(&ws, 1)); }, 10000);
        if (!ok)
        {
            ++stuck;
            std::printf("  iter %d variant %d stuck\n", it, variant);
            for (int p = 0; p < 2; ++p)
            {
                RawImageView *v = paneView(&ws, p);
                if (!v)
                    continue;
                std::printf("  iter %d pane %d: soft=%d image=%dx%d src=%dx%d rect=%d,%d %dx%d "
                            "scale=%.3f\n",
                            it, p, v->softLoading() ? 1 : 0, v->image().width(),
                            v->image().height(), v->sourceSize().width(), v->sourceSize().height(),
                            v->sourceRect().x(), v->sourceRect().y(), v->sourceRect().width(),
                            v->sourceRect().height(), v->scale());
            }
        }
    }
    std::printf("stuck iterations: %d / %d\n", stuck, iterations);
    check(stuck == 0, "every soft reload leaves Loading with a dense raster");
    std::printf(g_failures == 0 ? "=== Compare stuck-loading stress: PASS ===\n"
                                : "=== Compare stuck-loading stress: FAIL ===\n");
    return g_failures == 0 ? 0 : 1;
}
