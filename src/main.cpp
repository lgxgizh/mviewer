#include "mainwindow.h"

#include "application/Startup.h"

#include <QApplication>

class MainWindow;
static QString g_openOnLaunch;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");

    // M14-1: Windows Native — open a file directly from the command line.
    // `mviewer.exe image.jpg` → open the image instead of an empty window.
    for (int i = 1; i < argc; ++i)
    {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (!arg.startsWith("-") && !arg.startsWith("/"))
        {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile())
            {
                g_openOnLaunch = fi.absoluteFilePath();
                break;
            }
        }
    }

    // Load plugins (if any)
    startupPlugins();

    MainWindow mainWindow;
    if (!g_openOnLaunch.isEmpty())
        mainWindow.setOpenOnLaunch(g_openOnLaunch);
    mainWindow.show();

    return app.exec();
}
