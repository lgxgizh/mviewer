// M18 demo render: renders the REAL MainWindow (actual UI code) via Qt's
// offscreen platform and grabs three workflow states to PNGs, which
// scripts/record_demo.ps1 stitches into an animated GIF with ffmpeg. This is a
// genuine render of the real UI (no mock widgets), captured headlessly because
// the build/terminal session cannot reach the interactive display session.
// Build target `mviewer_demo_render`.
#include "mainwindow.h"
#include "metadatapanel.h"
#include "previewpanel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QPixmap>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace
{
void pump(int ms)
{
    QTimer::singleShot(ms, qApp, &QApplication::quit);
    qApp->exec();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_UseSoftwareOpenGL, true);
    printf("RENDER_START\n");
    fflush(stdout);

    const std::filesystem::path assets = (argc > 1)
                                             ? std::filesystem::path(argv[1])
                                             : (std::filesystem::current_path() / "demo_assets");
    const std::filesystem::path outdir =
        (argc > 2) ? std::filesystem::path(argv[2]) : std::filesystem::current_path();

    MainWindow w;
    w.setupUi();
    w.resize(1280, 800);

    auto *thumb = w.findChild<ThumbnailPanel *>();
    auto *search = w.findChild<QLineEdit *>();
    const QString dir = QString::fromStdString(assets.string());
    const QFileInfoList files = QDir(dir).entryInfoList(
        {"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tif", "*.tiff"}, QDir::Files, QDir::Name);

    auto grab = [&](const std::string &name)
    {
        const QPixmap pm = w.grab();
        const std::string path = (outdir / name).string();
        const bool ok = pm.save(QString::fromStdString(path));
        printf("GRAB_%s %s\n", name.c_str(), ok ? "OK" : "FAIL");
        fflush(stdout);
    };

    // State 1: directory opened (gallery populates). Quiesce the async
    // thumbnail worker so no background QPixmap updates race the offscreen
    // render (thumbnails show placeholder icons — fine for the demo).
    if (thumb)
    {
        thumb->setDirectory(dir);
        thumb->stopThumbnailWorker();
    }
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    grab("demo_state1_dir.png");

    // State 2: an image selected -> metadata populates (sync, no async viewer).
    if (!files.isEmpty())
    {
        const QString p = files.first().absoluteFilePath();
        if (thumb)
            thumb->scrollToPath(p);
        if (auto *mp = w.findChild<MetadataPanel *>())
            mp->setImage(p);
    }
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    grab("demo_state2_metadata.png");

    // State 3: live filename search "test" -> gallery filters.
    if (search)
        search->setText("test");
    QCoreApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    grab("demo_state3_search.png");

    printf("DEMO_RENDER_DONE\n");
    fflush(stdout);
    return 0;
}
