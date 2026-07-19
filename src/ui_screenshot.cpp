// M11.3 release artifact: render the REAL MainWindow (actual UI code, not a
// mock) to a PNG so the release ships a genuine screenshot. Headless-safe via
// QT_QPA_PLATFORM=offscreen + QWidget::grab().
//
// Build: CMake target `ui_screenshot` (links mviewer_ui + mviewer_core).
// Usage: ui_screenshot <out.png> [sample.png]
#include "mainwindow.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QFont>

#include <cstdio>
#include <filesystem>
#include <string>

static std::string makeSample(const std::filesystem::path &p)
{
    QImage img(960, 600, QImage::Format_RGB32);
    QPainter pa(&img);
    for (int y = 0; y < 600; ++y)
    {
        int g = 190 - y / 5;
        pa.fillRect(0, y, 960, 1, QColor(120, static_cast<uchar>(g), 210));
    }
    pa.setBrush(QColor(255, 230, 120));
    pa.setPen(Qt::NoPen);
    pa.drawEllipse(740, 110, 90, 90);
    pa.setBrush(QColor(60, 150, 75));
    pa.drawEllipse(-120, 470, 620, 320);
    pa.drawEllipse(520, 500, 720, 340);
    pa.setPen(Qt::white);
    pa.setFont(QFont("Sans", 30, QFont::Bold));
    pa.drawText(40, 90, "MViewer 1.0 — sample image view");
    pa.end();
    img.save(QString::fromStdString(p.string()));
    return p.string();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const std::string out =
        (argc > 1) ? argv[1] : (std::filesystem::temp_directory_path() / "mviewer_screenshot.png").string();
    const std::filesystem::path tp =
        std::filesystem::temp_directory_path() / "mviewer_sample.png";
    const std::string sample = makeSample(tp);

    MainWindow w;
    w.setupUi();
    w.resize(1040, 700);
    w.onImageOpen(QString::fromStdString(sample));

    // Let the async repository load + UI settle, then grab the real widget.
    QTimer::singleShot(1000, &app, [&]() {
        const QPixmap pm = w.grab();
        const bool ok = pm.save(QString::fromStdString(out));
        if (ok)
            printf("SCREENSHOT_OK: %s (%dx%d)\n", out.c_str(), pm.width(), pm.height());
        else
            printf("SCREENSHOT_FAIL: could not save %s\n", out.c_str());
        app.quit();
    });

    return app.exec();
}
