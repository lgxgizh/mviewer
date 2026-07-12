#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
