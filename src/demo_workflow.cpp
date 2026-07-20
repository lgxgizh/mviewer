// M18 demo harness: runs the REAL MainWindow (actual UI code) on the visible
// desktop and scripted-performs the new workflow so a screen recorder can
// capture it: open directory -> select image (metadata + analysis populate) ->
// type a live filename search -> clear search. No mock widgets; this exercises
// the same code paths a user does. Build target `mviewer_demo`.
#include "mainwindow.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QTimer>

#include <cstdio>
#include <filesystem>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const std::filesystem::path assets =
        (argc > 1) ? std::filesystem::path(argv[1])
                   : (std::filesystem::current_path() / "demo_assets");

    MainWindow w;
    w.setupUi();
    w.resize(1280, 800);
    w.show();
    w.raise();
    w.activateWindow();

    // Locate UI pieces via the QObject tree (no private access needed).
    auto *thumb = w.findChild<ThumbnailPanel *>();
    auto *search = w.findChild<QLineEdit *>();

    const QString dir = QString::fromStdString(assets.string());
    const QFileInfoList files =
        QDir(dir).entryInfoList({"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tif", "*.tiff"},
                                QDir::Files, QDir::Name);

    // Step 1 (t=1.0s): open the demo directory.
    QTimer::singleShot(1000, &app,
                       [thumb, dir]()
                       {
                           if (thumb)
                               thumb->setDirectory(dir);
                       });

    // Step 2 (t=3.0s): select the first image -> metadata + preview + analysis.
    QTimer::singleShot(3000, &app,
                       [thumb, &w, files]()
                       {
                           if (files.isEmpty())
                               return;
                           const QString p = files.first().absoluteFilePath();
                           if (thumb)
                               thumb->scrollToPath(p);
                           w.onImageOpen(p);
                       });

    // Step 3 (t=6.0s): live-search "test" -> gallery filters.
    QTimer::singleShot(6000, &app,
                       [search]()
                       {
                           if (search)
                               search->setText("test");
                       });

    // Step 4 (t=8.5s): clear search.
    QTimer::singleShot(8500, &app,
                       [search]()
                       {
                           if (search)
                               search->setText("");
                       });

    // Keep the window alive long enough for the recorder (killed externally).
    printf("DEMO_READY\n");
    fflush(stdout);
    return app.exec();
}
