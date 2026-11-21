// Browse Ctrl+C copies the selected files (not pixels) so Ctrl+V in File
// Explorer pastes them: file URLs (CF_HDROP on Windows), Preferred DropEffect
// = DROPEFFECT_COPY, and the native paths as text. Split browse copies the
// union of both panes. A focused text field keeps its own Ctrl+C.
#include "browse_file_clipboard.h"
#include "mainwindow.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <functional>
#include <iostream>
#include <memory>

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

bool waitUntil(const std::function<bool()> &pred, int timeoutMs = 8000)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

void writePngs(const QString &dirPath, const QString &tag, int count)
{
    QDir dir(dirPath);
    dir.mkpath(QStringLiteral("."));
    for (int i = 0; i < count; ++i)
    {
        QImage img(16, 12, QImage::Format_RGB32);
        img.fill(QColor(20 * i, 40, 200 - 20 * i));
        img.save(dir.filePath(QStringLiteral("%1_%2.png").arg(tag).arg(i)), "PNG");
    }
}

QString canonical(const QString &path)
{
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? QFileInfo(path).absoluteFilePath() : c;
}

QSet<QString> canonicalSet(const QStringList &paths)
{
    QSet<QString> out;
    for (const QString &p : paths)
        out.insert(canonical(p));
    return out;
}

QSet<QString> clipboardFiles()
{
    QSet<QString> out;
    const QMimeData *md = QApplication::clipboard()->mimeData();
    if (!md)
        return out;
    for (const QUrl &url : md->urls())
        out.insert(canonical(url.toLocalFile()));
    return out;
}

bool clipboardHasCopyEffect()
{
    const QMimeData *md = QApplication::clipboard()->mimeData();
    return md && md->data(mviewer::browseclip::preferredDropEffectMime()) ==
                     mviewer::browseclip::dropEffectCopyBytes();
}

void sendCtrlC(QWidget *target)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03"));
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03"));
    QApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

void resetClipboard()
{
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QCoreApplication::processEvents();
}

void testPayload(const QString &dirPath)
{
    std::cout << "-- payload helpers --\n";
    writePngs(dirPath, QStringLiteral("payload"), 2);
    const QDir dir(dirPath);
    const QString a = dir.filePath(QStringLiteral("payload_0.png"));
    const QString b = dir.filePath(QStringLiteral("payload_1.png"));
    const QString missing = dir.filePath(QStringLiteral("missing.png"));

    const QStringList targets = mviewer::browseclip::copyTargets({a, missing, b, a}, QString());
    CHECK(targets.size() == 2 && canonical(targets.at(0)) == canonical(a) &&
              canonical(targets.at(1)) == canonical(b),
          "copyTargets keeps existing files in order, drops missing and duplicates");
    CHECK(mviewer::browseclip::copyTargets({}, b).size() == 1,
          "copyTargets falls back to the current image without a selection");
    CHECK(mviewer::browseclip::copyTargets({}, QString()).isEmpty(),
          "copyTargets is empty with no selection and no current image");
    CHECK(mviewer::browseclip::copyTargets({missing}, QString()).isEmpty(),
          "copyTargets is empty when no selected path is a real file");

    std::unique_ptr<QMimeData> mime(mviewer::browseclip::makeFileCopyMimeData(targets));
    CHECK(mime->hasUrls() && mime->urls().size() == 2, "payload carries one URL per file");
    CHECK(mime->urls().at(0).isLocalFile(), "payload URLs are local file URLs");
    CHECK(mime->text() == QDir::toNativeSeparators(targets.at(0)) + QLatin1Char('\n') +
                              QDir::toNativeSeparators(targets.at(1)),
          "payload text is the native paths, one per line");
    const QByteArray effect = mime->data(mviewer::browseclip::preferredDropEffectMime());
    CHECK(effect.size() == 4 && static_cast<unsigned char>(effect.at(0)) == 1 &&
              effect.at(1) == 0 && effect.at(2) == 0 && effect.at(3) == 0,
          "Preferred DropEffect is DROPEFFECT_COPY (1) as a little-endian DWORD");
    CHECK(mviewer::browseclip::preferredDropEffectMime().contains(
              QStringLiteral("Preferred DropEffect")),
          "drop effect uses the native Windows format name");
    CHECK(!mime->hasImage(), "payload has no pixel data (Explorer pastes the files)");
}

void testSinglePane(const QString &dirPath)
{
    std::cout << "-- single pane Ctrl+C --\n";
    writePngs(dirPath, QStringLiteral("single"), 3);
    MainWindow mw;
    mw.resize(1200, 800);
    mw.show();
    mw.activateWindow();
    QCoreApplication::processEvents();
    mw.openExternalTargets({dirPath});
    ThumbnailPanel *panel = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    CHECK(panel && waitUntil([&]() { return panel->pathList().size() >= 3; }),
          "gallery lists the fixtures");
    if (!panel || panel->pathList().size() < 3)
        return;
    const QStringList paths = panel->pathList();
    panel->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    resetClipboard();
    panel->selectPath(paths.at(1));
    QCoreApplication::processEvents();
    sendCtrlC(panel);
    CHECK(clipboardFiles() == canonicalSet({paths.at(1)}),
          "Ctrl+C with one selected image puts that file on the clipboard");
    CHECK(clipboardHasCopyEffect(), "single copy marks Preferred DropEffect = copy");
    const QMimeData *md = QApplication::clipboard()->mimeData();
    CHECK(md && md->text() == QDir::toNativeSeparators(QFileInfo(paths.at(1)).absoluteFilePath()),
          "single copy text is the native path");

    resetClipboard();
    panel->selectPaths({paths.at(0), paths.at(2)});
    QCoreApplication::processEvents();
    sendCtrlC(panel);
    CHECK(clipboardFiles() == canonicalSet({paths.at(0), paths.at(2)}),
          "Ctrl+C with several selected images puts every selected file on the clipboard");
    CHECK(clipboardHasCopyEffect(), "multi copy marks Preferred DropEffect = copy");

    // A focused text field keeps Ctrl+C for its own text selection.
    auto *pathEdit = mw.findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    CHECK(pathEdit != nullptr, "address bar exists");
    if (pathEdit)
    {
        resetClipboard();
        pathEdit->setText(QStringLiteral("typed-text"));
        pathEdit->setFocus(Qt::OtherFocusReason);
        pathEdit->selectAll();
        QCoreApplication::processEvents();
        sendCtrlC(pathEdit);
        const QMimeData *after = QApplication::clipboard()->mimeData();
        CHECK(after && !after->hasUrls(), "Ctrl+C in the address bar does not copy files");
        CHECK(after && after->text() == QStringLiteral("typed-text"),
              "Ctrl+C in the address bar copies the selected text");
        panel->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
    }
}

void testSplitUnion(const QString &leftDir, const QString &rightDir)
{
    std::cout << "-- split browse union Ctrl+C --\n";
    writePngs(leftDir, QStringLiteral("left"), 2);
    writePngs(rightDir, QStringLiteral("right"), 2);
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    mw.activateWindow();
    QCoreApplication::processEvents();
    mw.openExternalTargets({leftDir});
    mw.toggleSplitBrowse(true);
    mw.openRightDirectory(rightDir);
    ThumbnailPanel *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    ThumbnailPanel *right = mw.secondaryThumbnailPanel();
    CHECK(waitUntil(
              [&]()
              {
                  return left && right && left->pathList().size() >= 2 &&
                         right->pathList().size() >= 2;
              }),
          "both panes listed their fixtures");
    if (!left || !right || left->pathList().size() < 2 || right->pathList().size() < 2)
        return;
    const QString leftPick = left->pathList().constFirst();
    const QString rightPick = right->pathList().constLast();
    left->selectPath(leftPick);
    right->selectPath(rightPick);
    QCoreApplication::processEvents();

    resetClipboard();
    right->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    sendCtrlC(right);
    CHECK(clipboardFiles() == canonicalSet({leftPick, rightPick}),
          "Ctrl+C in the right pane copies the union of both panes");

    resetClipboard();
    left->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    sendCtrlC(left);
    CHECK(clipboardFiles() == canonicalSet({leftPick, rightPick}),
          "Ctrl+C in the left pane copies the same union");
    CHECK(clipboardHasCopyEffect(), "union copy marks Preferred DropEffect = copy");
}
} // namespace

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-browse-copy-test"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-browse-copy-test"));

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::cerr << "no temp dir\n";
        return 1;
    }
    testPayload(tmp.filePath(QStringLiteral("payload")));
    testSinglePane(tmp.filePath(QStringLiteral("single")));
    testSplitUnion(tmp.filePath(QStringLiteral("left")), tmp.filePath(QStringLiteral("right")));

    std::cout << "browse_copy_files_tests: " << (g_failures == 0 ? "PASS" : "FAIL") << " ("
              << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
