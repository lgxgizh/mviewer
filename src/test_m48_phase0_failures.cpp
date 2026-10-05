// M48 Phase 0 — asynchronous decoder failure and recovery regressions.

#include "compareworkspace.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"
#include "core/render/Viewport.h"
#include "core/scheduler/TaskScheduler.h"
#include "exportdialog.h"
#include "imageviewer.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

QString writeSolidPng(const QTemporaryDir &dir, const char *name, int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const QString path = dir.filePath(QString::fromLatin1(name));
    image.save(path, "PNG");
    return path;
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
        int lastZoom = 0;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        QObject::connect(&viewer, &ImageViewer::zoomChanged, &viewer,
                         [&](int pct) { lastZoom = pct; });
        QImage warm(16, 16, QImage::Format_RGB32);
        warm.fill(Qt::red);
        viewer.setProvisionalImage(throwingPath, warm, QSize(100, 80));
        CHECK(!viewer.provisionalScreenRect().isEmpty(), "F2: warm thumbnail is on screen");
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return failed; }, 15000),
              "F2: a failed LOD decode reaches the loadFailed terminal");
        CHECK(viewer.provisionalScreenRect().isEmpty(), "F2: failed load drops the warm thumbnail");
        CHECK(lastZoom < 0, "F2: failed load clears the zoom readout");
        CHECK(viewer.windowTitle().contains(QStringLiteral("无法加载")),
              "F2: failed load retitles the viewer");
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

    // F4: a compare child must follow the dialog window's display profile.
    // The workspace itself is not a native window, so a resize that only
    // looked at its own QWindow used to leave an injected target in place.
    {
        MARK("F4 start");
        installDefaults();
        QDialog dialog;
        auto *layout = new QVBoxLayout(&dialog);
        auto *workspace = new CompareWorkspace(&dialog);
        layout->addWidget(workspace);
        dialog.resize(800, 600);
        dialog.show();
        pump(50);
        std::vector<uint8_t> bytes{1, 2, 3, 4};
        const auto injected = mviewer::core::DisplayColorContext::fromIccProfile(
            std::move(bytes), 3, "injected-monitor");
        workspace->setDisplayColorContext(injected);
        CHECK(workspace->displayColorContext().fingerprint == "injected-monitor",
              "F4: an explicit display target is accepted");
        dialog.resize(900, 700);
        pump(50);
        CHECK(dialog.windowHandle() != nullptr, "F4: the compare dialog has a window");
        CHECK(workspace->displayColorContext().fingerprint != "injected-monitor",
              "F4: resize rebinds compare color to the host window profile");
    }

    // F5: a failed load must not clear fit mode. With zoom lock on, the next
    // successful image would otherwise keep the pre-failure scale.
    {
        MARK("F5 start");
        installDefaults();
        QTemporaryDir dir;
        CHECK(dir.isValid(), "F5: temp dir");
        const QString smallPath = writeSolidPng(dir, "small.png", 200, 100);
        CHECK(QFileInfo::exists(smallPath), "F5: small image written");

        ImageViewer viewer;
        viewer.setFixedSize(800, 600);
        viewer.show();
        pump(30);
        viewer.setLockZoom(true);
        QImage warm(16, 16, QImage::Format_RGB32);
        warm.fill(Qt::red);
        viewer.setProvisionalImage(throwingPath, warm, QSize(4000, 200));
        CHECK(viewer.isFitMode() && viewer.isLockZoom(), "F5: starts fitted with zoom lock");
        const double stuckScale = viewer.viewTransform().scale;

        installThrowing(ThrowingM48Decoder::Mode::Lod);
        bool failed = false;
        int lastZoom = 0;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        QObject::connect(&viewer, &ImageViewer::zoomChanged, &viewer,
                         [&](int pct) { lastZoom = pct; });
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return failed; }, 15000), "F5: failed load reaches the terminal");
        CHECK(viewer.provisionalScreenRect().isEmpty(), "F5: warm thumbnail is dropped");
        CHECK(lastZoom < 0, "F5: zoom readout clears");
        CHECK(viewer.windowTitle().contains(QStringLiteral("无法加载")), "F5: failure title");
        CHECK(viewer.isFitMode(), "F5: failed load keeps fit mode");

        installDefaults();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                         [&](std::shared_ptr<ImageFrame>) { ready = true; });
        viewer.setBrowseSequence({smallPath});
        viewer.setImage(smallPath);
        CHECK(waitTrue([&] { return ready; }, 20000), "F5: the next image loads");
        Viewport expect;
        expect.screenW = viewer.viewTransform().screenW;
        expect.screenH = viewer.viewTransform().screenH;
        expect.fit(200, 100, FitPolicy::Comfortable);
        const double got = viewer.viewTransform().scale;
        CHECK(viewer.isFitMode(), "F5: the next image stays fitted");
        CHECK(std::abs(got - expect.scale) < 0.02, "F5: the next image uses its own fit scale");
        CHECK(std::abs(got - stuckScale) > 0.2, "F5: the pre-failure scale was not reused");
        const int expectPct = static_cast<int>(expect.scale * 100.0 + 0.5);
        CHECK(std::abs(lastZoom - expectPct) <= 1,
              "F5: zoom readout returns at the fitted percent");
    }

    // F6: an explicit locked zoom survives a warm thumbnail and a failed load.
    {
        MARK("F6 start");
        installDefaults();
        QTemporaryDir dir;
        CHECK(dir.isValid(), "F6: temp dir");
        const QString first = writeSolidPng(dir, "first.png", 200, 100);
        const QString second = writeSolidPng(dir, "second.png", 40, 40);
        CHECK(QFileInfo::exists(first) && QFileInfo::exists(second), "F6: images written");

        ImageViewer viewer;
        viewer.setFixedSize(800, 600);
        viewer.show();
        pump(30);
        bool ready = false;
        int lastZoom = 0;
        QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                         [&](std::shared_ptr<ImageFrame>) { ready = true; });
        QObject::connect(&viewer, &ImageViewer::zoomChanged, &viewer,
                         [&](int pct) { lastZoom = pct; });
        viewer.setImage(first);
        CHECK(waitTrue([&] { return ready; }, 20000), "F6: first image loads");
        viewer.setLockZoom(true);
        viewer.zoomActual();
        CHECK(!viewer.isFitMode(), "F6: 100% leaves fit mode");
        const double locked = viewer.viewTransform().scale;

        QImage warm(8, 8, QImage::Format_RGB32);
        warm.fill(Qt::red);
        viewer.setProvisionalImage(second, warm, QSize(4000, 100));
        CHECK(!viewer.isFitMode(), "F6: warm thumbnail keeps the explicit zoom");
        CHECK(std::abs(viewer.viewTransform().scale - locked) < 1e-6,
              "F6: warm thumbnail does not change the locked scale");

        installThrowing(ThrowingM48Decoder::Mode::Lod);
        bool failed = false;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return failed; }, 15000), "F6: failed load reaches the terminal");
        CHECK(!viewer.isFitMode(), "F6: failed load keeps the explicit zoom");
        CHECK(lastZoom < 0, "F6: zoom readout clears");
        CHECK(viewer.windowTitle().contains(QStringLiteral("无法加载")), "F6: failure title");
        CHECK(viewer.provisionalScreenRect().isEmpty(), "F6: warm thumbnail is dropped");

        installDefaults();
        ready = false;
        viewer.setBrowseSequence({second});
        viewer.setImage(second);
        CHECK(waitTrue([&] { return ready; }, 20000), "F6: the next image loads");
        CHECK(!viewer.isFitMode(), "F6: the next image keeps the locked zoom");
        CHECK(std::abs(viewer.viewTransform().scale - locked) < 1e-6, "F6: locked scale sticks");
        CHECK(lastZoom > 0 && lastZoom < 150, "F6: zoom readout returns at the locked zoom");
    }

    // F7: switching images must not keep a non-owning full-frame alias.
    {
        MARK("F7 start");
        installDefaults();
        QTemporaryDir dir;
        CHECK(dir.isValid(), "F7: temp dir");
        const QString big = writeSolidPng(dir, "big.png", 800, 600);
        CHECK(QFileInfo::exists(big), "F7: image written");

        ImageViewer viewer;
        viewer.setFixedSize(320, 240);
        viewer.show();
        pump(30);
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                         [&](std::shared_ptr<ImageFrame>) { ready = true; });
        viewer.setImage(big);
        CHECK(waitTrue([&] { return ready; }, 20000), "F7: source image loads");
        CHECK(viewer.width() > 0 && viewer.width() < 800, "F7: window is narrower than the source");
        CHECK(viewer.frame() && viewer.frame()->width() == 800,
              "F7: source is wider than the window");
        viewer.setImage(dir.filePath("next.png"));
        const QSize snap = viewer.transitionSnapshotSize();
        const int edge = std::max(64, std::max(viewer.width(), viewer.height()));
        CHECK(!snap.isEmpty(), "F7: a switch keeps a transition snapshot");
        CHECK(snap.width() <= edge && snap.height() <= edge,
              "F7: the snapshot is bounded by the window");
    }

    // F8: percent resize must not inherit the 1920 px long-edge default.
    {
        MARK("F8 start");
        ExportDialog dialog;
        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("exportResizeModeCombo"));
        auto *spin = dialog.findChild<QSpinBox *>(QStringLiteral("exportResizeValueSpin"));
        CHECK(mode && spin, "F8: resize controls exist");
        if (mode && spin)
        {
            CHECK(!spin->isEnabled(), "F8: resize value is disabled until a mode is chosen");
            mode->setCurrentIndex(mode->findData(QStringLiteral("scale")));
            CHECK(spin->isEnabled(), "F8: percent mode enables the value");
            CHECK(spin->value() == 100, "F8: percent mode starts at 100, not 1920");
            CHECK(spin->maximum() <= 800, "F8: percent mode rejects a pixel-sized value");
            CHECK(spin->suffix().contains(QLatin1Char('%')), "F8: percent mode labels the value");
            mode->setCurrentIndex(mode->findData(QStringLiteral("fit")));
            CHECK(spin->value() == 1920, "F8: long-edge mode restores a pixel default");
            CHECK(spin->suffix().contains(QStringLiteral("px")),
                  "F8: long-edge mode labels pixels");
            mode->setCurrentIndex(mode->findData(QStringLiteral("scale")));
            CHECK(spin->value() == 100, "F8: returning to percent does not keep 1920");
            mode->setCurrentIndex(mode->findData(QStringLiteral("none")));
            CHECK(!spin->isEnabled(), "F8: none disables the value again");
        }
    }

    std::printf("=== M48 async failure regression gate: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
