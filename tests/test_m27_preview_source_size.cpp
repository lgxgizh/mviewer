// Cached preview source size. The preview cache stores scaled pixels only.
// ImageMetadata defaults to 0x0, and QSize(0,0).isValid() is true, so a cache
// hit used to skip the header and the known-size fallback and deliver
// sourceKnown with 0x0 after the worker — overwriting a synchronous present.
// Assertions run only after preview stats, which the worker posts after the
// visual delivery. Part of m27_lifetime_tests.

#include "previewpanel.h"
#include "thumbnailprovider.h"

#include "core/image/Decoder.h"
#include "core/image/ImageFileRotate.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

using Quality = PreviewPanel::PresentationQuality;

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
}

template <typename F> bool waitFor(F &&done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        pump(10);
    return done();
}

// Stats are queued after the visual delivery, so this is past the synchronous
// cache presentation (which does not fill sample statistics).
bool workerSettled(const PreviewPanel &panel, const QString &path)
{
    return panel.presentedPath() == path && panel.presentationQuality() == Quality::Preview &&
           panel.hasImage() && panel.previewStatsKnownForTest();
}

QString fixtureDir()
{
    const QString dir = QDir::tempPath() + "/mviewer_m27_preview_size_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    return dir;
}

QString savePng(const QString &dir, const QString &name, int w, int h, QRgb color)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    const QString path = dir + "/" + name;
    return image.save(path, "PNG") ? path : QString();
}

// Minimal EXIF orientation (tag 0x0112) injected after SOI. Stored raster is
// rawW x rawH; orientations 6 and 8 swap the displayed axes.
QString writeExifJpeg(const QString &path, int rawW, int rawH, int orientation)
{
    QImage image(rawW, rawH, QImage::Format_RGB32);
    image.fill(qRgb(40, 90, 160));
    const QString plain = path + ".plain.jpg";
    if (!image.save(plain, "JPEG", 85))
        return {};
    QFile sink(plain);
    if (!sink.open(QIODevice::ReadOnly))
        return {};
    QByteArray jpeg = sink.readAll();
    sink.close();
    QFile::remove(plain);
    if (jpeg.size() < 4)
        return {};
    const char tiff[] = {'I',
                         'I',
                         42,
                         0,
                         8,
                         0,
                         0,
                         0,
                         1,
                         0,
                         0x12,
                         0x01,
                         3,
                         0,
                         1,
                         0,
                         0,
                         0,
                         static_cast<char>(orientation),
                         0,
                         0,
                         0,
                         0,
                         0,
                         0,
                         0};
    QByteArray payload("Exif\0\0", 6);
    payload.append(tiff, static_cast<int>(sizeof(tiff)));
    QByteArray app1;
    app1.append(static_cast<char>(0xFF));
    app1.append(static_cast<char>(0xE1));
    const int len = static_cast<int>(payload.size()) + 2;
    app1.append(static_cast<char>((len >> 8) & 0xFF));
    app1.append(static_cast<char>(len & 0xFF));
    app1.append(payload);
    jpeg.insert(2, app1);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(jpeg) != jpeg.size())
        return {};
    return path;
}

QSize orientedHeader(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize stored = reader.size();
    if (stored.width() > 0 && stored.height() < 0)
        stored.setHeight(-stored.height());
    const auto transform = reader.transformation();
    const bool swap = transform == QImageIOHandler::TransformationRotate90 ||
                      transform == QImageIOHandler::TransformationRotate270 ||
                      transform == QImageIOHandler::TransformationMirrorAndRotate90 ||
                      transform == QImageIOHandler::TransformationFlipAndRotate90;
    if (swap && stored.width() > 0 && stored.height() > 0)
        stored = stored.transposed();
    return stored;
}

void expectSource(const PreviewPanel &panel, const QSize &source, const char *label)
{
    const QSize got = panel.sourceImageSize();
    const QSize preview = panel.previewPixelSize();
    const int previewLong = std::max(preview.width(), preview.height());
    const int sourceLong = std::max(got.width(), got.height());
    const bool positive = got.width() > 0 && got.height() > 0;
    const bool capped =
        preview.width() > 0 && preview.height() > 0 && previewLong <= PreviewPanel::kPreviewMaxEdge;
    const bool distinct = got != preview && sourceLong > PreviewPanel::kPreviewMaxEdge;
    if (!(got == source && positive && capped && distinct))
    {
        printf("  detail %s: source %dx%d preview %dx%d expected %dx%d\n", label, got.width(),
               got.height(), preview.width(), preview.height(), source.width(), source.height());
    }
    CHECK(got == source && positive, label);
    CHECK(capped && distinct, label);
}

void show(PreviewPanel &panel, const QString &path, const QSize &known)
{
    panel.resize(320, 240);
    if (known.isValid() && known.width() > 0 && known.height() > 0)
        panel.setImage(path, QPixmap(), known);
    else
        panel.setImage(path);
}

void testPngHotColdAndAba(const QString &dir)
{
    printf("\n[preview source size: png hot/cold, known/unknown, A-B-A]\n");
    fflush(stdout);
    const QSize aSize(640, 400);
    const QSize bSize(480, 720);
    const QString a = savePng(dir, "a.png", aSize.width(), aSize.height(), qRgb(180, 40, 40));
    const QString b = savePng(dir, "b.png", bSize.width(), bSize.height(), qRgb(40, 140, 80));
    CHECK(!a.isEmpty() && !b.isEmpty(), "PASS png fixtures written");

    PreviewPanel cold;
    show(cold, a, QSize());
    CHECK(waitFor([&] { return workerSettled(cold, a); }, 20000), "PASS cold worker settled");
    expectSource(cold, aSize, "PASS cold unknown source is the file, not 0 or the preview");

    // Fresh panel: no local knownSources. The process preview cache is hot.
    PreviewPanel cross;
    show(cross, a, QSize());
    CHECK(waitFor([&] { return workerSettled(cross, a); }, 20000),
          "PASS cross-panel hot worker settled");
    expectSource(cross, aSize, "PASS cross-panel hot cache keeps the real source size");

    PreviewPanel known;
    show(known, a, aSize);
    CHECK(waitFor([&] { return workerSettled(known, a); }, 20000),
          "PASS known-size hot worker settled");
    expectSource(known, aSize, "PASS known source size survives the cache-hit worker");

    PreviewPanel aba;
    show(aba, a, QSize());
    CHECK(waitFor([&] { return workerSettled(aba, a); }, 20000), "PASS A-B-A first A settled");
    expectSource(aba, aSize, "PASS A-B-A first A source");
    show(aba, b, QSize());
    CHECK(waitFor([&] { return workerSettled(aba, b); }, 20000), "PASS A-B-A B settled");
    expectSource(aba, bSize, "PASS A-B-A B source");
    show(aba, a, QSize());
    CHECK(!aba.previewStatsKnownForTest(), "PASS return to A has not settled before the worker");
    CHECK(waitFor([&] { return workerSettled(aba, a); }, 20000), "PASS A-B-A return settled");
    expectSource(aba, aSize, "PASS A-B-A source after the worker is still A's original");
}

void testOriented(const QString &path, const QSize &displayed, const char *label)
{
    printf("\n[preview source size: %s]\n", label);
    fflush(stdout);
    const QSize header = orientedHeader(path);
    if (header != displayed)
    {
        printf("  detail %s: header %dx%d expected %dx%d\n", label, header.width(), header.height(),
               displayed.width(), displayed.height());
    }
    CHECK(header == displayed, label);
    PreviewPanel cold;
    show(cold, path, QSize());
    CHECK(waitFor([&] { return workerSettled(cold, path); }, 20000), label);
    expectSource(cold, displayed, label);

    PreviewPanel hot;
    show(hot, path, QSize());
    CHECK(waitFor([&] { return workerSettled(hot, path); }, 20000), label);
    expectSource(hot, displayed, label);

    PreviewPanel known;
    show(known, path, displayed);
    CHECK(waitFor([&] { return workerSettled(known, path); }, 20000), label);
    expectSource(known, displayed, label);
}

void testExifOrientation(const QString &dir)
{
    const QString rot90 = writeExifJpeg(dir + "/rot90.jpg", 720, 300, 6);
    const QString rot270 = writeExifJpeg(dir + "/rot270.jpg", 800, 280, 8);
    CHECK(!rot90.isEmpty() && !rot270.isEmpty(), "PASS exif jpegs written");
    testOriented(rot90, QSize(300, 720), "PASS EXIF 6 cold and hot agree on 300x720");
    testOriented(rot270, QSize(280, 800), "PASS EXIF 8 cold and hot agree on 280x800");
}

// Ctrl+R on a tagged JPEG rewrites only the EXIF orientation tag: the stored
// raster keeps its axes and the displayed geometry swaps. The panel must report
// the displayed size after the rewrite, not the size it cached before it.
void testRotateRefreshesSourceSize(const QString &dir)
{
    printf("\n[preview source size: rotation swaps the reported source size]\n");
    fflush(stdout);
    const QString path = writeExifJpeg(dir + "/rotate_me.jpg", 640, 400, 1);
    CHECK(!path.isEmpty(), "PASS rotate fixture written");

    PreviewPanel panel;
    show(panel, path, QSize());
    CHECK(waitFor([&] { return workerSettled(panel, path); }, 20000),
          "PASS before rotation settled");
    expectSource(panel, QSize(640, 400), "PASS reports 640x400 before rotation");

    const auto result = mviewer::core::rotateImageFile(path.toUtf8().toStdString(), 90);
    CHECK(result.ok, "PASS rotateImageFile succeeds");
    ThumbnailProvider::invalidateSource(path.toUtf8().toStdString());
    panel.setImage(path);
    CHECK(waitFor([&] { return panel.sourceImageSize() == QSize(400, 640); }, 20000),
          "PASS the panel reports the swapped size after rotation");
    CHECK(workerSettled(panel, path), "PASS re-settles after rotation");
    expectSource(panel, QSize(400, 640), "PASS reports the swapped 400x640 after rotation");
}

} // namespace

void runM27PreviewSourceSizeTests(int &pass, int &fail)
{
    printf("\n[preview source size: cached preview keeps post-orientation dimensions]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.resume(TaskScheduler::PoolType::DecodePool);
    const QString dir = fixtureDir();
    testPngHotColdAndAba(dir);
    testExifOrientation(dir);
    testRotateRefreshesSourceSize(dir);
    sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15));
    pump(50);
    QDir(dir).removeRecursively();
    pass += g_pass;
    fail += g_fail;
}
