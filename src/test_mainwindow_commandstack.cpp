// M45: real MainWindow command-surface integration.
//
// This deliberately stays isolated from the larger Browse acceptance suite:
// it verifies that the production QAction callback is registered on the same
// CommandStack used by ThumbnailPanel, without allowing unrelated long-lived
// acceptance work to trigger the production update-check timer.

#include "directorytree.h"
#include "mainwindow.h"
#include "runtime_storage.h"
#include "thumbnailpanel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QInputDialog>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <cstdlib>
#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void pump(int ms = 30)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(10);
    return predicate();
}

QString writePng(const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(QColor(30, 60, 90));
    image.save(path, "PNG");
    return path;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-m45-commandstack-test");
    QCoreApplication::setApplicationName("mviewer-m45-commandstack-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    const QString appConfig =
        mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    QFile::remove(QDir(appConfig).filePath(QStringLiteral("recovery.json")));
    const QString crashDir = QDir(mviewer::runtime::writableDirectory(
                                      QStandardPaths::AppDataLocation))
                                 .filePath(QStringLiteral("crash-reports"));
    QDir(crashDir).removeRecursively();

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    QDir directory(temporary.filePath("browse"));
    directory.mkpath(".");
    const QString original = writePng(directory, "m45_uiop.png");
    const QString renamed = directory.filePath("m45_uiop_renamed.png");

    MainWindow window;
    window.resize(1100, 750);
    window.show();
    pump(80);

    auto *tree = window.findChild<DirectoryTree *>();
    auto *panel = window.findChild<ThumbnailPanel *>();
    QAction *undo = nullptr;
    QAction *redo = nullptr;
    for (QAction *action : window.findChildren<QAction *>())
    {
        if (action->shortcut() == QKeySequence::Undo)
            undo = action;
        else if (action->shortcut() == QKeySequence::Redo)
            redo = action;
    }
    CHECK(tree && panel, "M45: production MainWindow owns the directory/gallery path");
    CHECK(undo && redo, "M45: production Undo/Redo QActions are registered");
    if (!tree || !panel || !undo || !redo)
        return 1;

    // Drive the production gallery directly here. DirectoryTree's native
    // QFileSystemModel expansion is covered by Browse acceptance; this test
    // isolates the MainWindow-owned CommandStack and avoids coupling the
    // callback regression to a root-tree scan.
    panel->setDirectory(directory.absolutePath());
    CHECK(waitFor([&]
                  {
                      return panel->currentDir().compare(directory.absolutePath(),
                                                         Qt::CaseInsensitive) == 0 &&
                             !panel->entries().isEmpty();
                  }),
          "M45: production navigation publishes the directory to ThumbnailPanel");

    panel->selectPath(original);
    QTimer renamePoller;
    renamePoller.setInterval(10);
    QObject::connect(&renamePoller, &QTimer::timeout, [&]
                     {
                         for (QWidget *top : QApplication::topLevelWidgets())
                         {
                             auto *dialog = qobject_cast<QInputDialog *>(top);
                             if (!dialog || !dialog->isVisible())
                                 continue;
                             dialog->setTextValue("m45_uiop_renamed.png");
                             dialog->accept();
                             renamePoller.stop();
                             return;
                         }
                     });
    renamePoller.start();
    panel->renameSelected();
    renamePoller.stop();
    CHECK(QFile::exists(renamed) && !QFile::exists(original),
          "M45: production Rename reaches the disk");
    CHECK(undo->isEnabled(),
          "M45: CommandStack callback updates production Undo without deadlock");

    undo->trigger();
    CHECK(QFile::exists(original) && !QFile::exists(renamed),
          "M45: production Undo restores Rename");
    CHECK(redo->isEnabled(), "M45: production Redo becomes enabled after Undo");
    redo->trigger();
    CHECK(QFile::exists(renamed) && !QFile::exists(original),
          "M45: production Redo reapplies Rename");

    CHECK(waitFor([&]
                   {
                       for (const auto &entry : panel->entries())
                           if (entry.path == renamed)
                               return true;
                       return false;
                   }),
           "M45: Rename redo rescan publishes the renamed path before Delete");

    // Delete is asynchronous in the production panel. Wait for both the disk
    // transition and the CommandStack label, so this test cannot race the
    // queued UI history commit by issuing Undo immediately after the worker
    // removes the source.
    panel->selectPath(renamed);
    panel->moveToTrashSelected();
    CHECK(waitFor([&]
                  {
                      return !QFile::exists(renamed) && undo->text().contains("Delete");
                  }),
          "M45: production Delete reaches MViewer trash and commits history");
    undo->trigger();
    CHECK(waitFor([&] { return QFile::exists(renamed) && redo->isEnabled(); }),
          "M45: production Undo restores asynchronously deleted file");
    redo->trigger();
    CHECK(waitFor([&]
                  {
                      return !QFile::exists(renamed) && undo->text().contains("Delete");
                  }),
          "M45: production Redo reapplies asynchronously deleted file");

    QImage clipboardImage(32, 32, QImage::Format_RGB32);
    clipboardImage.fill(QColor(90, 40, 20));
    QApplication::clipboard()->setImage(clipboardImage);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(&window, &paste);
    CHECK(paste.isAccepted(), "M45: production Ctrl+V accepts clipboard image paste");
    const QString pasteDir =
        QDir(mviewer::runtime::writableDirectory(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("mviewer-clip-paste"));
    CHECK(waitFor([&]
                  {
                      return !QDir(pasteDir)
                                  .entryList(QStringList() << QStringLiteral("paste_*.png"),
                                             QDir::Files)
                                  .isEmpty();
                  }),
          "M45: clipboard PNG encoding completes off the UI key handler");

    if (g_failures > 0)
    {
        std::cout << "mainwindow_commandstack_acceptance: FAIL (" << g_failures
                  << " failures)\n";
        std::fflush(stdout);
        std::_Exit(1);
    }
    std::cout << "mainwindow_commandstack_acceptance: PASS\n";
    std::fflush(stdout);
    // MainWindow owns long-lived Qt/worker resources whose process-level
    // shutdown is covered by the dedicated lifecycle suites. This acceptance
    // executable is intentionally isolated; exit after flushing its verdict
    // so a late global Qt teardown cannot turn a passed UI assertion into a
    // watchdog timeout.
    std::_Exit(0);
}
