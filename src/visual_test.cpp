// Visual self-test: instantiate the real MainWindow, render offscreen, save PNG.
#include <QApplication>
#include <QTimer>
#include <QPixmap>
#include <QImage>
#include "mainwindow.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    w.resize(1600, 900);
    app.processEvents();
    QTimer::singleShot(1500, &app, [&]() {
        QPixmap pm(w.size());
        w.render(&pm);
        pm.save("D:/mviewer/selftest_main.png", "PNG");
        app.quit();
    });
    return app.exec();
}
