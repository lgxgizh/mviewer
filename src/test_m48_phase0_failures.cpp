// M48 Phase 0 — asynchronous decoder failure and recovery regressions.

#include "compareworkspace.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"

#include <QApplication>
#include <QFileInfo>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(c, m)                                                                          \
    do                                                                                       \
    {                                                                                        \
        if (!(c))                                                                            \
        {                                                                                    \
            std::printf("FAIL: %s\n", m);                                                   \
            std::fflush(stdout);                                                             \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (false)

#define MARK(t)                                                                              \
    do                                                                                       \
    {                                                                                        \
        std::printf("%s\n", t);                                                             \
        std::fflush(stdout);                                                                 \
    } while (false)

std::string fixtureRoot()
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large";
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool schedulerIdle()
{
    uint64_t pending = 0;
    uint64_t active = 0;
    for (int p = 0; p < 5; ++p)
    {
        const auto metrics = TaskScheduler::instance().metrics(static_cast<TaskScheduler::PoolType>(p));
        pending += metrics.pending;
        active += metrics.active_tasks;
    }
    return pending == 0 && active == 0;
}

class ThrowingM48Decoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
    enum class Mode
    {
        None,
        Probe,
        Lod,
        Region
    };

    std::atomic<Mode> mode{Mode::None};
    mutable std::atomic<int> regionCalls{0};

    bool canDecode(const std::string &path) const override
    {
        return QFileInfo(QString::fromStdString(path)).suffix().toLower() == "m48";
    }
    ImageData decodeFull(const std::string &) const override { throw std::runtime_error("m48"); }
    ImageData decodeFull(const std::string &, mviewer::domain::ImageMetadata &) const override
    {
        throw std::runtime_error("m48");
    }
    ImageData decodeScaled(const std::string &, int) const override
    {
        throw std::runtime_error("m48");
    }
    ImageData decodeScaled(const std::string &, int, mviewer::domain::ImageMetadata &) const override
    {
        throw std::runtime_error("m48");
    }
    std::vector<std::string> extensions() const override { return {"m48"}; }
    const char *name() const override { return "ThrowingM48Decoder"; }

    bool canProbe(const std::string &) const override { return true; }
    bool probeMetadata(const std::string &path, mviewer::domain::ImageMetadata &meta) const override
    {
        if (mode == Mode::Probe)
            throw std::runtime_error("injected probe failure");
        meta.filePath = path;
        meta.fileName = "throwing.m48";
        meta.width = 5000;
        meta.height = 4000;
        meta.fileSize = 1024;
        meta.modifiedEpochSec = 1700000000;
        meta.orientation = 1;
        meta.format = "M48";
        return true;
    }
    bool canNativeLod(const std::string &) const override { return true; }
    ImageData decodeLod(const std::string &, int, mviewer::domain::ImageMetadata &) const override
    {
        if (mode == Mode::Lod)
            throw std::runtime_error("injected lod failure");
        return makeSolidRgb(64, 64, 200, 60, 30);
    }
    bool canNativeRegion(const std::string &) const override { return false; }
    ImageData decodeRegion(const std::string &, int, int, int, int, int, int,
                           mviewer::domain::ImageMetadata &) const override
    {
        ++regionCalls;
        if (mode == Mode::Region)
            throw std::runtime_error("injected region failure");
        return makeSolidRgb(64, 64, 30, 60, 200);
    }

  private:
    static ImageData makeSolidRgb(int w, int h, uint8_t r, uint8_t g, uint8_t b)
    {
        ImageData out = makeImageData(w, h, PixelFormat::RGB24);
        const auto view = out.view();
        for (int y = 0; y < h; ++y)
        {
            uint8_t *p = view.data + static_cast<size_t>(y) * view.stride();
            for (int x = 0; x < w; ++x)
            {
                p[x * 3] = r;
                p[x * 3 + 1] = g;
                p[x * 3 + 2] = b;
            }
        }
        return out;
    }
};

std::shared_ptr<ThrowingM48Decoder> g_throwing;

void installThrowing(ThrowingM48Decoder::Mode mode)
{
    auto &registry = DecoderRegistry::instance();
    registry.resetToDefaults();
    registry.unregister("QtFallbackDecoder");
    g_throwing = std::make_shared<ThrowingM48Decoder>();
    g_throwing->mode = mode;
    registry.registerDecoder(g_throwing);
    registry.registerDecoder(std::make_shared<QtDecoder>());
    registry.registerDecoder(std::make_shared<QtFallbackDecoder>());
}

void installDefaults()
{
    auto &registry = DecoderRegistry::instance();
    registry.resetToDefaults();
    registry.registerDecoder(std::make_shared<QtDecoder>());
    registry.registerDecoder(std::make_shared<QtFallbackDecoder>());
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString jpeg100 = QString::fromStdString(fixtureRoot() + "/large_jpeg_100mp.jpg");
    const QString throwingPath = QString::fromStdString(fixtureRoot() + "/throwing.m48");

    // F1: a throwing probe must not escape CompareWorkspace::setImages.
    {
        MARK("F1 start");
        installThrowing(ThrowingM48Decoder::Mode::Probe);
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        int warningCount = 0;
        QObject::connect(&ws, &CompareWorkspace::loadWarning, &ws,
                         [&](const QString &) { ++warningCount; });
        bool threw = false;
        try
        {
            ws.setImages({throwingPath, jpeg100});
        }
        catch (...)
        {
            threw = true;
        }
        CHECK(!threw, "F1: a throwing probe never escapes setImages on the UI thread");
        CHECK(waitTrue([&] { return warningCount == 1; }, 20000),
              "F1: throwing probe is accounted once and finishes with one load warning");
        CHECK(waitTrue(schedulerIdle, 20000), "F1: pools drain");
    }

    // F2: a throwing decodeLod reaches an observable terminal and recovers.
    {
        MARK("F2 start");
        installThrowing(ThrowingM48Decoder::Mode::Lod);
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool failed = false;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return failed; }, 15000),
              "F2: a failed LOD decode reaches the loadFailed terminal");
        CHECK(waitTrue(schedulerIdle, 20000), "F2: pools drain after the terminal");
        installDefaults();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setBrowseSequence({jpeg100});
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return ready; }, 60000),
              "F2: the viewer recovers and opens a valid image afterwards");
    }

    // F3: a throwing decodeRegion keeps the raster and does not retry.
    {
        MARK("F3 start");
        installThrowing(ThrowingM48Decoder::Mode::Region);
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return ready; }, 60000), "F3: initial LOD displays");
        const QImage before = viewer.displayRaster();
        const int regionCallsBefore = g_throwing->regionCalls.load();
        bool failed = false;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        viewer.zoomActual();
        viewer.update();
        CHECK(waitTrue([&] { return g_throwing->regionCalls.load() > regionCallsBefore; }, 30000),
              "F3: zoom requests a region raster");
        pump(1500);
        CHECK(!viewer.displayRaster().isNull() && viewer.displayRaster() == before,
              "F3: the current good raster is kept when the upgrade fails");
        CHECK(failed, "F3: the failed region upgrade reaches the loadFailed terminal");
        const int regionCallsAfterFailure = g_throwing->regionCalls.load();
        viewer.zoomIn();
        viewer.update();
        pump(500);
        CHECK(waitTrue(schedulerIdle, 20000), "F3: pools drain");
        CHECK(g_throwing->regionCalls.load() == regionCallsAfterFailure,
              "F3: a degraded display does not retry region upgrades");
    }

    std::printf("=== M48 async failure regression gate: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
