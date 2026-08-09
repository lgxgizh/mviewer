// M24 Phase 7 鈥?performance & resource-stability soak tool.
//
// Uses REAL-scale data (synthesized on disk) instead of micro samples:
//   S1  10000-image directory: scan -> first thumbnail latency
//   S2  rapid folder switching (20 x 5000-image dirs): switch latency
//   S3  UI-thread stall monitor during full 10000-image load
//   S4  24 MP JPEG full decode (cold)
//   S5  4000x4000 TIFF full decode (cold)
//   S6  corrupt-mixed 1000-image directory: full browse + clean exit
//   S7  steady-state memory / thread / handle counts + exit time
//   S8  50-cycle Compare UI mode/session loop + resource-growth report
//   S9  500-cycle Workspace disk serialize/deserialize round-trip
//
// M25 RC convergence additions (T scenarios, Professional Browse pipeline):
//   T1  thumbnail size churn (64/240/custom, cold+warm, decode in flight)
//   T2  directory churn with background dimension + metadata indexing work
//   T3  RAW-only / mixed-format gallery-vs-FileSystem count consistency
//   T4  metadata workload (sort / search / filter / recursive) UI stalls
//
// NOT a CTest gate (keeps CI fast); run manually / nightly:
//   build_msvc\bin\mviewer_m24_soak.exe [--out results.json]
// Exit code 0 = all scenarios completed without crash.

#include "compareworkspace.h"
#include "core/filesystem/FileSystem.h"
#include "core/image/ImageRepository.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "core/workspace/WorkspaceSerializer.h"
#include "domain/CompareSession.h"
#include "domain/Workspace.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
struct Sample
{
    QString name;
    double ms = 0;
};

QString g_outFile;

void report(const QList<Sample> &samples)
{
    QJsonArray arr;
    std::cout << "== M24 soak results ==\n";
    for (const auto &s : samples)
    {
        std::cout << "  " << s.name.toStdString() << " = " << s.ms << " ms\n";
        arr.append(QJsonObject{{"name", s.name}, {"ms", s.ms}});
    }
    if (!g_outFile.isEmpty())
    {
        QFile f(g_outFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(arr).toJson());
    }
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

QString synthDir(const QString &tag, int count, int w, int h, int corruptEvery = 0)
{
    const QString dir = QDir::tempPath() + "/mviewer_soak_" + tag + "_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    QImage img(w, h, QImage::Format_RGB32);
    for (int i = 0; i < count; ++i)
    {
        const QString name = QString("img_%1.png").arg(i, 5, 10, QChar('0'));
        if (corruptEvery > 0 && (i % corruptEvery) == 0)
        {
            QFile f(dir + "/" + name);
            f.open(QIODevice::WriteOnly);
            f.write("\x89PNG\r\n\x1a\n not a real png", 24);
            continue;
        }
        img.fill(QColor((i * 7) & 0xFF, (i * 13) & 0xFF, (i * 29) & 0xFF));
        img.save(dir + "/" + name, "PNG");
    }
    return dir;
}

void waitForEntryCount(ThumbnailPanel &panel, int expected, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        pump(10);
        if (panel.entries().size() >= expected)
            return;
    }
}

// Minimal little-endian TIFF (DNG-like) carrying ISO/Make/Model/LensModel so
// parseRawMetadata extracts real camera/lens/ISO fields for the T4 workload.
bool writeFakeDng(const std::string &path, const std::string &make,
                  const std::string &model, const std::string &lens, uint16_t iso)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
    std::fwrite(hdr, 1, 8, f);
    const uint16_t count = 4;
    std::fwrite(&count, 2, 1, f);
    const long ifdStart = 8;
    long dataOffset = ifdStart + 2 + count * 12 + 4;
    const long makeOff = dataOffset;
    const long modelOff = makeOff + static_cast<long>(make.size()) + 1;
    const long lensOff = modelOff + static_cast<long>(model.size()) + 1;
    auto writeEntry = [&](uint16_t tag, uint16_t type, uint32_t cnt, uint32_t val)
    {
        std::fwrite(&tag, 2, 1, f);
        std::fwrite(&type, 2, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        std::fwrite(&val, 4, 1, f);
    };
    writeEntry(0x8827, 3, 1, iso);                                       // ISO (SHORT inline)
    writeEntry(0x010F, 2, static_cast<uint32_t>(make.size()) + 1,
               static_cast<uint32_t>(makeOff)); // Make (ASCII)
    writeEntry(0x0110, 2, static_cast<uint32_t>(model.size()) + 1,
               static_cast<uint32_t>(modelOff)); // Model (ASCII)
    writeEntry(0xA434, 2, static_cast<uint32_t>(lens.size()) + 1,
               static_cast<uint32_t>(lensOff)); // LensModel (ASCII)
    uint32_t nextIfd = 0;
    std::fwrite(&nextIfd, 4, 1, f);
    std::fseek(f, makeOff, SEEK_SET);
    std::fwrite(make.c_str(), 1, make.size() + 1, f);
    std::fwrite(model.c_str(), 1, model.size() + 1, f);
    std::fwrite(lens.c_str(), 1, lens.size() + 1, f);
    std::fclose(f);
    return true;
}

void sendKey(QWidget *target, Qt::Key key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    pump(25);
}

struct ProcessResources
{
    double rssMB = 0.0;
    double handles = 0.0;
    bool valid = false;
};

ProcessResources processResources()
{
    ProcessResources result;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    DWORD handles = 0;
    const bool memoryOk = GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    const bool handlesOk = GetProcessHandleCount(GetCurrentProcess(), &handles);
    result.rssMB = counters.WorkingSetSize / (1024.0 * 1024.0);
    result.handles = static_cast<double>(handles);
    result.valid = memoryOk && handlesOk;
#endif
    return result;
}

bool workspaceRoundTripMatches(const mviewer::domain::Workspace &expected,
                               const std::optional<mviewer::domain::Workspace> &actual)
{
    if (!actual || actual->rootPath != expected.rootPath || actual->folders.size() != 1 ||
        actual->comparedImages != expected.comparedImages ||
        actual->compareSessionJson.empty())
        return false;
    const auto &expectedFolder = expected.folders.front();
    const auto &folder = actual->folders.front();
    if (folder.path != expectedFolder.path || folder.name != expectedFolder.name ||
        folder.imageSet.images.size() != expectedFolder.imageSet.images.size())
        return false;
    for (size_t i = 0; i < expectedFolder.imageSet.images.size(); ++i)
    {
        const auto &want = expectedFolder.imageSet.images[i];
        const auto &got = folder.imageSet.images[i];
        if (got.filePath != want.filePath || got.fileName != want.fileName ||
            got.width != want.width || got.height != want.height || got.roiX != want.roiX ||
            got.roiY != want.roiY || got.roiW != want.roiW || got.roiH != want.roiH ||
            got.analysis != want.analysis)
            return false;
    }
    const auto session = mviewer::core::deserializeCompareSession(actual->compareSessionJson);
    return session && session->isValid() && session->imageIds == expected.comparedImages &&
           session->cells.size() == expected.comparedImages.size() && session->cols == 2 &&
           session->rows == 1 && session->syncMode == mviewer::domain::SyncMode::All &&
           session->sharedScale == 1.25 && session->sharedOffsetX == 3.0 &&
           session->sharedOffsetY == -2.0 && session->selection.active &&
           session->selection.synced &&
           session->selection.x == 10 && session->selection.y == 12 &&
           session->selection.w == 64 && session->selection.h == 48 &&
           session->threshold == 23 && session->blinkIntervalMs == 175 &&
           session->sidePanelVisible && session->layoutIndex == 2 && session->uniformScale;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--out" && i + 1 < argc)
            g_outFile = QString::fromLocal8Bit(argv[++i]);

    // MVIEWER_SOAK_1THREAD=1 caps every scheduler pool to one thread: isolates
    // CPU-starvation artifacts (low-core machines) from genuine UI stalls.
    if (qEnvironmentVariableIsSet("MVIEWER_SOAK_1THREAD"))
    {
        auto &sched = TaskScheduler::instance();
        sched.setQueueMaxThreads(TaskScheduler::Priority::UI, 1);
        sched.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
        sched.setQueueMaxThreads(TaskScheduler::Priority::Thumbnail, 1);
        sched.setQueueMaxThreads(TaskScheduler::Priority::Background, 1);
        sched.setPoolMaxThreads(TaskScheduler::DecodePool, 1);
        std::cout << "[soak] single-thread scheduler mode\n";
    }
    if (qEnvironmentVariableIsSet("MVIEWER_TIMING"))
        std::cout << "[soak] timing instrumentation on\n";

    QList<Sample> samples;
    bool soakOk = true;

    // 鈹€鈹€ S1: 10000-image first-screen 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const QString dir = synthDir("s1", 10000, 24, 24);
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        QElapsedTimer t;
        t.start();
        panel.setDirectory(dir);
        // First thumbnail arrival: poll the ready map through the pipeline.
        double firstThumbMs = -1;
        QElapsedTimer poll;
        poll.start();
        while (poll.elapsed() < 20000)
        {
            pump(5);
            if (!panel.pathList().isEmpty())
            {
                firstThumbMs = t.elapsed();
                break;
            }
        }
        waitForEntryCount(panel, 10000, 30000);
        const double fullScanMs = t.elapsed();
        pump(2000);
        samples.append({"S1_10000_scan_ms", fullScanMs});
        samples.append({"S1_10000_first_entries_ms", firstThumbMs > 0 ? firstThumbMs : -1});
        panel.hide();
        panel.deleteLater();
        QDir(dir).removeRecursively();
        pump(100);
    }

    // 鈹€鈹€ S2: rapid folder switching (20 x 5000) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const QString a = synthDir("s2a", 5000, 24, 24);
        const QString b = synthDir("s2b", 5000, 24, 24);
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        pump(100);
        panel.setDirectory(a);
        waitForEntryCount(panel, 5000, 20000);
        QElapsedTimer t;
        double worst = 0;
        for (int i = 0; i < 20; ++i)
        {
            const QString &d = (i % 2) ? b : a;
            t.restart();
            panel.setDirectory(d);
            waitForEntryCount(panel, 5000, 15000);
            worst = qMax(worst, double(t.elapsed()));
        }
        samples.append({"S2_switch_worst_ms", worst});
        pump(2000);
        panel.hide();
        panel.deleteLater();
        QDir(a).removeRecursively();
        QDir(b).removeRecursively();
        pump(100);
    }

    // 鈹€鈹€ S3: UI-thread stall during full load 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const QString dir = synthDir("s3", 10000, 24, 24);
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        qint64 lastTick = 0;
        double maxGap = 0;
        QElapsedTimer ticker;
        ticker.start();
        QTimer monitor;
        monitor.setInterval(8);
        QObject::connect(&monitor, &QTimer::timeout, [&]()
                         {
                             const qint64 now = ticker.nsecsElapsed();
                             maxGap = qMax(maxGap, double(now - lastTick) / 1e6);
                             lastTick = now;
                         });
        monitor.start();
        QElapsedTimer t;
        t.start();
        panel.setDirectory(dir);
        const double setDirReturnMs = t.elapsed();
        // First entry arrival (scan completion).
        QElapsedTimer entriesT;
        entriesT.start();
        while (entriesT.elapsed() < 30000 && panel.entries().isEmpty())
            pump(5);
        const double firstEntriesMs = t.elapsed();
        waitForEntryCount(panel, 10000, 30000);
        const double fullEntriesMs = t.elapsed();
        pump(3000);
        const double settleMs = t.elapsed();
        monitor.stop();
        samples.append({"S3_setdir_return_ms", setDirReturnMs});
        samples.append({"S3_first_entries_ms", firstEntriesMs});
        samples.append({"S3_full_entries_ms", fullEntriesMs});
        samples.append({"S3_settle_ms", settleMs});
        samples.append({"S3_ui_stall_max_ms", double(maxGap)});
        panel.hide();
        panel.deleteLater();
        QDir(dir).removeRecursively();
        pump(100);
    }

    // ── S3b: bisect the stall — List view (no dimension pass) ──────────────
    {
        const QString dir = synthDir("s3b", 10000, 24, 24);
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.setViewMode(ThumbnailPanel::List); // skips ensureDimensions
        panel.show();
        pump(100);
        qint64 lastTick = 0;
        double maxGap = 0;
        QElapsedTimer ticker;
        ticker.start();
        QTimer monitor;
        monitor.setInterval(8);
        QObject::connect(&monitor, &QTimer::timeout, [&]()
                         {
                             const qint64 now = ticker.nsecsElapsed();
                             maxGap = qMax(maxGap, double(now - lastTick) / 1e6);
                             lastTick = now;
                         });
        monitor.start();
        QElapsedTimer t;
        t.start();
        panel.setDirectory(dir);
        waitForEntryCount(panel, 10000, 30000);
        const double fullMs = t.elapsed();
        pump(3000);
        monitor.stop();
        samples.append({"S3b_list_full_entries_ms", fullMs});
        samples.append({"S3b_list_ui_stall_max_ms", double(maxGap)});
        panel.hide();
        panel.deleteLater();
        QDir(dir).removeRecursively();
        pump(100);
    }

    // ── S3c: bisect the stall — zero-size viewport (no decode scheduling) ──
    {
        const QString dir = synthDir("s3c", 10000, 24, 24);
        ThumbnailPanel panel;
        panel.resize(0, 0); // nothing visible -> pipeline never schedules
        panel.show();
        pump(100);
        qint64 lastTick = 0;
        double maxGap = 0;
        QElapsedTimer ticker;
        ticker.start();
        QTimer monitor;
        monitor.setInterval(8);
        QObject::connect(&monitor, &QTimer::timeout, [&]()
                         {
                             const qint64 now = ticker.nsecsElapsed();
                             maxGap = qMax(maxGap, double(now - lastTick) / 1e6);
                             lastTick = now;
                         });
        monitor.start();
        QElapsedTimer t;
        t.start();
        panel.setDirectory(dir);
        waitForEntryCount(panel, 10000, 30000);
        const double fullMs = t.elapsed();
        pump(3000);
        monitor.stop();
        samples.append({"S3c_zeroview_full_entries_ms", fullMs});
        samples.append({"S3c_zeroview_ui_stall_max_ms", double(maxGap)});
        panel.hide();
        panel.deleteLater();
        QDir(dir).removeRecursively();
        pump(100);
    }

    // 鈹€鈹€ S4: 24 MP JPEG decode (cold) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const QString dir = QDir::tempPath() + "/mviewer_soak_s4_" +
                            QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        const QString file = dir + "/big24mp.jpg";
        if (!QFile::exists(file))
        {
            QImage img(6000, 4000, QImage::Format_RGB32);
            for (int y = 0; y < 4000; y += 4)
                for (int x = 0; x < 6000; x += 4)
                    img.setPixel(x, y, QColor((x * 7) & 0xFF, (y * 11) & 0xFF, 128).rgb());
            img.save(file, "JPEG", 90);
        }
        QElapsedTimer t;
        t.start();
        auto r = ImageRepository::instance().load(file.toStdString());
        const double ms = t.elapsed();
        samples.append({"S4_24mp_jpeg_decode_ms", ms});
        samples.append({"S4_24mp_jpeg_ok", r.success() ? 1.0 : 0.0});
        ImageRepository::instance().invalidateAll();
        QDir(dir).removeRecursively();
    }

    // 鈹€鈹€ S5: large TIFF decode (cold) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const QString dir = QDir::tempPath() + "/mviewer_soak_s5_" +
                            QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        const QString file = dir + "/big4k.tiff";
        if (!QFile::exists(file))
        {
            QImage img(4000, 4000, QImage::Format_RGB32);
            for (int y = 0; y < 4000; y += 4)
                for (int x = 0; x < 4000; x += 4)
                    img.setPixel(x, y, QColor((x * 5) & 0xFF, (y * 9) & 0xFF, 200).rgb());
            img.save(file, "TIFF");
        }
        QElapsedTimer t;
        t.start();
        auto r = ImageRepository::instance().load(file.toStdString());
        const double ms = t.elapsed();
        samples.append({"S5_4k_tiff_decode_ms", ms});
        samples.append({"S5_4k_tiff_ok", r.success() ? 1.0 : 0.0});
        ImageRepository::instance().invalidateAll();
        QDir(dir).removeRecursively();
    }

    // 鈹€鈹€ S6: corrupt-mixed browse + clean exit 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const QString dir = synthDir("s6", 1000, 24, 24, 20); // 5% corrupt
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        QElapsedTimer t;
        t.start();
        panel.setDirectory(dir);
        waitForEntryCount(panel, 1000, 15000);
        pump(3000);
        samples.append({"S6_corrupt_mixed_browse_ms", double(t.elapsed())});
        panel.hide();
        panel.deleteLater();
        pump(200);
        QDir(dir).removeRecursively();
    }

    // 鈹€鈹€ S7: steady-state resources + exit 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        const double rssMB = pmc.WorkingSetSize / (1024.0 * 1024.0);
        DWORD handleCount = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handleCount);
        samples.append({"S7_steady_rss_mb", rssMB});
        samples.append({"S7_handles", double(handleCount)});
#endif
    }

    // ── S8: Compare UI create/mode/session/destroy loop ──────────────────────
    {
        constexpr int kIterations = 50;
        constexpr int kWarmupIterations = 5;
        const QString dir = synthDir("s8", 8, 256, 256);
        const QDir imageDir(dir);
        QStringList paths;
        for (const QString &name : imageDir.entryList({"*.png"}, QDir::Files, QDir::Name))
            paths.append(imageDir.filePath(name));

        int completed = 0;
        double worstMs = 0.0;
        ProcessResources warmResources;
        for (int i = 0; i < kIterations && paths.size() >= 2; ++i)
        {
            QElapsedTimer round;
            round.start();
            bool valid = false;
            {
                CompareWorkspace workspace;
                workspace.resize(1100, 750);
                workspace.show();
                pump(30);
                workspace.setImages({paths[0], paths[1]});
                pump(30);
                for (Qt::Key key : {Qt::Key_S, Qt::Key_O, Qt::Key_K, Qt::Key_H, Qt::Key_B})
                    sendKey(&workspace, key);
                const auto session = workspace.compareSession();
                workspace.applySession(session);
                pump(30);
                valid = workspace.comparedImageCount() == 2 && session.isValid() &&
                        workspace.compareSession().isValid();
                workspace.close();
                pump(20);
            }
            // Drain queued diff completions only after the workspace subscription
            // and blink timer have been destroyed.
            pump(50);
            worstMs = qMax(worstMs, double(round.elapsed()));
            if (valid)
                ++completed;
            if (i == kWarmupIterations - 1)
                warmResources = processResources();
        }
        const ProcessResources finalResources = processResources();
        samples.append({"S8_compare_completed", double(completed)});
        samples.append({"S8_compare_worst_ms", worstMs});
        samples.append({"S8_compare_rss_growth_mb",
                        warmResources.valid && finalResources.valid
                            ? finalResources.rssMB - warmResources.rssMB
                            : -1.0});
        samples.append({"S8_compare_handle_growth",
                        warmResources.valid && finalResources.valid
                            ? finalResources.handles - warmResources.handles
                            : -1.0});
        if (completed != kIterations)
            soakOk = false;
        QDir(dir).removeRecursively();
        pump(100);
    }

    // ── T1: Thumbnail size churn (64/240/custom, cold+warm, decode in flight) ─
    {
        constexpr int kChurn = 24;
        const QString dir = synthDir("t1", 1000, 96, 64);
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        panel.setViewMode(ThumbnailPanel::Thumbnail);
        panel.setDirectory(dir);
        waitForEntryCount(panel, 1000, 20000);
        pump(1000); // let the first decode burst finish (warm cache)

        double worstStall = 0;
        qint64 lastTick = 0;
        QElapsedTimer ticker;
        ticker.start();
        QTimer monitor;
        monitor.setInterval(8);
        QObject::connect(&monitor, &QTimer::timeout, [&]()
                         {
                             const qint64 now = ticker.nsecsElapsed();
                             worstStall = qMax(worstStall, double(now - lastTick) / 1e6);
                             lastTick = now;
                         });
        monitor.start();
        int staleSized = 0;
        QElapsedTimer churn;
        churn.start();
        const ProcessResources warmResources = processResources();
        for (int i = 0; i < kChurn; ++i)
        {
            const int nextSize = (i % 4 == 0) ? 64 : (i % 4 == 1) ? 240 : (i % 4 == 2) ? 180 : 140;
            panel.setThumbSize(nextSize);
            // Churn again while decodes from the previous size are still in
            // flight (no settle between switches).
            if (i % 2 == 1)
                panel.setThumbSize((i % 3 == 0) ? 64 : 240);
            pump(30);
            // Correctness: any ready thumbnail must match the CURRENT size
            // (stale-size pixmaps are dropped on size change).
            if (i % 4 == 3)
            {
                for (const QString &p : panel.pathList())
                {
                    const QPixmap pm = panel.thumbReady(p);
                    if (!pm.isNull() && pm.width() != panel.thumbSize())
                        ++staleSized;
                }
            }
        }
        const double churnMs = churn.elapsed();
        monitor.stop();
        const ProcessResources finalResources = processResources();
        panel.hide();
        panel.deleteLater();
        QDir(dir).removeRecursively();
        pump(100);
        samples.append({"T1_size_churn_total_ms", churnMs});
        samples.append({"T1_size_churn_worst_ui_stall_ms", worstStall});
        samples.append({"T1_stale_sized_ready_thumbs", double(staleSized)});
        samples.append({"T1_rss_growth_mb",
                        warmResources.valid && finalResources.valid
                            ? finalResources.rssMB - warmResources.rssMB
                            : -1.0});
        if (staleSized != 0)
            soakOk = false;
    }

    // ── T2: Directory churn with background dimension + metadata work ────────
    {
        constexpr int kSwitches = 12;
        const QString a = synthDir("t2a", 1000, 48, 48);
        const QString b = synthDir("t2b", 1000, 48, 48);
        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        // Details view triggers the background dimension scan; a camera filter
        // triggers the async metadata index — both must abort fast on switch.
        panel.setViewMode(ThumbnailPanel::Details);
        panel.setCameraFilter("sony");
        pump(200);
        double worstFirstEntries = 0;
        double worstFull = 0;
        bool staleRows = false;
        for (int i = 0; i < kSwitches; ++i)
        {
            const QString &d = (i % 2) ? b : a;
            QElapsedTimer t;
            t.start();
            panel.setDirectory(d);
            waitForEntryCount(panel, 1000, 20000);
            const double firstMs = t.elapsed();
            worstFirstEntries = qMax(worstFirstEntries, firstMs);
            QElapsedTimer full;
            full.start();
            waitForEntryCount(panel, 1000, 20000);
            worstFull = qMax(worstFull, double(full.elapsed()));
            pump(150);
            // All settled rows must belong to the CURRENT directory.
            if (!staleRows)
            {
                for (const QString &p : panel.pathList())
                    if (!p.startsWith(d, Qt::CaseInsensitive))
                    {
                        staleRows = true;
                        break;
                    }
            }
        }
        samples.append({"T2_switch_worst_first_entries_ms", worstFirstEntries});
        samples.append({"T2_switch_worst_full_ms", worstFull});
        samples.append({"T2_stale_rows_from_old_dir", staleRows ? 1.0 : 0.0});
        panel.hide();
        panel.deleteLater();
        QDir(a).removeRecursively();
        QDir(b).removeRecursively();
        pump(100);
        if (staleRows)
            soakOk = false;
    }

    // ── T3: RAW-only / mixed-format directory consistency ────────────────────
    {
        const QString rawDir = QDir::tempPath() + "/mviewer_soak_t3raw_" +
                               QString::number(QCoreApplication::applicationPid());
        const QString mixedDir = QDir::tempPath() + "/mviewer_soak_t3mix_" +
                                 QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(rawDir);
        QDir().mkpath(mixedDir);
        const QStringList rawExts = {"cr2", "dng", "nef", "arw", "raf", "rw2", "orf"};
        for (int i = 0; i < 300; ++i)
        {
            QFile f(rawDir + QString("/r_%1.%2").arg(i).arg(rawExts[i % rawExts.size()]));
            f.open(QIODevice::WriteOnly);
            f.close();
            const QString ext = (i % 5 == 0) ? "jpg" : (i % 5 == 1) ? "webp" : (i % 5 == 2) ? "gif"
                                      : (i % 5 == 3)   ? "dng"
                                                       : "png";
            QFile g(mixedDir + QString("/m_%1.%2").arg(i).arg(ext));
            g.open(QIODevice::WriteOnly);
            g.close();
        }
        // 300 txt decoys must be excluded by the SSOT.
        QFile decoy(rawDir + "/decoys.txt");
        decoy.open(QIODevice::WriteOnly);
        decoy.close();

        const int fsRawCount = static_cast<int>(
            FileSystem::listImages(rawDir.toStdString(), 0).size());
        const int fsMixedCount = static_cast<int>(
            FileSystem::listImages(mixedDir.toStdString(), 0).size());

        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        panel.setViewMode(ThumbnailPanel::Thumbnail);
        panel.setDirectory(rawDir);
        waitForEntryCount(panel, 300, 20000);
        const int panelRawCount = panel.entries().size();
        panel.setDirectory(mixedDir);
        waitForEntryCount(panel, 300, 20000);
        const int panelMixedCount = panel.entries().size();
        panel.hide();
        panel.deleteLater();
        QDir(rawDir).removeRecursively();
        QDir(mixedDir).removeRecursively();
        pump(100);

        samples.append({"T3_raw_gallery_count", double(panelRawCount)});
        samples.append({"T3_raw_fs_count", double(fsRawCount)});
        samples.append({"T3_mixed_gallery_count", double(panelMixedCount)});
        samples.append({"T3_mixed_fs_count", double(fsMixedCount)});
        if (panelRawCount != fsRawCount || panelMixedCount != fsMixedCount)
            soakOk = false;
    }

    // ── T4: Metadata workload (sort / search / filter / recursive) ───────────
    {
        const QString dir = QDir::tempPath() + "/mviewer_soak_t4_" +
                            QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        // 2000 files: half plain PNG, half fake DNGs with camera/lens/ISO tags.
        QImage png(32, 32, QImage::Format_RGB32);
        for (int i = 0; i < 2000; ++i)
        {
            if (i % 2 == 0)
            {
                png.fill(QColor((i * 7) & 0xFF, (i * 13) & 0xFF, 0));
                png.save(dir + QString("/p_%1.png").arg(i), "PNG");
            }
            else
            {
                const int iso = 100 * (1 + (i % 20));
                writeFakeDng((dir + QString("/m_%1.dng").arg(i)).toStdString(),
                             (i % 4 == 1) ? "SONY" : "NIKON",
                             QString("BODY%1").arg(i % 5).toStdString(),
                             QString("LENS%1").arg(i % 7).toStdString(),
                             static_cast<uint16_t>(iso));
            }
        }
        QDir(dir).mkpath("sub");
        for (int i = 0; i < 50; ++i)
        {
            QImage sub(16, 16, QImage::Format_RGB32);
            sub.fill(QColor(0, (i * 9) & 0xFF, (i * 5) & 0xFF));
            sub.save(dir + QString("/sub/s_%1_target.png").arg(i), "PNG");
        }

        ThumbnailPanel panel;
        panel.resize(1200, 800);
        panel.show();
        panel.setViewMode(ThumbnailPanel::Thumbnail);
        panel.setDirectory(dir);
        waitForEntryCount(panel, 2000, 30000);
        pump(500);

        double worstStall = 0;
        qint64 lastTick = 0;
        QElapsedTimer ticker;
        ticker.start();
        QTimer monitor;
        monitor.setInterval(8);
        QObject::connect(&monitor, &QTimer::timeout, [&]()
                         {
                             const qint64 now = ticker.nsecsElapsed();
                             worstStall = qMax(worstStall, double(now - lastTick) / 1e6);
                             lastTick = now;
                         });
        monitor.start();
        QElapsedTimer work;
        work.start();

        // Sort churn: camera/lens/resolution sorts re-scan the directory
        // (off-thread) and must never freeze the UI.
        for (int i = 0; i < 3; ++i)
        {
            panel.setSortMode(ThumbnailPanel::SortCamera);
            pump(50);
            panel.setSortMode(ThumbnailPanel::SortLens);
            pump(50);
            panel.setSortMode(ThumbnailPanel::SortResolution);
            pump(50);
        }
        panel.setSortMode(ThumbnailPanel::SortName);
        pump(50);

        // Metadata search (async index) + field filters.
        panel.setMetaSearch(true);
        panel.setFilter("sony");
        pump(3000); // wait for the async index
        panel.setFilter(QString());
        panel.setCameraFilter("sony");
        pump(2000);
        const int cameraFiltered = panel.pathList().size();
        panel.setCameraFilter(QString());
        panel.setLensFilter("LENS1");
        pump(1500);
        const int lensFiltered = panel.pathList().size();
        panel.setLensFilter(QString());
        panel.setIsoFilter(800);
        pump(1500);
        const int isoFiltered = panel.pathList().size();
        panel.setIsoFilter(0);
        pump(200);

        // Recursive filename search (off-thread walk) is a distinct mode from
        // metadata search — switch it off like the UI checkbox does.
        panel.setMetaSearch(false);
        panel.setFilter("target", true);
        pump(2000);
        const int recursiveFound = panel.pathList().size();
        panel.setFilter(QString());
        pump(200);

        monitor.stop();
        const double workMs = work.elapsed();
        samples.append({"T4_metadata_work_total_ms", workMs});
        samples.append({"T4_worst_ui_stall_ms", worstStall});
        samples.append({"T4_camera_filter_count", double(cameraFiltered)});
        samples.append({"T4_lens_filter_count", double(lensFiltered)});
        samples.append({"T4_iso_filter_count", double(isoFiltered)});
        samples.append({"T4_recursive_search_count", double(recursiveFound)});
        panel.hide();
        panel.deleteLater();
        QDir(dir).removeRecursively();
        pump(100);
        // 1000 DNGs (odd i): SONY when i%4==1 → 500 files; NIKON the other
        // 500. ISO 800 → 100*(1+i%20)==800 → i%20==7 → 100 files. Recursive
        // filename search finds the 50 "sub/s_*_target.png" files.
        if (cameraFiltered < 450 || cameraFiltered > 550 || lensFiltered <= 0 ||
            isoFiltered != 100 || recursiveFound != 50)
        {
            std::cerr << "T4 filter/sort counts unexpected (camera=" << cameraFiltered
                      << " lens=" << lensFiltered << " iso=" << isoFiltered
                      << " recursive=" << recursiveFound << ")\n";
            soakOk = false;
        }
    }

    // ── T5: Scheduler dependency/cancel/deadline churn ───────────────────────
    // Every lifecycle path (success, cancelTree subtree, deadline expiry,
    // deferred release) exercised in a loop; every pool must converge to zero
    // pending/waiting/active/queue_depth after each drain.
    {
        constexpr int kIterations = 120;
        auto &sched = TaskScheduler::instance();
        int convergeFailures = 0;
        int deadlocks = 0;
        for (int i = 0; i < kIterations; ++i)
        {
            switch (i % 4)
            {
            case 0: // dependency chain
            {
                std::atomic<bool> c1{false}, c2{false};
                auto a = sched.submit(TaskScheduler::Priority::Background,
                                      [](const TaskScheduler::TaskContext &) {});
                auto b = sched.submit(TaskScheduler::Priority::Background,
                                      [&](const TaskScheduler::TaskContext &) { c1 = true; },
                                      {a->id});
                auto c = sched.submit(TaskScheduler::Priority::Background,
                                      [&](const TaskScheduler::TaskContext &) { c2 = true; },
                                      {b->id});
                if (!sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)))
                    ++deadlocks;
                if (!c1.load() || !c2.load())
                    ++convergeFailures;
                break;
            }
            case 1: // cancelTree subtree while running
            {
                std::atomic<bool> leafRan{false};
                auto a = sched.submit(
                    TaskScheduler::Priority::Background,
                    [](const TaskScheduler::TaskContext &ctx)
                    {
                        for (int k = 0; k < 1000 && !ctx.isCancelled(); ++k)
                            std::this_thread::sleep_for(std::chrono::microseconds(200));
                    });
                auto b = sched.submit(TaskScheduler::Priority::Background,
                                      [](const TaskScheduler::TaskContext &) {},
                                      {a->id});
                auto c = sched.submit(TaskScheduler::Priority::Background,
                                      [&](const TaskScheduler::TaskContext &) { leafRan = true; },
                                      {b->id});
                TaskScheduler::cancelTree(a->id);
                if (!sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)))
                    ++deadlocks;
                if (leafRan.load())
                    ++convergeFailures;
                break;
            }
            case 2: // deadline expired before start
            {
                std::atomic<bool> ran{false};
                auto h = sched.submit(
                    TaskScheduler::Priority::Background,
                    [&](const TaskScheduler::TaskContext &) { ran = true; },
                    {},
                    std::chrono::steady_clock::now() - std::chrono::seconds(1));
                if (!sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)))
                    ++deadlocks;
                if (ran.load())
                    ++convergeFailures;
                break;
            }
            case 3: // deferred released at submit time + immediate reuse
            {
                auto a = sched.submit(TaskScheduler::Priority::Background,
                                      [](const TaskScheduler::TaskContext &) {});
                sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5));
                auto b = sched.submit(TaskScheduler::Priority::Background,
                                      [](const TaskScheduler::TaskContext &) {},
                                      {a->id});
                if (!sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)))
                    ++deadlocks;
                break;
            }
            }
            // Post-drain convergence on the pool that was exercised.
            const auto m = sched.metrics(TaskScheduler::MetadataPool);
            if (m.pending != 0 || m.waiting != 0 || m.active_tasks != 0 || m.queue_depth != 0)
                ++convergeFailures;
        }
        samples.append({"T5_scheduler_churn_converge_failures", double(convergeFailures)});
        samples.append({"T5_scheduler_churn_drain_timeouts", double(deadlocks)});
        if (convergeFailures != 0 || deadlocks != 0)
            soakOk = false;
    }

    // ── T6: Scheduler saturation / rejection / drain ─────────────────────────
    // A low queue-depth pool under flood: submissions must be rejected cleanly
    // (no pending residue), and drain must stay bounded afterwards.
    {
        auto &sched = TaskScheduler::instance();
        sched.setPoolMaxThreads(TaskScheduler::DecodePool, 2);
        sched.setMaxQueueDepth(TaskScheduler::DecodePool, 4);
        uint64_t rejected = 0;
        for (int round = 0; round < 20; ++round)
        {
            // Occupy both threads so the flood hits the depth cap.
            std::atomic<int> blockers{0};
            std::mutex bm;
            std::condition_variable bc;
            for (int k = 0; k < 2; ++k)
                sched.submit(TaskScheduler::Priority::Decode,
                             [&](const TaskScheduler::TaskContext &)
                             {
                                 {
                                     std::lock_guard<std::mutex> lk(bm);
                                     blockers++;
                                 }
                                 bc.notify_all();
                                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
                             });
            {
                std::unique_lock<std::mutex> lk(bm);
                bc.wait(lk, [&] { return blockers == 2; });
            }
            for (int k = 0; k < 200; ++k)
            {
                auto h = sched.submit(TaskScheduler::Priority::Decode,
                                      [](const TaskScheduler::TaskContext &) {});
                if (!h)
                    ++rejected;
            }
            if (!sched.drain(TaskScheduler::DecodePool, std::chrono::seconds(10)))
            {
                soakOk = false;
                samples.append({"T6_saturation_drain_timeout", 1.0});
                break;
            }
        }
        const auto m = sched.metrics(TaskScheduler::DecodePool);
        samples.append({"T6_saturation_rejected", double(rejected)});
        samples.append({"T6_saturation_pending_after", double(m.pending)});
        samples.append({"T6_saturation_active_after", double(m.active_tasks)});
        samples.append({"T6_saturation_depth_after", double(m.queue_depth)});
        if (rejected == 0 || m.pending != 0 || m.active_tasks != 0 || m.queue_depth != 0)
            soakOk = false;
        sched.setMaxQueueDepth(TaskScheduler::DecodePool, 1000);
        sched.setPoolMaxThreads(TaskScheduler::DecodePool, qMax(1, QThread::idealThreadCount()));
    }

    // ── T7: MetadataIndexer dual-consumer race ───────────────────────────────
    // MainWindow-style search re-index and gallery-filter-style indexing race
    // on the same directory repeatedly; both must always complete and the
    // shared cache must stay within its bound.
    {
        const QString dir = QDir::tempPath() + "/mviewer_soak_t7_" +
                            QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        std::vector<std::string> paths;
        constexpr int kFiles = 1500;
        for (int i = 0; i < kFiles; ++i)
        {
            const QString p = dir + QString("/f_%1.dng").arg(i, 5, 10, QChar('0'));
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.close();
            paths.push_back(p.toStdString());
        }
        auto &indexer = mviewer::core::MetadataIndexer::instance();
        indexer.cancel();
        const size_t savedLimit = indexer.cacheLimit();
        indexer.setCacheLimit(3000); // bounded: > one directory's working set

        int racesWon = 0;
        constexpr int kRounds = 6;
        for (int round = 0; round < kRounds; ++round)
        {
            std::atomic<bool> searchDone{false};
            std::atomic<int> filterEntries{0};
            std::atomic<bool> filterDone{false};
            const uint64_t r1 = indexer.index(paths, {}, [&]() { searchDone = true; });
            const uint64_t r2 = indexer.index(
                paths,
                [&](const mviewer::core::MetadataIndexEntry &) { filterEntries.fetch_add(1); },
                [&]() { filterDone = true; });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while ((!searchDone.load() || !filterDone.load()) &&
                   std::chrono::steady_clock::now() < deadline)
                pump(5);
            if (searchDone.load() && filterDone.load() &&
                filterEntries.load() == kFiles)
                ++racesWon;
        }
        samples.append({"T7_dual_consumer_races_won", double(racesWon)});
        samples.append({"T7_cache_size", double(indexer.size())});
        if (racesWon != kRounds)
            soakOk = false;
        if (indexer.size() > indexer.cacheLimit())
            soakOk = false;
        indexer.setCacheLimit(savedLimit);
        indexer.cancel();
        QDir(dir).removeRecursively();
    }

    // ── T8: Thumbnail rapid generation churn + real backpressure ─────────────
    {
        auto &sched = TaskScheduler::instance();
        sched.setPoolMaxThreads(TaskScheduler::ThumbnailPool, 1);
        const size_t savedDepth = sched.maxQueueDepth(TaskScheduler::ThumbnailPool);
        sched.setMaxQueueDepth(TaskScheduler::ThumbnailPool, 2); // force real backpressure

        ThumbnailPipeline pipe;
        // Slow-ish decode keeps the single worker busy so the depth-2 cap
        // produces real backpressure across the whole churn; the pump between
        // sweeps paces the harness like human scrolling.
        pipe.setDecodeFn([](const std::string &, int size) -> ImageData
                         {
                             std::this_thread::sleep_for(std::chrono::milliseconds(2));
                             return makeImageData(size, size, PixelFormat::RGB24);
                         });
        std::atomic<int> delivered{0};
        pipe.setResultFn([&](const std::string &, int, const ImageData &) { delivered.fetch_add(1); });

        constexpr int kDirs = 6;
        constexpr int kPerDir = 500;
        for (int d = 0; d < kDirs; ++d)
        {
            std::vector<std::string> src;
            for (int i = 0; i < kPerDir; ++i)
                src.push_back("t8dir" + std::to_string(d) + "/img" + std::to_string(i) + ".jpg");
            pipe.setSources(src);
            for (size_t i = 0; i + 20 <= 500; i += 20)
            {
                pipe.setVisibleRange(i, i + 20);
                pump(1);
            }
        }
        // Settle: re-request the current visible window so keys rejected under
        // backpressure get their retry, then let the pool finish.
        pipe.setVisibleRange(0, 20);
        sched.drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(30));
        const size_t pendingAfter = pipe.pendingCount();
        const size_t handlesAfter = pipe.handlesCount();
        samples.append({"T8_thumbnail_churn_delivered", double(delivered.load())});
        samples.append({"T8_thumbnail_pending_after", double(pendingAfter)});
        samples.append({"T8_thumbnail_handles_after", double(handlesAfter)});
        if (pendingAfter != 0 || handlesAfter > 64 || delivered.load() < 20)
            soakOk = false;
        sched.setMaxQueueDepth(TaskScheduler::ThumbnailPool, savedDepth);
        sched.setPoolMaxThreads(TaskScheduler::ThumbnailPool, qMax(2, QThread::idealThreadCount()));
    }

    // ── T9: Repository saturated async completion at scale ───────────────────
    {
        auto &sched = TaskScheduler::instance();
        sched.setPoolMaxThreads(TaskScheduler::DecodePool, 1);
        const size_t savedDepth = sched.maxQueueDepth(TaskScheduler::DecodePool);
        sched.setMaxQueueDepth(TaskScheduler::DecodePool, 2);

        const QString dir = QDir::tempPath() + "/mviewer_soak_t9_" +
                            QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        for (int i = 0; i < 40; ++i)
        {
            QImage img(16, 16, QImage::Format_RGB32);
            img.fill(QColor((i * 7) % 256, 90, 150));
            img.save(dir + QString("/i_%1.png").arg(i, 3, 10, QChar('0')), "PNG");
        }
        std::atomic<bool> blockerDone{false};
        // Two blockers fill the depth-2 cap so every directory submission is
        // rejected (the "1 accepted task waits forever" case is covered by the
        // M26 unit tests with an unblockable pool); the aggregate must fire
        // exactly once per round, instantly, with explicit failures.
        for (int k = 0; k < 2; ++k)
            sched.submit(TaskScheduler::Priority::Decode,
                         [&](const TaskScheduler::TaskContext &)
                         {
                             while (!blockerDone.load())
                                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
                         });
        int completedRounds = 0;
        constexpr int kRounds = 8;
        for (int round = 0; round < kRounds; ++round)
        {
            std::atomic<int> calls{0};
            std::atomic<int> resultsOk{0};
            ImageRepository::instance().loadDirectoryAsync(
                dir.toStdString(),
                [&](std::vector<ImageRepository::Result> r)
                {
                    calls.fetch_add(1);
                    int fail = 0;
                    for (const auto &res : r)
                        if (!res.success())
                            ++fail;
                    resultsOk.store(fail >= 38 ? 1 : 0); // most rejected -> explicit failures
                });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (calls.load() == 1 && resultsOk.load() == 1)
                ++completedRounds;
        }
        blockerDone = true;
        samples.append({"T9_repository_saturated_rounds_ok", double(completedRounds)});
        if (completedRounds != kRounds)
            soakOk = false;
        sched.drain(TaskScheduler::DecodePool, std::chrono::seconds(20));
        sched.setMaxQueueDepth(TaskScheduler::DecodePool, savedDepth);
        sched.setPoolMaxThreads(TaskScheduler::DecodePool, qMax(1, QThread::idealThreadCount()));
        QDir(dir).removeRecursively();
    }

    // ── T10: long-session resource trend ─────────────────────────────────────
    // Repeated browse cycles (list -> pipeline churn -> index) with resource
    // samples at the start and end: RSS, OS handles, scheduler handles,
    // pipeline bookkeeping, metadata cache.
    {
        auto &sched = TaskScheduler::instance();
        ThumbnailPipeline &pipe = ThumbnailPipeline::instance();
        pipe.clear();
        pipe.setDecodeFn([](const std::string &, int size) -> ImageData
                         { return makeImageData(size, size, PixelFormat::RGB24); });

        const QString dir = QDir::tempPath() + "/mviewer_soak_t10_" +
                            QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(dir);
        std::vector<std::string> paths;
        for (int i = 0; i < 800; ++i)
        {
            const QString p = dir + QString("/g_%1.dng").arg(i, 4, 10, QChar('0'));
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.close();
            paths.push_back(p.toStdString());
        }
        auto &indexer = mviewer::core::MetadataIndexer::instance();
        indexer.cancel();

        auto report = [&]() -> double
        {
            double schedulerHandles = 0;
            for (int p = 0; p < 5; ++p)
                schedulerHandles += 0; // live handles are per-pool metrics below
            const auto bg = sched.metrics(TaskScheduler::MetadataPool);
            const auto th = sched.metrics(TaskScheduler::ThumbnailPool);
            return bg.pending + th.pending;
        };

        const ProcessResources warm = processResources();
        for (int cycle = 0; cycle < 12; ++cycle)
        {
            std::vector<std::string> src;
            for (int i = 0; i < 800; ++i)
                src.push_back("t10/img" + std::to_string(i) + ".jpg");
            pipe.setSources(src);
            for (size_t i = 0; i + 30 <= 800; i += 30)
                pipe.setVisibleRange(i, i + 30);
            std::atomic<bool> done{false};
            indexer.index(paths, {}, [&]() { done = true; });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!done.load() && std::chrono::steady_clock::now() < deadline)
                pump(5);
            sched.drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(20));
            sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(20));
        }
        const ProcessResources final = processResources();
        const auto bg = sched.metrics(TaskScheduler::MetadataPool);
        const auto th = sched.metrics(TaskScheduler::ThumbnailPool);
        const double rssGrowth = (warm.valid && final.valid) ? final.rssMB - warm.rssMB : -1.0;
        const double handleGrowth = (warm.valid && final.valid) ? final.handles - warm.handles : -1.0;
        samples.append({"T10_rss_growth_mb", rssGrowth});
        samples.append({"T10_os_handle_growth", handleGrowth});
        samples.append({"T10_scheduler_pending_after", double(bg.pending + th.pending)});
        samples.append({"T10_scheduler_active_after", double(bg.active_tasks + th.active_tasks)});
        samples.append({"T10_thumbnail_pending_after", double(pipe.pendingCount())});
        samples.append({"T10_thumbnail_handles_after", double(pipe.handlesCount())});
        samples.append({"T10_metadata_cache_after", double(indexer.size())});
        // Convergence requirements: no accumulated scheduler/pipeline state;
        // metadata cache bounded by its limit; RSS growth bounded over 12 cycles.
        if (bg.pending != 0 || th.pending != 0 || bg.active_tasks != 0 || th.active_tasks != 0 ||
            pipe.pendingCount() != 0 || pipe.handlesCount() > 64 ||
            indexer.size() > indexer.cacheLimit() || rssGrowth > 60.0)
            soakOk = false;
        pipe.clear();
        indexer.cancel();
        QDir(dir).removeRecursively();
    }

    // ── S9: Workspace disk serialize/deserialize round-trip ──────────────────
    {
        constexpr int kIterations = 500;
        QTemporaryDir tempDir(QDir::tempPath() + "/mviewer_soak_s9_XXXXXX");
        const QString workspacePath = tempDir.filePath("roundtrip.mvws");

        mviewer::domain::Workspace source;
        source.rootPath = tempDir.path().toStdString();
        mviewer::domain::Folder folder;
        folder.path = source.rootPath + "/images";
        folder.name = "images";
        folder.imageSet.folderPath = folder.path;
        mviewer::domain::ImageMetadata first;
        first.filePath = folder.path + "/left.png";
        first.fileName = "left.png";
        first.width = 256;
        first.height = 256;
        first.roiX = 10;
        first.roiY = 12;
        first.roiW = 64;
        first.roiH = 48;
        first.analysis = "mean=127.5; psnr=42.0";
        mviewer::domain::ImageMetadata second = first;
        second.filePath = folder.path + "/right.png";
        second.fileName = "right.png";
        second.analysis = "mean=129.0; psnr=41.5";
        folder.imageSet.images = {first, second};
        source.folders.push_back(folder);
        source.comparedImages = {first.filePath, second.filePath};

        mviewer::domain::CompareSession session;
        session.imageIds = source.comparedImages;
        session.cells = {{1.25, 3.0, -2.0}, {1.25, -4.0, 5.0}};
        session.syncMode = mviewer::domain::SyncMode::All;
        session.sharedScale = 1.25;
        session.sharedOffsetX = 3.0;
        session.sharedOffsetY = -2.0;
        session.cols = 2;
        session.rows = 1;
        session.selection = {10, 12, 64, 48, true, true};
        session.threshold = 23;
        session.blinkIntervalMs = 175;
        session.sidePanelVisible = true;
        session.layoutIndex = 2;
        session.uniformScale = true;
        source.compareSessionJson = mviewer::core::serializeCompareSession(session);

        int completed = 0;
        double worstMs = 0.0;
        QElapsedTimer total;
        total.start();
        for (int i = 0; i < kIterations && tempDir.isValid(); ++i)
        {
            QElapsedTimer round;
            round.start();
            bool valid = true;
            const QByteArray bytes =
                QByteArray::fromStdString(mviewer::core::serializeWorkspace(source));
            QFile file(workspacePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                file.write(bytes) != bytes.size())
                valid = false;
            file.close();

            std::optional<mviewer::domain::Workspace> restored;
            if (valid && file.open(QIODevice::ReadOnly))
                restored = mviewer::core::deserializeWorkspace(file.readAll().toStdString());
            else
                valid = false;
            file.close();
            valid = valid && workspaceRoundTripMatches(source, restored);
            worstMs = qMax(worstMs, double(round.elapsed()));
            if (valid)
                ++completed;
        }
        const double totalMs = total.elapsed();
        samples.append({"S9_workspace_completed", double(completed)});
        samples.append({"S9_workspace_total_ms", totalMs});
        samples.append({"S9_workspace_worst_ms", worstMs});
        if (completed != kIterations)
            soakOk = false;
        const bool fileRemoved = !QFile::exists(workspacePath) || QFile::remove(workspacePath);
        const bool dirRemoved = tempDir.remove();
        if (!fileRemoved || !dirRemoved)
        {
            std::cerr << "S9 cleanup failed: " << tempDir.path().toStdString() << "\n";
            soakOk = false;
        }
    }

    QElapsedTimer exitT;
    exitT.start();
    QTimer::singleShot(0, &app, [&]() { app.quit(); });
    const int rc = app.exec();
    samples.append({"S7_exit_ms", double(exitT.elapsed())});

    report(samples);
    const bool passed = rc == 0 && soakOk;
    std::cout << "m24_soak: " << (passed ? "PASS" : "FAIL") << "\n";
    return passed ? 0 : 1;
}


