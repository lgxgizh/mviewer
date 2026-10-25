// USM dialog keeps the source ICC profile and does not offer a RAW save suffix.
#include "usmcomparedialog.h"

#include "core/image/ImageBuffer.h"
#include "core/image/ImageFormats.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QByteArray>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace
{
int g_failures = 0;

void check(bool condition, const char *message)
{
    std::cout << (condition ? "[ok] " : "[FAIL] ") << message << "\n";
    if (!condition)
        ++g_failures;
}

void testDefaultSuffix()
{
    const QString rawExt = UsmCompareDialog::defaultSaveExtension(QStringLiteral("nef"));
    check(rawExt == QLatin1String("tif") || rawExt == QLatin1String("png"),
          "RAW default suffix is tif or png");
    check(!mviewer::core::isRawExtension(rawExt.toStdString()),
          "RAW default suffix is not a RAW extension");
    check(UsmCompareDialog::defaultSaveExtension(QStringLiteral("CR2")) == rawExt,
          "RAW suffix decision is case-insensitive");
    check(UsmCompareDialog::defaultSaveExtension(QStringLiteral("shot.nrw")) == rawExt,
          "RAW path uses the same default suffix");
    check(UsmCompareDialog::defaultSaveExtension(QStringLiteral("jpg")) == QLatin1String("jpg"),
          "JPEG keeps its suffix");
    check(UsmCompareDialog::defaultSaveExtension(QString()) == QLatin1String("png"),
          "empty suffix becomes png");
}

void testEmbeddedIcc()
{
    const std::vector<uint8_t> icc = {0x00, 0x01, 0x02, 0x61, 0x62, 0x63, 0x64};
    const QByteArray encoded =
        QByteArray(reinterpret_cast<const char *>(icc.data()), static_cast<int>(icc.size()))
            .toBase64();
    mviewer::domain::ImageMetadata meta;
    meta.textKeys["MViewer.DisplayICC.Base64"] =
        std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
    meta.width = 2;
    meta.height = 2;
    ImageData pixels = makeImageData(2, 2, PixelFormat::RGB24);
    UsmCompareDialog dlg(QStringLiteral("shot.nef"), pixels, nullptr, meta);
    check(dlg.embeddedIccProfile() == icc, "USM embeds the source ICC bytes");
}

void testBackgroundFailure()
{
    TaskScheduler::instance().setPoolMaxThreads(TaskScheduler::DecodePool, 2);
    QString title;
    QString body;
    std::thread::id loaderThread;
    UsmCompareDialog *live = nullptr;
    UsmCompareDialog::setFailurePresenter(
        [&](const QString &gotTitle, const QString &gotBody)
        {
            title = gotTitle;
            body = gotBody;
            if (live)
                live->reject();
        });
    auto loader = [&](const std::string &)
    {
        loaderThread = std::this_thread::get_id();
        UsmLoadedSource loaded;
        loaded.error = "boom";
        return loaded;
    };
    UsmCompareDialog dlg(QStringLiteral("missing.nef"), {}, nullptr, {},
                         UsmCompareDialog::OpenMode::Background, loader);
    live = &dlg;
    QTimer::singleShot(3000, &dlg, &QDialog::reject);
    dlg.exec();
    check(title == QStringLiteral("USM sharpen comparison"), "USM failure title");
    check(body == QStringLiteral("Cannot open missing.nef for USM sharpen comparison: boom"),
          "USM failure body");
    check(loaderThread != std::this_thread::get_id(), "USM loader leaves the UI thread");
    check(dlg.loadedOffUiThread(), "dialog records an off-UI load");
    UsmCompareDialog::setFailurePresenter({});
}

void testRejectedSubmit()
{
    auto &sched = TaskScheduler::instance();
    const size_t previous = sched.maxQueueDepth(TaskScheduler::DecodePool);
    sched.setMaxQueueDepth(TaskScheduler::DecodePool, 1);
    std::atomic<bool> hold{true};
    auto blocker = sched.submit(TaskScheduler::Priority::Decode,
                                [&](const TaskScheduler::TaskContext &)
                                {
                                    while (hold.load())
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                });
    QString body;
    bool loaderCalled = false;
    UsmCompareDialog::setFailurePresenter([&](const QString &, const QString &gotBody)
                                          { body = gotBody; });
    UsmCompareDialog dlg(QStringLiteral("blocked.nef"), {}, nullptr, {},
                         UsmCompareDialog::OpenMode::Background,
                         [&](const std::string &)
                         {
                             loaderCalled = true;
                             return UsmLoadedSource{};
                         });
    check(!loaderCalled, "rejected USM submit does not run the loader");
    const QString expect = QStringLiteral("Cannot open blocked.nef for USM sharpen comparison: ") +
                           QStringLiteral("scheduler rejected submission");
    check(body == expect, "rejected USM submit surfaces a failure");
    hold.store(false);
    TaskScheduler::cancel(blocker);
    sched.setMaxQueueDepth(TaskScheduler::DecodePool, previous);
    UsmCompareDialog::setFailurePresenter({});
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testDefaultSuffix();
    testEmbeddedIcc();
    testBackgroundFailure();
    testRejectedSubmit();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "usm dialog tests passed\n";
    return 0;
}
