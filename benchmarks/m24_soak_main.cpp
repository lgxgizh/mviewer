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
//
// NOT a CTest gate (keeps CI fast); run manually / nightly:
//   build_msvc\bin\mviewer_m24_soak.exe [--out results.json]
// Exit code 0 = all scenarios completed without crash.

#include "core/image/ImageRepository.h"
#include "core/scheduler/TaskScheduler.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>
#include <iostream>
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

    QElapsedTimer exitT;
    exitT.start();
    QTimer::singleShot(0, &app, [&]() { app.quit(); });
    const int rc = app.exec();
    samples.append({"S7_exit_ms", double(exitT.elapsed())});

    report(samples);
    std::cout << "m24_soak: " << (rc == 0 ? "PASS" : "FAIL") << "\n";
    return rc;
}


