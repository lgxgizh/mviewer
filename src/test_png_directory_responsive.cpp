// PNG directory-switch liveness.
//
// Two folders, one synthetic 3838x2156 RGB888 PNG each. That size is a
// coverage choice: it sits above the viewer's 4e6-pixel display-LOD threshold.
// It is not a reconstruction of a redacted user height.
//
// openExternalTargets must return within 1000 ms. A 25 ms GUI heartbeat
// records the largest gap between firings. Gallery scan, thumbnail decode,
// and preview decode must become ready through public accessors while that
// heartbeat keeps firing. The viewer is included when its public widget can
// be shown; MainWindow requests a display raster only for a visible viewer.
//
// Production scheduler widths are left unchanged. Forcing every pool to one
// thread hides the overlapping QImageReader::read() fan-out this test exists
// to expose.
//
// QtDecoder::fullImageReadCount is printed and never asserted. It counts
// decodeFull only. The Qt PNG LOD setScaledSize+read fallback and the status
// metadata 1x1 read do not increment it. A zero delta does not prove those
// reads were skipped, and a positive delta does not by itself locate a stall.
// NOLINTBEGIN(readability-magic-numbers)

#include "application/Shutdown.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/render/DisplayResample.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "previewpanel.h"
#include "runtime_storage.h"
#include "thumbnailpanel.h"
#include "thumbnailprovider.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace
{

const bool kJpegCase = qEnvironmentVariable("MVIEWER_DIRECTORY_CASE") == QStringLiteral("jpeg");
const int kPngWidth = kJpegCase ? 4096 : 3838;
const int kPngHeight = kJpegCase ? 3072 : 2156;
const int kFileCount = kJpegCase ? 4 : 1;
constexpr int kHeartbeatMs = 25;
constexpr int kSwitchBudgetMs = 1000;
constexpr int kGapBudgetMs = 1000;
constexpr int kReadyBudgetMs = 30000;
constexpr int kDrainSeconds = 10;

// Wrap actual GUI event delivery, including QOpenGLWidget paint/upload and
// queued decode completions. Slow-event timing is diagnostic, not a product patch.
class TimedApplication : public QApplication
{
  public:
    using QApplication::QApplication;
    bool notify(QObject *receiver, QEvent *event) override
    {
        const bool guiThread = QThread::currentThread() == thread();
        QElapsedTimer span;
        span.start();
        const int kind = static_cast<int>(event->type());
        const QByteArray type(receiver ? receiver->metaObject()->className() : "null");
        const bool result = QApplication::notify(receiver, event);
        const qint64 elapsed = span.elapsed();
        if (guiThread && elapsed >= 100)
            std::cout << "GUI_EVENT class=" << type.constData() << " type=" << kind
                      << " elapsedMs=" << elapsed << '\n';
        return result;
    }
};

struct Results
{
    int pass = 0;
    int fail = 0;
};

void check(Results &results, bool cond, const std::string &msg)
{
    if (cond)
    {
        std::cout << "  PASS: " << msg << '\n';
        ++results.pass;
    }
    else
    {
        std::cout << "  FAIL: " << msg << '\n';
        ++results.fail;
    }
}

bool samePath(const QString &left, const QString &right)
{
    const QString a = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
    const QString b = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    do
        QCoreApplication::processEvents(QEventLoop::AllEvents, kHeartbeatMs);
    while (timer.elapsed() < ms);
}

std::string phaseText(const char *phase, const char *msg)
{
    std::string text = "phase ";
    text += phase;
    text += ": ";
    text += msg;
    return text;
}

const char *flag(bool value)
{
    return value ? "yes" : "no";
}

const char *previewQualityName(PreviewPanel::PresentationQuality quality)
{
    switch (quality)
    {
    case PreviewPanel::PresentationQuality::None:
        return "None";
    case PreviewPanel::PresentationQuality::Placeholder:
        return "Placeholder";
    case PreviewPanel::PresentationQuality::Thumbnail:
        return "Thumbnail";
    case PreviewPanel::PresentationQuality::Preview:
        return "Preview";
    }
    return "Unknown";
}

// Smooth 8-bit gradient. QImage::save writes a normal non-interlaced PNG.
// On a master build with MVIEWER_HAS_PNG_LOD this file is eligible for the
// bounded libpng path; released 1.0.178 has no such path.
bool writeGradientPng(const QString &path, int blueBias)
{
    const QString external = qEnvironmentVariable("MVIEWER_PNG_FIXTURE");
    if (!kJpegCase && !external.isEmpty())
        return QFile::copy(external, path);
    QImage image(kPngWidth, kPngHeight, QImage::Format_RGB888);
    if (image.isNull())
        return false;
    for (int y = 0; y < kPngHeight; ++y)
    {
        uchar *row = image.scanLine(y);
        const int green = (y * 255) / (kPngHeight - 1);
        for (int x = 0; x < kPngWidth; ++x)
        {
            const int i = x * 3;
            row[i] = static_cast<uchar>((x * 255) / (kPngWidth - 1));
            row[i + 1] = static_cast<uchar>(green);
            row[i + 2] = static_cast<uchar>(blueBias);
        }
    }
    return image.save(path, kJpegCase ? "JPEG" : "PNG");
}

struct Fixtures
{
    QString dirA;
    QString dirB;
    QString pngA;
    QString pngB;
};

bool prepareFixtures(QTemporaryDir &temp, Fixtures &out)
{
    if (!temp.isValid())
        return false;
    QDir root(temp.path());
    out.dirA = root.filePath(QStringLiteral("folder-a"));
    out.dirB = root.filePath(QStringLiteral("folder-b"));
    if (!QDir().mkpath(out.dirA) || !QDir().mkpath(out.dirB))
        return false;
    const QString name = kJpegCase ? QStringLiteral("sample1.jpg") : QStringLiteral("sample.png");
    out.pngA = QDir(out.dirA).absoluteFilePath(name);
    out.pngB = QDir(out.dirB).absoluteFilePath(name);
    for (const QString &dir : {out.dirA, out.dirB})
    {
        for (int i = 1; i <= kFileCount; ++i)
        {
            const QString leaf = kJpegCase ? QStringLiteral("sample%1.jpg").arg(i) : name;
            if (!writeGradientPng(QDir(dir).filePath(leaf), 32 + 40 * i))
                return false;
        }
    }
    return true;
}

void isolateSettings()
{
    // Hermetic INI store under the test-mode config directory. Empty settings
    // keep restoreLastSession from opening a previous folder. The recovery
    // env skips the crash-restore prompt scheduled from the MainWindow ctor.
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-png-directory-responsive"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-png-directory-responsive"));
    mviewer::runtime::resetWritableDirectoryCacheForTests();
    mviewer::runtime::configureSettings();
    QSettings().clear();
    const QString cfg = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    if (!cfg.isEmpty())
        QDir(cfg).removeRecursively();
}

struct Heartbeat
{
    QTimer timer;
    QElapsedTimer clock;
    qint64 largestGapMs = 0;
    int ticks = 0;

    void start()
    {
        largestGapMs = 0;
        ticks = 0;
        clock.start();
        timer.setInterval(kHeartbeatMs);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, &timer, [this]() { onTick(); });
        timer.start();
    }

    // The first firing counts. A directory switch that blocks before the
    // event loop runs shows up as the gap from start() to that firing.
    void onTick()
    {
        const qint64 gap = clock.restart();
        if (gap > largestGapMs)
            largestGapMs = gap;
        ++ticks;
    }

    void stop()
    {
        timer.stop();
    }
};

ImageViewer *findImageViewer()
{
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets)
    {
        if (auto *viewer = qobject_cast<ImageViewer *>(widget))
            return viewer;
    }
    return nullptr;
}

struct PhaseState
{
    qint64 callMs = -1;
    qint64 waitMs = -1;
    qint64 firstViewerMs = -1;
    qint64 firstThumbMs = -1;
    qint64 firstPreviewMs = -1;
    int ticksBefore = 0;
    int ticksAfter = 0;
    qint64 gapMs = 0;
    uint64_t readsBefore = 0;
    uint64_t readsAfter = 0;
    bool dirReady = false;
    bool thumbReady = false;
    bool thumbFailed = false;
    bool previewReady = false;
    bool viewerExpected = false;
    bool viewerReady = false;
    bool heartbeatMoved = false;
    int pathCount = 0;
    QString currentDir;
    QSize thumbPx;
    QSize knownSize;
    QSize entrySize;
    PreviewPanel::PresentationQuality previewQuality = PreviewPanel::PresentationQuality::None;
    bool previewHasImage = false;
    QSize previewPx;
    QSize previewSource;
    QString previewRequested;
    QString previewPresented;
    bool viewerHidden = true;
    QSize viewerWidget;
    QString viewerPath;
    bool lod = false;
    bool frameValid = false;
    QSize rasterPx;
    QSize displaySize;
};

// Gallery listing and the decoder can disagree on slash form. Either key is
// the size remembered when the thumbnail was produced.
QSize knownSource(const QString &listed, const QString &png)
{
    const QSize fromGallery = ThumbnailProvider::knownSourceSize(listed);
    if (!fromGallery.isEmpty())
        return fromGallery;
    return ThumbnailProvider::knownSourceSize(png);
}

void sampleGallery(ThumbnailPanel &panel, const QString &dir, const QString &png, PhaseState &state)
{
    state.currentDir = panel.currentDir();
    const QStringList &paths = panel.pathList();
    state.pathCount = paths.size();
    QString listed;
    for (const QString &candidate : paths)
        if (samePath(candidate, png))
            listed = candidate;
    const bool oneFile = state.pathCount == kFileCount && !listed.isEmpty();
    state.dirReady = oneFile && samePath(state.currentDir, dir);
    state.thumbFailed = state.dirReady && panel.thumbFailed(listed);
    const QPixmap thumb = state.dirReady ? panel.thumbReady(listed) : QPixmap();
    state.thumbPx = thumb.size();
    state.knownSize = state.dirReady ? knownSource(listed, png) : QSize();
    state.entrySize = QSize();
    const ThumbnailPanel::Entry *entry = nullptr;
    if (state.dirReady)
        entry = panel.entryForPath(listed);
    if (entry != nullptr)
        state.entrySize = QSize(entry->width, entry->height);
    const QSize want(kPngWidth, kPngHeight);
    const bool pixels = !thumb.isNull() && thumb.width() > 0 && thumb.height() > 0;
    state.thumbReady = state.dirReady && pixels && state.knownSize == want;
    for (const QString &candidate : paths)
        state.thumbReady = state.thumbReady && !panel.thumbReady(candidate).isNull();
}

void samplePreview(PreviewPanel &preview, const QString &png, PhaseState &state)
{
    state.previewQuality = preview.presentationQuality();
    state.previewHasImage = preview.hasImage();
    state.previewPx = preview.previewPixelSize();
    state.previewSource = preview.sourceImageSize();
    state.previewRequested = preview.requestedPath();
    state.previewPresented = preview.presentedPath();
    const bool requestedOk = samePath(state.previewRequested, png);
    const bool presentedOk = samePath(state.previewPresented, png);
    const bool pathOk = requestedOk && presentedOk;
    state.previewReady = pathOk && state.previewHasImage &&
                         state.previewQuality == PreviewPanel::PresentationQuality::Preview &&
                         state.previewPx.width() > 0 && state.previewPx.height() > 0 &&
                         !preview.currentPixmap().isNull();
}

void sampleViewer(ImageViewer *viewer, const QString &png, PhaseState &state)
{
    state.viewerExpected = viewer != nullptr && !viewer->isHidden();
    state.viewerHidden = viewer == nullptr || viewer->isHidden();
    state.viewerWidget = viewer != nullptr ? viewer->size() : QSize();
    state.viewerPath = viewer != nullptr ? viewer->currentPath() : QString();
    state.lod = viewer != nullptr && viewer->isLodDisplay();
    const QImage raster = viewer != nullptr ? viewer->displayRaster() : QImage();
    state.rasterPx = raster.size();
    state.displaySize = viewer != nullptr ? viewer->displaySize() : QSize();
    const auto frame = viewer != nullptr ? viewer->frame() : std::shared_ptr<ImageFrame>{};
    state.frameValid = frame && frame->isValid();
    const bool pixels = !raster.isNull() || state.frameValid;
    const QSize want(kPngWidth, kPngHeight);
    state.viewerReady = state.viewerExpected && samePath(state.viewerPath, png) && pixels &&
                        state.displaySize == want;
}

bool contentReady(const PhaseState &state)
{
    if (!state.dirReady || !state.thumbReady || !state.previewReady)
        return false;
    return state.viewerExpected && state.viewerReady;
}

void awaitReady(Heartbeat &hb, ThumbnailPanel &panel, PreviewPanel &preview, ImageViewer *viewer,
                const QString &dir, const QString &png, PhaseState &state)
{
    QElapsedTimer budget;
    budget.start();
    while (budget.elapsed() < kReadyBudgetMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, kHeartbeatMs);
        sampleGallery(panel, dir, png, state);
        samplePreview(preview, png, state);
        sampleViewer(viewer, png, state);
        if (state.viewerReady && state.firstViewerMs < 0)
            state.firstViewerMs = budget.elapsed();
        if (state.thumbReady && state.firstThumbMs < 0)
            state.firstThumbMs = budget.elapsed();
        if (state.previewReady && state.firstPreviewMs < 0)
            state.firstPreviewMs = budget.elapsed();
        if (contentReady(state) && hb.ticks > state.ticksBefore)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    state.waitMs = budget.elapsed();
    // Readiness is not proof that the queued raster paint ran. Keep the GUI
    // alive through paint + 48ms Sharp upgrade before switching away.
    pump(250);
    sampleGallery(panel, dir, png, state);
    samplePreview(preview, png, state);
    sampleViewer(viewer, png, state);
    state.ticksAfter = hb.ticks;
    state.gapMs = hb.largestGapMs;
    state.readsAfter = QtDecoder::fullImageReadCount();
    state.heartbeatMoved = state.ticksAfter > state.ticksBefore;
}

void printSize(const char *label, const QSize &size)
{
    std::cout << ' ' << label << '=' << size.width() << 'x' << size.height();
}

void printPhase(const char *phase, const PhaseState &state)
{
    std::cout << "[phase " << phase << "] sampled\n";
    std::cout << "  openExternalTargets " << state.callMs << " ms (budget " << kSwitchBudgetMs
              << ")\n";
    std::cout << "  readiness wait " << state.waitMs << " ms (budget " << kReadyBudgetMs << ")\n";
    std::cout << "  firstRasterMs=" << state.firstViewerMs << " allThumbsMs=" << state.firstThumbMs
              << " previewPixelsMs=" << state.firstPreviewMs << '\n';
    std::cout << "  fullImageReadCount " << state.readsBefore << " -> " << state.readsAfter
              << " (decodeFull only; PNG LOD fallback and metadata 1x1 read are not counted)\n";
    std::cout << "  heartbeat ticks " << state.ticksBefore << " -> " << state.ticksAfter
              << " largestGapSinceStart " << state.gapMs << " ms (budget " << kGapBudgetMs << ")\n";
    std::cout << "  gallery dirReady=" << flag(state.dirReady) << " pathCount=" << state.pathCount
              << " currentDir=" << state.currentDir.toStdString() << '\n';
    std::cout << "  thumb ready=" << flag(state.thumbReady)
              << " failed=" << flag(state.thumbFailed);
    printSize("pixmap", state.thumbPx);
    printSize("known", state.knownSize);
    printSize("entry", state.entrySize);
    std::cout << '\n';
    std::cout << "  preview ready=" << flag(state.previewReady)
              << " quality=" << previewQualityName(state.previewQuality)
              << " hasImage=" << flag(state.previewHasImage);
    printSize("px", state.previewPx);
    printSize("source", state.previewSource);
    std::cout << "\n    requested=" << state.previewRequested.toStdString()
              << "\n    presented=" << state.previewPresented.toStdString() << '\n';
    std::cout << "  viewer expected=" << flag(state.viewerExpected)
              << " ready=" << flag(state.viewerReady) << " hidden=" << flag(state.viewerHidden)
              << " lod=" << flag(state.lod) << " frame=" << flag(state.frameValid);
    printSize("widget", state.viewerWidget);
    printSize("raster", state.rasterPx);
    printSize("display", state.displaySize);
    std::cout << "\n    path=" << state.viewerPath.toStdString() << '\n';
}

void runPhase(Results &results, Heartbeat &hb, MainWindow &window, ThumbnailPanel &panel,
              PreviewPanel &preview, ImageViewer *viewer, const char *phase, const QString &dir,
              const QString &png, bool selectFile = false)
{
    std::cout << "\n[phase " << phase << "] begin\n";
    PhaseState state;
    state.ticksBefore = hb.ticks;
    state.readsBefore = QtDecoder::fullImageReadCount();
    QElapsedTimer callTimer;
    callTimer.start();
    window.openExternalTargets({selectFile ? png : dir});
    state.callMs = callTimer.elapsed();
    std::cout << "  openExternalTargets " << state.callMs << " ms\n";
    awaitReady(hb, panel, preview, viewer, dir, png, state);
    printPhase(phase, state);

    check(results, state.callMs < kSwitchBudgetMs,
          phaseText(phase, "openExternalTargets returns within 1000 ms"));
    check(results, state.dirReady,
          phaseText(phase, "ThumbnailPanel currentDir and pathList match fixture count"));
    check(results, state.thumbReady,
          phaseText(phase, "all gallery thumbnails have pixels and selected source size matches"));
    check(results, state.previewReady,
          phaseText(phase, "preview shows decoded pixels for the requested image"));
    check(results, state.heartbeatMoved,
          phaseText(phase, "GUI heartbeat keeps firing during directory work"));
    check(results, state.gapMs < kGapBudgetMs,
          phaseText(phase, "largest GUI heartbeat gap stays under 1000 ms"));
    check(results, viewer != nullptr && !viewer->isHidden(), "visible viewer is mandatory");
    if (qEnvironmentVariableIntValue("MVIEWER_LARGE_VIEW") != 0)
        check(results, viewer && viewer->width() >= 1920 && viewer->devicePixelRatioF() >= 2,
              "large view must really request a 4096-edge first raster");
    if (viewer == nullptr)
    {
        std::cout << "  obstacle: no ImageViewer in QApplication::topLevelWidgets(). "
                     "MainWindow does not expose the viewer. Display raster was not checked.\n";
    }
    else if (viewer->isHidden())
    {
        std::cout << "  obstacle: ImageViewer::show() left the widget hidden. "
                     "onCurrentImageChanged requests a display raster only when the viewer "
                     "is not hidden, and MainWindow has no public force-show. "
                     "Display raster was not checked.\n";
    }
    else
    {
        check(results, state.viewerReady,
              phaseText(phase, "viewer current path and displayed raster match requested image"));
    }
}

struct PoolName
{
    TaskScheduler::PoolType pool;
    const char *name;
};

void drainLiveWork(Results &results, MainWindow &window, ImageViewer *viewer)
{
    // Caller has stopped the heartbeat. drain() blocks this thread, so a
    // still-running heartbeat would treat the join as a GUI stall.
    // ThumbnailPanel's scan pool is private. The panel destructor joins it
    // when the window dies, after this function returns.
    window.close();
    if (viewer != nullptr)
        viewer->close();
    pump(50);

    auto &scheduler = TaskScheduler::instance();
    const PoolName pools[] = {
        {TaskScheduler::MetadataPool, "MetadataPool"},
        {TaskScheduler::DecodePool, "DecodePool"},
        {TaskScheduler::ThumbnailPool, "ThumbnailPool"},
        {TaskScheduler::AnalysisPool, "AnalysisPool"},
        {TaskScheduler::IOPool, "IOPool"},
    };
    for (const PoolName &entry : pools)
    {
        const bool drained = scheduler.drain(entry.pool, std::chrono::seconds(kDrainSeconds));
        std::cout << "  drain " << entry.name << ' ' << (drained ? "ok" : "timeout") << '\n';
        check(results, drained, std::string(entry.name) + " drained before window destruction");
    }
    pump(50);
    mviewer::core::waitForDisplayResampleIdle();
    pump(50);
}

int runWindow(const Fixtures &fixtures)
{
    Results results;
    Heartbeat heartbeat;
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    ImageViewer *viewer = findImageViewer();
    if (viewer != nullptr)
    {
        const bool large = qEnvironmentVariableIntValue("MVIEWER_LARGE_VIEW") != 0;
        if (large)
            viewer->setMinimumSize(1920, 1080); // Do not let show/fit clamp to CI's small screen.
        viewer->resize(large ? 1920 : 960, large ? 1080 : 540);
        viewer->show();
    }
    // Let ctor timers (disabled recovery / update check) settle outside the
    // measured switches.
    pump(80);

    auto *panel = window.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    auto *preview = window.findChild<PreviewPanel *>();
    check(results, panel != nullptr, "MainWindow publishes thumbnailPanel");
    check(results, preview != nullptr, "MainWindow publishes PreviewPanel");
    if (panel == nullptr || preview == nullptr)
    {
        drainLiveWork(results, window, viewer);
        return results.fail == 0 ? 0 : 1;
    }

    std::cout << "fullImageReadCount before switches: " << QtDecoder::fullImageReadCount() << '\n';
    std::cout << "case=" << (kJpegCase ? "jpeg" : "png") << " expected_files=" << kFileCount
              << " dpr=" << (viewer ? viewer->devicePixelRatioF() : 0) << '\n';
    if (viewer == nullptr)
    {
        std::cout << "obstacle: ImageViewer was not among top-level widgets after MainWindow "
                     "construction. Gallery and preview still run.\n";
    }
    else if (viewer->isHidden())
    {
        std::cout << "obstacle: viewer stayed hidden after show(); display raster will not be "
                     "requested by directory navigation.\n";
    }

    heartbeat.start();
    runPhase(results, heartbeat, window, *panel, *preview, viewer, "A", fixtures.dirA,
             fixtures.pngA);
    runPhase(results, heartbeat, window, *panel, *preview, viewer, "B", fixtures.dirB,
             fixtures.pngB);
    runPhase(results, heartbeat, window, *panel, *preview, viewer, "A-again", fixtures.dirA,
             fixtures.pngA);
    if (kJpegCase)
    {
        for (int i = 2; i <= 4; ++i)
        {
            const QString path =
                QDir(fixtures.dirA).filePath(QStringLiteral("sample%1.jpg").arg(i));
            const QByteArray phase = QStringLiteral("JPEG-%1").arg(i).toUtf8();
            runPhase(results, heartbeat, window, *panel, *preview, viewer, phase.constData(),
                     fixtures.dirA, path, true);
        }
    }
    std::cout << "fullImageReadCount after switches: " << QtDecoder::fullImageReadCount() << '\n';
    heartbeat.stop();
    drainLiveWork(results, window, viewer);

    std::cout << "\n=== Results: " << results.pass << " passed, " << results.fail
              << " failed ===\n";
    return results.fail == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    TimedApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);

    // Joined after runWindow returns, while QApplication is still alive.
    // runWindow drains scheduler pools and the display-resample thread before
    // its MainWindow (and the viewer it owns) are destroyed.
    mviewer::application::ScopedProcessShutdown shutdown;

    isolateSettings();
    QTemporaryDir temp;
    Fixtures fixtures;
    if (!prepareFixtures(temp, fixtures))
    {
        std::cout << "png_directory_responsive_tests: FAIL (fixture)\n";
        return 1;
    }
    std::cout << "fixture " << kPngWidth << 'x' << kPngHeight << " RGB888\n";
    std::cout << "  A " << fixtures.pngA.toStdString() << '\n';
    std::cout << "  B " << fixtures.pngB.toStdString() << '\n';

    const int code = runWindow(fixtures);
    if (code == 0)
        std::cout << "png_directory_responsive_tests: PASS\n";
    else
        std::cout << "png_directory_responsive_tests: FAIL\n";
    return code;
}

// NOLINTEND(readability-magic-numbers)
