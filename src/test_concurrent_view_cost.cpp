// Mechanism measurements, not a hardware FPS gate. No scheduler width changes.
#include "check.h"
#include "compareworkspace.h"
#include "core/image/MetadataReader.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "imageviewer.h"
#include "imageviewer_queuedecode.h"
#include "previewpanel.h"
#include "thumbnailprovider.h"
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QPaintEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>

struct ImageViewerPromotedRasterTestAccess
{
    static qint64 draw(ImageViewer &v, bool transition)
    {
        QImage canvas(QSize(qRound(v.width() * v.devicePixelRatioF()),
                            qRound(v.height() * v.devicePixelRatioF())),
                      QImage::Format_ARGB32_Premultiplied);
        canvas.setDevicePixelRatio(v.devicePixelRatioF());
        canvas.fill(Qt::black);
        if (transition)
            v.prepareTransitionImage(QStringLiteral("next"), false);
        QPainter painter(&canvas);
        QElapsedTimer t;
        t.start();
        for (int n = 0; n < 5; ++n)
        {
            if (transition)
                v.drawTransition(painter);
            else
                v.drawDisplayRaster(painter);
        }
        return t.nsecsElapsed() / 5;
    }
};

struct CompareSourceDisplayTestAccess
{
    static bool loaded(const CompareWorkspace &w)
    {
        return w.m_engine.imageCount() == 2;
    }
    static QWidget *canvas(CompareWorkspace &w)
    {
        return w.m_compareCanvas;
    }
    static void swipe(CompareWorkspace &w)
    {
        w.m_swipeChk->setChecked(true);
    }
    static quint64 renders(const CompareWorkspace &w)
    {
        return w.m_canvasBaseRenderCount;
    }
    static void invalidate(CompareWorkspace &w)
    {
        w.m_canvasBaseKey.clear();
    }
};

class PaintTiming final : public ImageViewer
{
  public:
    qint64 totalNs = 0;
    qint64 maxNs = 0;
    int contentPaints = 0;
    std::function<void()> onContent;

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        const bool content = isLodDisplay();
        QElapsedTimer t;
        t.start();
        ImageViewer::paintEvent(event);
        const qint64 ns = t.nsecsElapsed();
        if (!content)
            return;
        ++contentPaints;
        totalNs += ns;
        maxNs = std::max(maxNs, ns);
        if (onContent)
            onContent();
    }
};

QString fixture(const QTemporaryDir &tmp, const QString &name, const char *format)
{
    QImage image(4096, 3072, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
    {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            row[x] = qRgb(x % 256, y % 256, (x + y) % 256);
    }
    const QString path = tmp.filePath(name);
    MVIEWER_CHECK(image.save(path, format), "generated 12.6MP image");
    return path;
}

void sample(const char *label, const QueueDecodeSample &s)
{
    std::printf("%s queuePlusYieldMs=%.3f yieldMs=%.3f poolQueueAndEntryMs=%.3f decodeMs=%.3f\n",
                label, s.queueWaitNs / 1e6, s.foregroundYieldNs / 1e6,
                std::max<std::int64_t>(0, s.queueWaitNs - s.foregroundYieldNs) / 1e6,
                s.decodeNs / 1e6);
}

void concurrent(const QString &path)
{
    resetQueueDecodeSamples();
    PaintTiming viewer;
    PreviewPanel preview;
    viewer.setFixedSize(1920, 1080);
    preview.resize(400, 500);
    viewer.show();
    preview.show();
    QApplication::processEvents();
    MVIEWER_CHECK(viewer.devicePixelRatioF() >= 1.99, "DPR2 measured, no silent DPR1 fallback");
    QElapsedTimer first;
    first.start();
    qint64 firstPaintNs = -1;
    viewer.onContent = [&]
    {
        if (firstPaintNs < 0)
            firstPaintNs = first.nsecsElapsed();
    };
    preview.setImage(path);
    bool activeAtSubmit = false;
    // Start current LOD and visible thumbnail demand while REAL preview pixels
    // are decoding, not while an artificial foreground sleep occupies a pool.
    QElapsedTimer startWait;
    startWait.start();
    // Test-only rendezvous without exposing/painting windows between observing
    // the worker's start and submitting current display demand.
    while (startWait.elapsed() < 3000)
    {
        const auto p = latestQueueDecodeSample(QueueDecodeRole::Preview);
        if (p && !p->finished)
        {
            activeAtSubmit = true;
            viewer.setBrowseSequence({path});
            viewer.setImage(path);
            break;
        }
        if (p && p->finished)
            break;
        std::this_thread::yield();
    }
    const bool sawPreview = activeAtSubmit;
    MVIEWER_CHECK(sawPreview && activeAtSubmit, "real preview decode active when LOD submitted");
    auto &pipe = ThumbnailPipeline::instance();
    constexpr int consumer = 8181;
    auto thumbNs = std::make_shared<std::atomic<qint64>>(0);
    auto thumbCount = std::make_shared<std::atomic<int>>(0);
    pipe.setDecodeFn(
        [thumbNs, thumbCount](const std::string &p, int edge)
        {
            QElapsedTimer t;
            t.start();
            auto image = ThumbnailProvider::produce(p, edge);
            thumbNs->fetch_add(t.nsecsElapsed());
            thumbCount->fetch_add(1);
            return image;
        });
    std::vector<std::string> thumbPaths{path.toUtf8().toStdString()};
    for (int n = 1; n < 4; ++n)
    {
        const QString copy = path + QString::number(n) + "." + QFileInfo(path).suffix();
        MVIEWER_CHECK(QFile::copy(path, copy), "distinct cold thumbnail fixture");
        thumbPaths.push_back(copy.toUtf8().toStdString());
    }
    pipe.setSourcesForConsumer(consumer, thumbPaths);
    pipe.setVisibleRangeForConsumer(consumer, 0, thumbPaths.size());
    const bool ready = mviewer::test::waitFor(
        [&]
        {
            return viewer.isLodDisplay() &&
                   preview.presentationQuality() == PreviewPanel::PresentationQuality::Preview;
        },
        30000);
    MVIEWER_CHECK(ready, "simultaneous preview + current LOD converge");
    viewer.update();
    mviewer::test::waitFor([&] { return firstPaintNs >= 0; }, 3000);
    const auto p = latestQueueDecodeSample(QueueDecodeRole::Preview);
    const auto c = latestQueueDecodeSample(QueueDecodeRole::CurrentDisplay);
    MVIEWER_CHECK(p && c && p->finished && c->finished, "both real decode timings captured");
    if (p)
        sample("preview", *p);
    if (c)
        sample("currentLOD", *c);
    std::printf(
        "concurrent %s firstPaintMs=%.3f contentPaints=%d maxPaintMs=%.3f GL=%d raster=%dx%d\n",
        qPrintable(path.section('/', -1)), firstPaintNs / 1e6, viewer.contentPaints,
        viewer.maxNs / 1e6, viewer.isValid() ? 1 : 0, viewer.displayRaster().width(),
        viewer.displayRaster().height());
    if (ready)
    {
        const auto settled = ImageViewerPromotedRasterTestAccess::draw(viewer, false);
        const auto transition = ImageViewerPromotedRasterTestAccess::draw(viewer, true);
        std::printf("raster-backend actual draw methods settledMs=%.3f transitionMs=%.3f\n",
                    settled / 1e6, transition / 1e6);
    }
    // Rebuild includes synchronous smooth scaling; preserve quality/DPR.
    QElapsedTimer resize;
    resize.start();
    for (int n = 0; n < 30; ++n)
        preview.resize(380 + n, 500);
    std::printf("preview DPR2 resize30 totalMs=%.3f\n", resize.nsecsElapsed() / 1e6);
    MVIEWER_CHECK(preview.scaledPixmapForTest().devicePixelRatio() >= 1.99,
                  "preview remains sharp device-resolution output");
    MVIEWER_CHECK(mviewer::test::waitFor([&] { return thumbCount->load() == 4; }, 30000),
                  "all four real visible thumbnail decodes finish");
    std::printf("visibleThumbnailCount=%d sumWorkerMs=%.3f (cache/decode/store)\n",
                thumbCount->load(), thumbNs->load() / 1e6);
    pipe.removeConsumer(consumer);
    pipe.restoreDefaultDecodeFn();
}

void metadataCost(const QString &path)
{
    QElapsedTimer t;
    t.start();
    const auto headers = mviewer::core::MetadataReader::readHeaders(path.toUtf8().toStdString());
    const auto headerNs = t.nsecsElapsed();
    t.restart();
    const auto full = mviewer::core::MetadataReader::read(path.toUtf8().toStdString());
    const auto fullNs = t.nsecsElapsed();
    QImageReader reader(path);
    std::printf("metadata %s headerMs=%.3f fullMs=%.3f pluginScaledSize=%d channels=%d color=%s\n",
                qPrintable(path.section('/', -1)), headerNs / 1e6, fullNs / 1e6,
                reader.supportsOption(QImageIOHandler::ScaledSize) ? 1 : 0, full.channels,
                full.colorSpace.c_str());
    MVIEWER_CHECK(headers.width == full.width && full.width == 4096,
                  "headers and display metadata preserve dimensions");
}

void compareCost(const QString &a, const QString &b)
{
    CompareWorkspace w;
    w.resize(1920, 1080);
    w.show();
    w.setImages({a, b});
    MVIEWER_CHECK(
        mviewer::test::waitFor([&] { return CompareSourceDisplayTestAccess::loaded(w); }, 30000),
        "Compare two frames loaded asynchronously");
    CompareSourceDisplayTestAccess::swipe(w);
    QApplication::processEvents();
    auto *canvas = CompareSourceDisplayTestAccess::canvas(w);
    MVIEWER_CHECK(canvas != nullptr, "Compare canvas exists");
    if (!canvas)
        return;
    QPixmap output(canvas->size() * canvas->devicePixelRatioF());
    output.setDevicePixelRatio(canvas->devicePixelRatioF());
    CompareSourceDisplayTestAccess::invalidate(w);
    QElapsedTimer timer;
    timer.start();
    canvas->render(&output);
    const auto rebuildNs = timer.nsecsElapsed();
    const auto builds = CompareSourceDisplayTestAccess::renders(w);
    timer.restart();
    for (int n = 0; n < 5; ++n)
        canvas->render(&output);
    std::printf("Compare DPR=%.2f canvas=%dx%d rebuildMs=%.3f cachedAvgMs=%.3f\n",
                canvas->devicePixelRatioF(), canvas->width(), canvas->height(), rebuildNs / 1e6,
                timer.nsecsElapsed() / 5e6);
    MVIEWER_CHECK(builds == CompareSourceDisplayTestAccess::renders(w),
                  "settled Compare paints reuse base surface without allocation/composition");
}

int main(int argc, char **argv)
{
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    MVIEWER_CHECK(tmp.isValid(), "temporary directory");
    const auto jpeg = fixture(tmp, "concurrent.jpg", "JPEG");
    const auto png = fixture(tmp, "concurrent.png", "PNG");
    concurrent(jpeg);
    concurrent(png);
    metadataCost(jpeg);
    metadataCost(png);
    compareCost(jpeg, png);
    std::fflush(stdout);
    return mviewer::test::failures() ? 1 : 0;
}
