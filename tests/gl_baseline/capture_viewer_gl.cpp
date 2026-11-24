// Manual hardware check (NOT part of ctest): the ImageViewer's real GL tile path,
// rendered through Qt's blitter vs the opt-in fragment shader.
//
//     Xvfb :99 & DISPLAY=:99 QT_QPA_PLATFORM=xcb \
//         MVIEWER_GPU=1 MVIEWER_BLITTER=qt   ./capture_viewer_gl <outdir>
//         MVIEWER_GPU=1 MVIEWER_BLITTER=shader ./capture_viewer_gl <outdir>
//
// Diff the two directories with scripts/compare_gl_baseline.py --baseline.
#include "gpu/GpuTileUploader.h"
#include "imageviewer.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace
{
void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(outDir);
    printf("MVIEWER_GPU=%s MVIEWER_BLITTER=%s\n", qgetenv("MVIEWER_GPU").constData(),
           qgetenv("MVIEWER_BLITTER").constData());
    printf("gpuEnabled=%d\n", int(GpuTileUploader::enabled()));

    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("src.png"));
    {
        QImage src(1600, 1200, QImage::Format_RGB888);
        for (int y = 0; y < src.height(); ++y)
            for (int x = 0; x < src.width(); ++x)
                src.setPixel(x, y,
                             qRgb((x * 255) / (src.width() - 1), (y * 255) / (src.height() - 1),
                                  ((x + y) * 255) / (src.width() + src.height() - 2)));
        src.save(path);
    }

    ImageViewer viewer;
    viewer.resize(1280, 800);
    viewer.setImage(path);
    viewer.show();
    pump(1500);

    struct Shot
    {
        const char *name;
        bool overlay;
    };
    const Shot shots[] = {
        {"plain_0", false},
        {"plain_1", false},
        {"zebra_0", true},
    };

    for (const Shot &shot : shots)
    {
        if (shot.overlay)
        {
            viewer.setOverlayMode(mviewer::OverlayMode::Zebra);
            viewer.setZebraThreshold(40);
        }
        else
        {
            viewer.setOverlayMode(mviewer::OverlayMode::None);
        }
        pump(1200);
        const QImage frame = viewer.grabFramebuffer();
        const QString file = QStringLiteral("%1/%2.png").arg(outDir, shot.name);
        frame.save(file);
        printf("%s %dx%d -> %s\n", shot.name, frame.width(), frame.height(), qPrintable(file));
    }
    printf("done\n");
    return 0;
}
