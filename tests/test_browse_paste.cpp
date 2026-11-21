// Browse paste-into-folder and open-current-folder acceptance.
#include "mainwindow.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cout << "[FAIL] " << (msg) << "\n";                                               \
            ++g_failures;                                                                          \
        }                                                                                          \
        else                                                                                       \
            std::cout << "[ok] " << (msg) << "\n";                                                 \
    } while (0)

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QString writePng(const QDir &dir, const QString &name, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage img(16, 16, QImage::Format_RGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

void testPasteIntoFolderAndOpenAction(const QString &dirPath)
{
    std::cout << "── Browse: paste into folder + open-folder action ──\n";
    QDir dir(dirPath);
    writePng(dir, "paste_seed.png", QColor(10, 20, 30));

    SelectionModel sel;
    ThumbnailPanel panel;
    panel.setSelectionModel(&sel);
    panel.setDirectory(dirPath);
    QElapsedTimer t;
    t.start();
    while (panel.entries().empty() && t.elapsed() < 8000)
        pump(10);
    CHECK(!panel.entries().empty(), "paste target directory has at least one image");

    QImage clip(24, 24, QImage::Format_RGB32);
    clip.fill(QColor(90, 40, 20));
    QApplication::clipboard()->setImage(clip);
    const QStringList written = panel.pasteClipboardIntoCurrentDirectory();
    CHECK(written.size() == 1, "clipboard image paste writes one file");
    CHECK(QFile::exists(written.first()), "pasted clipboard image exists on disk");
    CHECK(QFileInfo(written.first()).fileName().startsWith(QStringLiteral("clipboard-")),
          "raw clipboard paste uses clipboard-YYYYMMDD-HHMMSS name");
    CHECK(panel.selectedPaths().contains(written.first()),
          "pasted clipboard image becomes the gallery selection");

    const QString source = writePng(QDir(QDir::tempPath()), "paste_url_src.png", QColor(1, 2, 3));
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(source)});
    QApplication::clipboard()->setMimeData(mime);
    const QStringList copied = panel.pasteClipboardIntoCurrentDirectory();
    CHECK(copied.size() == 1, "file-URL paste copies one image");
    CHECK(QFileInfo(copied.first()).fileName() == QStringLiteral("paste_url_src.png"),
          "file-URL paste keeps the original file name");
    CHECK(QFile::exists(copied.first()), "copied file exists in the browse directory");

    auto *mime2 = new QMimeData;
    mime2->setUrls({QUrl::fromLocalFile(source)});
    QApplication::clipboard()->setMimeData(mime2);
    const QStringList dup = panel.pasteClipboardIntoCurrentDirectory();
    CHECK(dup.size() == 1 &&
              QFileInfo(dup.first()).fileName() == QStringLiteral("paste_url_src-2.png"),
          "duplicate paste appends -2 before the extension");

    MainWindow window;
    window.show();
    pump(50);
    auto *openFolder = window.findChild<QAction *>(QStringLiteral("openCurrentFolderAction"));
    CHECK(openFolder != nullptr, "browse toolbar exposes openCurrentFolderAction");
    CHECK(openFolder->text() == QStringLiteral("Open Folder"),
          "open-folder action uses the English source string");
    ThumbnailPanel empty;
    empty.openCurrentFolder();
    panel.openCurrentFolder();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-browse-paste-test");
    QCoreApplication::setApplicationName("mviewer-browse-paste-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::cerr << "no temp dir\n";
        return 1;
    }
    QDir pasteDir(tmp.filePath("paste_folder"));
    pasteDir.mkpath(".");
    testPasteIntoFolderAndOpenAction(pasteDir.absolutePath());

    if (g_failures > 0)
    {
        std::cout << "browse_paste_acceptance: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "browse_paste_acceptance: PASS\n";
    return 0;
}
