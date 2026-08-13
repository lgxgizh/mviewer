#include "core/image/QtConvert.h"
#include "core/compare/CompareEngine.h"
#include "core/metadata/MetadataPresentationService.h"
#include "core/scheduler/TaskScheduler.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QEventLoop>

#include <cstdio>
#include <string>
#include <vector>

using namespace std::chrono_literals;

static int g_fail = 0;
#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s\n", m);                                                         \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (0)

static mviewer::domain::ImageMetadata profileMeta(const QColorSpace &space)
{
    mviewer::domain::ImageMetadata meta;
    const QByteArray encoded = space.iccProfile().toBase64();
    meta.textKeys["MViewer.DisplayICC.Base64"] =
        std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
    meta.hasIccProfile = true;
    return meta;
}

static void checkSyncAxes()
{
    SyncController sync;
    sync.setCellCount(2);
    sync.setCellScale(0, 1.0);
    sync.setCellScale(1, 2.0);
    sync.setCellOffset(0, 10.0, 20.0);
    sync.setCellOffset(1, 30.0, 40.0);

    sync.setEnabled(false);
    sync.setZoomEnabled(true);
    sync.setDragEnabled(false);
    sync.setScale(3.0);
    sync.setOffset(50.0, 60.0);
    CHECK(sync.cell(0).scale == 3.0 && sync.cell(1).scale == 3.0,
          "Zoom-only propagates scale");
    CHECK(sync.cell(0).offset.x == 10.0 && sync.cell(1).offset.x == 30.0,
          "Zoom-only preserves pane offsets");

    sync.setZoomEnabled(false);
    sync.setDragEnabled(true);
    sync.setScale(4.0);
    sync.setOffset(70.0, 80.0);
    CHECK(sync.cell(0).scale == 3.0 && sync.cell(1).scale == 3.0,
          "Drag-only preserves pane scales");
    CHECK(sync.cell(0).offset.x == 70.0 && sync.cell(1).offset.x == 70.0,
          "Drag-only propagates offsets");

    sync.setEnabled(false);
    sync.zoomAt(0.0, 0.0, 2.0, 0);
    CHECK(sync.cell(0).scale == 6.0 && sync.cell(1).scale == 3.0,
          "Off keeps zoom independent per pane");
}

static void checkMetadataSingleFlight(int argc, char **argv)
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/single-flight.png";
    QImage fixture(8, 6, QImage::Format_RGB32);
    fixture.fill(qRgb(20, 40, 80));
    CHECK(fixture.save(path, "PNG"), "metadata fixture written");

    auto &service = mviewer::core::MetadataPresentationService::instance();
    auto &scheduler = TaskScheduler::instance();
    scheduler.drain(TaskScheduler::MetadataPool, 5s);
    const uint64_t submitted = scheduler.metrics(TaskScheduler::MetadataPool).submitted;
    int delivered = 0;
    bool firstValid = false;
    bool secondValid = false;
    service.request(path.toStdString(), "m36-consumer-a",
                    [&](const auto &snapshot)
                    {
                        ++delivered;
                        firstValid = snapshot.valid();
                    });
    service.request(path.toStdString(), "m36-consumer-b",
                    [&](const auto &snapshot)
                    {
                        ++delivered;
                        secondValid = snapshot.valid();
                    });
    CHECK(scheduler.metrics(TaskScheduler::MetadataPool).submitted == submitted + 1,
          "two metadata consumers share one flight");
    scheduler.drain(TaskScheduler::MetadataPool, 5s);
    QElapsedTimer timer;
    timer.start();
    while (delivered < 2 && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(delivered == 2 && firstValid && secondValid,
          "all metadata consumers receive one immutable result");
    CHECK(service.cached(path.toStdString()).has_value(),
          "metadata result is retained as a memory snapshot");
    service.cancel("m36-consumer-a");
    service.cancel("m36-consumer-b");
    Q_UNUSED(argc);
    Q_UNUSED(argv);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ImageData source = makeImageData(2, 1, PixelFormat::RGB24);
    (*source.buffer)[0] = 180;
    (*source.buffer)[1] = 90;
    (*source.buffer)[2] = 40;
    (*source.buffer)[3] = 20;
    (*source.buffer)[4] = 140;
    (*source.buffer)[5] = 220;
    const std::vector<uint8_t> before = *source.buffer;

    const std::vector<QColorSpace> spaces = {
        QColorSpace::SRgb, QColorSpace::AdobeRgb, QColorSpace::DisplayP3};
    for (const QColorSpace &space : spaces)
    {
        const auto meta = profileMeta(space);
        const QImage reference = mvcore::toDisplayQImage(source, meta);
        const ImageData display = mvcore::toDisplayImageData(source, meta);
        const QImage materialized = mvcore::toQImage(display);
        CHECK(!reference.isNull() && !materialized.isNull(), "display materialization is valid");
        CHECK(reference.pixelColor(0, 0) == materialized.pixelColor(0, 0),
              "ImageData tile equals display reference");
        CHECK(reference.pixelColor(1, 0) == materialized.pixelColor(1, 0),
              "second display sample equals display reference");
    }

    mviewer::domain::ImageMetadata noProfile;
    const QImage rawDisplay = mvcore::toDisplayQImage(source, noProfile);
    CHECK(rawDisplay.pixelColor(0, 0) == QColor(180, 90, 40),
          "no-profile display keeps numeric pixels");
    CHECK(*source.buffer == before, "analysis-domain bytes remain byte-identical");
    checkSyncAxes();
    checkMetadataSingleFlight(argc, argv);
    std::printf("M36 display tests: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
