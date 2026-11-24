// Manual hardware baseline for the CompareWorkspace canvas.
//
// This is NOT part of ctest: it needs a real window system to paint into. It
// captures what the CURRENT CPU QPainter canvas produces so a future GPU
// compositor can be diffed against it pixel by pixel - the same "measure first"
// rule that governed the libjpeg-turbo fast path (ADR 036) and the tile shader
// (ADR 037).
//
// Build and run on a machine that can show a window (Xvfb is enough):
//
//     Xvfb :99 -screen 0 1600x1000x24 &
//     DISPLAY=:99 QT_QPA_PLATFORM=xcb ./capture_compare_canvas <outdir> [scaleFactor]
//
// The committed baselines live in tests/gl_baseline/dpr1 and .../dpr2, captured
// from MViewer v1.0.212 on Ubuntu 22.04 / Qt 6.8 / llvmpipe. Compare a fresh
// capture against them with scripts/compare_gl_baseline.py.
#include "compareworkspace.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <cstdio>

namespace
{
void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QImage makeSource(int w, int h, int seed, bool marker)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y,
                         qRgb((x * 255) / (w - 1), (y * 255) / (h - 1),
                              ((x + y + seed) * 255) / (w + h - 2)));
    if (marker)
    {
        // A hard-edged block in the top-left quadrant: alignment/offset deltas
        // only show up if something does not move together.
        QPainter p(&img);
        p.fillRect(w / 8, h / 8, w / 4, h / 4, QColor(240, 30, 30));
        p.end();
    }
    return img;
}

void pressKey(QWidget *target, int key)
{
    QApplication::sendEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QApplication::sendEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

bool waitFor(const std::function<bool()> &pred, int timeoutMs)
{
    const auto start = QDateTime::currentMSecsSinceEpoch();
    while (QDateTime::currentMSecsSinceEpoch() - start < timeoutMs)
    {
        if (pred())
            return true;
        pump(50);
    }
    return pred();
}

QString hashOf(const QImage &img)
{
    const QByteArray raw = QByteArray::fromRawData(
        reinterpret_cast<const char *>(img.constBits()), static_cast<int>(img.sizeInBytes()));
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex());
}
} // namespace

int main(int argc, char **argv)
{
    if (argc > 2)
        qputenv("QT_SCALE_FACTOR", QByteArray(argv[2]));
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(outDir);

    QTemporaryDir tmp;
    const QString pathA = tmp.filePath(QStringLiteral("a.png"));
    const QString pathB = tmp.filePath(QStringLiteral("b.png"));
    makeSource(1600, 1200, 0, false).save(pathA);
    makeSource(1600, 1200, 40, true).save(pathB);

    CompareWorkspace ws;
    ws.resize(1600, 900);
    ws.setImages({pathA, pathB});
    ws.show();
    pump(400);

    QWidget *canvas = ws.findChild<QWidget *>(QStringLiteral("compareCanvas"));
    printf("canvas=%p\n", static_cast<void *>(canvas));
    if (!canvas)
        return 2;
    printf("engine images=%d\n", ws.engine().imageCount());
    waitFor([&] { return ws.engine().imageCount() >= 2; }, 60000);
    pump(800);

    struct Shot
    {
        const char *modeKey;
        const char *name;
        int drags;
    };
    const Shot shots[] = {
        {"S", "split", 2},
        {"W", "swipe", 2},
        {"O", "overlay", 1},
        {"K", "checker", 1},
        {"H", "diff", 1},
    };

    for (const Shot &shot : shots)
    {
        pressKey(&ws, shot.modeKey[0]);
        pump(900);
        for (int d = 0; d < shot.drags; ++d)
        {
            if (shot.drags == 2 && canvas)
            {
                const QPoint centre = canvas->rect().center();
                const int targetX = canvas->width() * (d == 0 ? 35 : 65) / 100;
                QApplication::sendEvent(
                    canvas, new QMouseEvent(QEvent::MouseButtonPress, centre,
                                            QPoint(targetX, centre.y()), Qt::LeftButton,
                                            Qt::LeftButton, Qt::NoModifier));
                for (int step = 1; step <= 8; ++step)
                {
                    const int x = centre.x() + (targetX - centre.x()) * step / 8;
                    QApplication::sendEvent(canvas,
                                            new QMouseEvent(QEvent::MouseMove,
                                                            QPoint(x, centre.y()), Qt::NoButton,
                                                            Qt::LeftButton, Qt::NoModifier));
                    pump(40);
                }
                QApplication::sendEvent(
                    canvas, new QMouseEvent(QEvent::MouseButtonRelease,
                                            QPoint(targetX, centre.y()), Qt::LeftButton,
                                            Qt::NoButton, Qt::NoModifier));
                pump(600);
            }
            const QImage grab = canvas->grab().toImage();
            const QString file = QStringLiteral("%1/%2_%3.png").arg(outDir, shot.name).arg(d);
            grab.save(file);
            printf("%s[%d] %dx%d md5=%s -> %s\n", shot.name, d, grab.width(), grab.height(),
                   qPrintable(hashOf(grab)), qPrintable(file));
        }
        // Leave the mode off again so the next shot starts from a clean state.
        pressKey(&ws, shot.modeKey[0]);
        pump(300);
    }

    printf("done\n");
    return 0;
}
