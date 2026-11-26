// CompareAdjustedExportJob: decode at full resolution -> apply the pane's
// adjustments off-thread -> write the file, and report through the status
// callback on the owning thread.
#include "compareworkspace_adjusted_export.h"

#include "core/async/AsyncLifetimeToken.h"
#include "core/image/CellImageAdjust.h"
#include "core/image/Decoder.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <functional>
#include <iostream>

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
    } while (false)

QString writeSource(QTemporaryDir &dir)
{
    QImage image(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb((x * 4) % 256, (y * 3) % 256, ((x + y) * 2) % 256));
    const QString path = dir.filePath(QStringLiteral("source.png"));
    return image.save(path, "PNG") ? path : QString();
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        // The job marshals its progress and status back to this thread, so the
        // event loop has to spin for it to get anywhere.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    const QString source = writeSource(dir);
    CHECK(!source.isEmpty(), "source fixture written");
    const QString outPath = dir.filePath(QStringLiteral("tuned.png"));

    QStringList messages;
    std::atomic<int> statusCount{0};
    auto lifetime = std::make_shared<mviewer::core::AsyncLifetimeToken>();

    mviewer::ui::CompareAdjustedExportJob job(nullptr, lifetime,
                                              [&](const QString &text, int)
                                              {
                                                  ++statusCount;
                                                  messages.append(text);
                                              });
    CHECK(job.thread() != nullptr, "job has an owning thread");

    mviewer::core::CellImageAdjust adjust;
    adjust.brightness = 24; // a tonal change that must survive the round trip
    adjust.usmEnabled = false;
    job.start(source.toUtf8().toStdString(), 0, outPath, adjust);

    CHECK(waitFor([&] { return QFile::exists(outPath); }, 20000),
          "adjusted export writes the output file");
    CHECK(waitFor([&] { return statusCount.load() > 0; }, 20000),
          "export reports its result");
    CHECK(messages.join(QLatin1Char(' ')).contains(QStringLiteral("Tuning result exported")),
          "success is reported through the status callback");
    CHECK(!messages.join(QLatin1Char(' ')).contains(QStringLiteral("failed")),
          "no failure message on the happy path");

    if (QFile::exists(outPath))
    {
        const QImage written = QImage(outPath);
        CHECK(!written.isNull() && written.width() == 96 && written.height() == 64,
              "exported image keeps the full source resolution");
        const QImage original(source);
        const int before = qGray(original.pixel(40, 30));
        const int after = qGray(written.pixel(40, 30));
        CHECK(after - before >= 20, "the pane adjustment is applied before encoding");
    }

    // A second export supersedes the first; cancel must be safe at any point.
    job.start(source.toUtf8().toStdString(), 0, dir.filePath(QStringLiteral("second.png")), adjust);
    job.cancel();
    CHECK(true, "cancel after start does not crash");
    job.cancel();
    CHECK(true, "repeated cancel does not crash");

    if (g_failures != 0)
    {
        std::cout << "compare_adjusted_export_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "compare_adjusted_export_tests: PASS\n";
    return 0;
}
