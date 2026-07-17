#include "mainwindow.h"

#include "application/Startup.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");

    // Load plugins (if any)
    startupPlugins();

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
