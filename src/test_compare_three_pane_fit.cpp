// Three-pane compare: every pane gets an equal column and the same
// fit-to-window scale. Regression: a long filename without break points
// (camera-style "DSC_..._v3.jpg") made its pane header's minimum width the
// column's minimum, so the short-named left pane was squeezed and its image
// rendered much smaller than the other two.
//
// Two-pane regression (TC-CMP-FIT-ORIENT): a portrait PNG with a short name
// and a landscape-stored JPEG with EXIF orientation 6 and a long name both
// DISPLAY as 600x800. The long name used to wrap to a two-line pane header,
// which shortened that pane's viewport so its image fit visibly smaller.
// Headers are now one line in every pane; both panes must get the same
// viewport and the same fit scale, and Browse dimension probes must report
// the displayed (oriented) size.

#include "compareworkspace.h"
#include "core/image/FrameSequence.h"
#include "core/image/MetadataReader.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
int g_failures = 0;

void check(bool cond, const char *message)
{
    std::printf(cond ? "[ok] %s\n" : "FAIL: %s\n", message);
    if (!cond)
        ++g_failures;
}

void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (timer.elapsed() < ms);
}

QString writePng(const QDir &dir, const QString &name, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage image(1200, 800, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

RawImageView *paneView(QWidget *root, int pane)
{
    QWidget *cell = root->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(pane));
    return cell ? cell->findChild<RawImageView *>() : nullptr;
}

bool waitForPanes(CompareWorkspace *ws, int count, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        bool ready = ws->comparedImageCount() == count;
        for (int i = 0; ready && i < count; ++i)
        {
            const RawImageView *view = paneView(ws, i);
            ready = view && view->width() > 32 && view->height() > 32 &&
                    !view->displayImage().isNull() && view->sourceSize().width() > 0;
        }
        if (ready)
            return true;
        pump(25);
    }
    return false;
}

double fitFor(const RawImageView *view)
{
    return std::min(view->width() / 1200.0, view->height() / 800.0);
}

void checkThreePanes(CompareWorkspace *ws, const char *label)
{
    std::printf("-- %s\n", label);
    RawImageView *views[3] = {paneView(ws, 0), paneView(ws, 1), paneView(ws, 2)};
    if (!views[0] || !views[1] || !views[2])
    {
        check(false, "all three pane views exist");
        return;
    }
    const int minW = std::min({views[0]->width(), views[1]->width(), views[2]->width()});
    const int maxW = std::max({views[0]->width(), views[1]->width(), views[2]->width()});
    std::printf("   pane widths %d %d %d\n", views[0]->width(), views[1]->width(),
                views[2]->width());
    check(maxW - minW <= 4, "three panes share the grid width equally");
    double scales[3] = {};
    for (int i = 0; i < 3; ++i)
        scales[i] = ws->engine().cellScale(i);
    std::printf("   cell scales %.4f %.4f %.4f\n", scales[0], scales[1], scales[2]);
    const double hi = std::max({scales[0], scales[1], scales[2]});
    check(hi > 0.0 && scales[0] / hi > 0.97, "left pane is not smaller than the others");
    check(std::abs(scales[1] - scales[2]) / hi < 0.03, "right panes share one fit scale");
    for (int i = 0; i < 3; ++i)
    {
        const double expect = fitFor(views[i]);
        check(expect > 0.0 && std::abs(scales[i] - expect) / expect < 0.03,
              "pane scale is fit-to-window for its own view");
    }
}
// JPEG stored as rawW x rawH with an EXIF orientation tag inserted after SOI
// (minimal little-endian TIFF IFD with one 0x0112 SHORT entry).
QString writeExifJpeg(const QDir &dir, const QString &name, int rawW, int rawH, int orientation)
{
    QImage image(rawW, rawH, QImage::Format_RGB32);
    image.fill(QColor(90, 120, 200));
    QByteArray jpeg;
    {
        QFile sink(dir.filePath(QStringLiteral("tmp_plain.jpg")));
        image.save(sink.fileName(), "JPEG", 90);
        if (sink.open(QIODevice::ReadOnly))
            jpeg = sink.readAll();
    }
    if (jpeg.size() < 4)
        return QString();
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
    payload.append(tiff, sizeof(tiff));
    QByteArray app1;
    app1.append(static_cast<char>(0xFF));
    app1.append(static_cast<char>(0xE1));
    const int len = static_cast<int>(payload.size()) + 2;
    app1.append(static_cast<char>((len >> 8) & 0xFF));
    app1.append(static_cast<char>(len & 0xFF));
    app1.append(payload);
    jpeg.insert(2, app1);
    const QString path = dir.filePath(name);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(jpeg) != jpeg.size())
        return QString();
    return path;
}

QString writePortraitPng(const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    QImage image(600, 800, QImage::Format_RGB32);
    image.fill(QColor(200, 120, 60));
    image.save(path, "PNG");
    return path;
}

int headerHeight(QWidget *root, int pane)
{
    QWidget *cell = root->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(pane));
    QWidget *header =
        cell ? cell->findChild<QWidget *>(QStringLiteral("comparePaneHeader")) : nullptr;
    return header && header->isVisible() ? header->height() : -1;
}

void checkTwoPanesMatch(CompareWorkspace *ws, int hostWidth)
{
    RawImageView *a = paneView(ws, 0);
    RawImageView *b = paneView(ws, 1);
    if (!a || !b)
    {
        check(false, "both pane views exist");
        return;
    }
    const double sa = ws->engine().cellScale(0);
    const double sb = ws->engine().cellScale(1);
    std::printf("   width %d: headers %d/%d views %dx%d / %dx%d src %dx%d / %dx%d scales %.4f "
                "%.4f\n",
                hostWidth, headerHeight(ws, 0), headerHeight(ws, 1), a->width(), a->height(),
                b->width(), b->height(), a->sourceSize().width(), a->sourceSize().height(),
                b->sourceSize().width(), b->sourceSize().height(), sa, sb);
    check(headerHeight(ws, 0) > 0 && headerHeight(ws, 0) == headerHeight(ws, 1),
          "long and short names get the same one-line header height");
    check(a->height() == b->height() && std::abs(a->width() - b->width()) <= 2,
          "both panes get the same viewport");
    check(a->sourceSize() == QSize(600, 800) && b->sourceSize() == QSize(600, 800),
          "both panes fit the displayed (EXIF-oriented) 600x800 geometry");
    const double hi = std::max(sa, sb);
    check(hi > 0.0 && std::abs(sa - sb) / hi < 0.01,
          "same displayed size fits at the same scale in both panes");
}

void runOrientedPairCase(const QDir &dir)
{
    std::printf("-- two panes: short portrait PNG vs long-named EXIF-6 JPEG\n");
    const QString portrait = writePortraitPng(dir, QStringLiteral("p.png"));
    const QString rotated = writeExifJpeg(
        dir,
        QStringLiteral("IMG_20261025_portrait_rotated_right_exif6_long_name_that_used_to_wrap_"
                       "onto_a_second_header_line_v2.jpg"),
        800, 600, 6);
    check(!portrait.isEmpty() && !rotated.isEmpty(), "oriented fixtures written");

    const auto meta = mviewer::core::MetadataReader::read(rotated.toStdString());
    check(meta.width == 600 && meta.height == 800 && meta.orientation == 6,
          "metadata reports displayed 600x800 for the EXIF-6 file");
    const auto frame = mviewer::core::FrameSequenceReader::frameInfo(rotated.toStdString(), 0);
    std::printf("   browse probe %dx%d\n", frame.width, frame.height);
    check(frame.width == 600 && frame.height == 800,
          "browse Resolution probe reports displayed 600x800, not stored 800x600");

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    host.resize(1400, 820);
    host.show();
    pump(50);
    ws->setImages({portrait, rotated});
    check(waitForPanes(ws, 2, 20000), "two oriented panes loaded");
    pump(300);
    // Sweep widths so the long name crosses the old 1x..2x wrap window.
    for (const int width : {1400, 1150, 950, 800, 650})
    {
        host.resize(width, 820);
        pump(300);
        checkTwoPanesMatch(ws, width);
    }
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("FAIL: temp dir\n");
        return 1;
    }
    const QDir dir(tmp.path());
    const QString longStem =
        QStringLiteral("DSC_2026_10_13_wedding_ceremony_second_shooter_raw_export_full_resolution");
    const QStringList images = {
        writePng(dir, QStringLiteral("a1.png"), QColor(200, 60, 60)),
        writePng(dir, longStem + QStringLiteral("_v3.png"), QColor(60, 160, 60)),
        writePng(dir, longStem + QStringLiteral("_v4.png"), QColor(60, 60, 200)),
    };

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    host.resize(1400, 820);
    host.show();
    pump(50);
    ws->setImages(images);
    check(waitForPanes(ws, 3, 20000), "three panes loaded and laid out");
    pump(300);
    checkThreePanes(ws, "long names on the right panes");

    // The window narrows (e.g. restored from maximized): columns stay equal
    // and every pane re-fits instead of the left one collapsing.
    host.resize(1000, 700);
    // resizeEvent schedules fit via schedulePostLayoutFit(); pump so the
    // coalesced refit runs before we assert cell scales (Linux offscreen ASan
    // otherwise still sees the pre-resize fit values).
    pump(300);
    checkThreePanes(ws, "after narrowing the window");

    // Regression test: snapshot in comparison mode must cover the full canvas area on High-DPI
    // screens without shrinking to 1/DPR or leaving blank dead space when zoomed in.
    const QString snapPath = dir.filePath("test_snap_coverage.png");
    check(ws->saveComparisonViewTo(snapPath), "snapshot saves to file successfully");
    const QImage snapImg(snapPath);
    check(!snapImg.isNull(), "snapshot image is valid");
    const qreal dpr = ws->devicePixelRatioF() > 0.0 ? ws->devicePixelRatioF() : 1.0;
    const int bannerH = static_cast<int>(std::round(36 * dpr));
    check(snapImg.height() > bannerH, "snapshot includes comparison area and banner");

    auto checkCoverage = [&](const QImage &img, const char *label)
    {
        const QRgb bg = qRgb(24, 24, 28);
        int maxX = 0;
        for (int y = 0; y < img.height() - bannerH; y += 8)
        {
            for (int x = img.width() - 1; x > maxX; --x)
            {
                if (img.pixel(x, y) != bg && qAlpha(img.pixel(x, y)) > 0)
                {
                    maxX = x;
                    break;
                }
            }
        }
        check(maxX >= static_cast<int>(img.width() * 0.9), label);
    };

    checkCoverage(snapImg, "snapshot covers full canvas width in fit mode");

    // Zoom in (actual pixels / 100%) and verify that snapshot still covers full canvas width
    ws->zoomActual();
    pump(300);
    const QString snapZoomPath = dir.filePath("test_snap_zoomed.png");
    check(ws->saveComparisonViewTo(snapZoomPath), "zoomed snapshot saves successfully");
    const QImage snapZoomImg(snapZoomPath);
    checkCoverage(snapZoomImg, "snapshot covers full canvas width when zoomed in");

    runOrientedPairCase(dir);

    if (g_failures > 0)
    {
        std::printf("compare_three_pane_fit_tests: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("compare_three_pane_fit_tests: PASS\n");
    return 0;
}
