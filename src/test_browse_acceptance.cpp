// M24 Phase 4A — Browse workflow acceptance tests.
//
// Maps the M24 Workflow A acceptance items that were NOT yet automated:
//   A#5  view-mode switching preserves selection state (Grid/List/Details/
//        Filmstrip/Compact)
//   A#6  single / multi-select semantics (ExtendedSelection = Windows habits)
//   A#7  filter / sort / search must not corrupt selection or point at the
//        wrong image
//   A#8  rename / delete / undo keep model, thumbnails, and selection
//        consistent
//   A#9  empty dirs, corrupt files, missing dirs, overlong paths degrade
//        gracefully with feedback (failed placeholder, no crash)
//   A#10 metadata overlay reflects the current image only (no stale data
//        after switching)
//
// Runs offscreen like the rest of the suite. Uses a REAL ThumbnailPanel (with
// CommandStack) plus a REAL MainWindow for the metadata-overlay check.

#include "appstate.h"
#include "core/command/CommandStack.h"
#include "core/command/FileDeleteCommand.h"
#include "core/command/FileRenameCommand.h"
#include "mainwindow.h"
#include "metadataoverlay.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHelpEvent>
#include <QImage>
#include <QInputDialog>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolTip>

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
    } while (0)

void pump(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

QString writePng(const QDir &dir, const QString &name, QColor color)
{
    const QString path = dir.filePath(name);
    QImage img(16, 16, QImage::Format_RGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

// Path with a ~240-char component (Windows MAX_PATH is 260; long-path support
// needs \\?\ which most APIs handle, but the UI must not crash either way).
QString makeLongPathDir(const QDir &parent)
{
    QString deep = parent.absolutePath();
    const QString seg = QString(38, QChar('d'));
    for (int i = 0; i < 4; ++i)
        deep += "/" + seg;
    QDir().mkpath(deep);
    return deep;
}

// ─── A#5 + A#6: view-mode selection consistency + multi-select ───────────────
void testViewModeAndSelection(const QString &dirPath)
{
    std::cout << "── Browse A#5/A#6: view modes + selection ──\n";
    QDir dir(dirPath);
    const QStringList paths = {
        writePng(dir, "ba_a.png", QColor(200, 40, 40)),
        writePng(dir, "ba_b.png", QColor(40, 200, 40)),
        writePng(dir, "ba_c.png", QColor(40, 40, 200)),
        writePng(dir, "ba_d.png", QColor(200, 200, 40)),
    };

    SelectionModel sel;
    ThumbnailPanel panel;
    panel.setSelectionModel(&sel);
    panel.setDirectory(dirPath);
    // Wait for the async scan to land the 4 entries.
    const auto deadline = QElapsedTimer{};
    QElapsedTimer t;
    t.start();
    while (panel.entries().size() != 4 && t.elapsed() < 8000)
        pump(10);
    CHECK(panel.entries().size() == 4, "A#5: directory scan lands all entries");

    // Windows-style multi-select: Ctrl/Shift semantics are provided by
    // ExtendedSelection; programmatic multi-select must survive view switches.
    panel.setViewMode(ThumbnailPanel::Thumbnail);
    panel.selectPaths({paths[0], paths[2]}, paths[0]);
    CHECK(panel.selectedPaths().size() == 2, "A#6: multi-select via SelectionModel");
    CHECK(panel.selectionMode() == QAbstractItemView::ExtendedSelection,
          "A#6: gallery uses ExtendedSelection (Ctrl/Shift semantics)");

    // Selection must survive every view-mode switch.
    const QList<ThumbnailPanel::ViewMode> modes = {
        ThumbnailPanel::List,        ThumbnailPanel::Details,
        ThumbnailPanel::Filmstrip,   ThumbnailPanel::Compact,
        ThumbnailPanel::LargeIcon,   ThumbnailPanel::SmallIcon,
        ThumbnailPanel::Thumbnail,
    };
    bool selSurvives = true;
    for (const auto m : modes)
    {
        panel.setViewMode(m);
        pump(20);
        const QStringList selNow = panel.selectedPaths();
        if (selNow.size() != 2 || !selNow.contains(paths[0]) || !selNow.contains(paths[2]))
            selSurvives = false;
    }
    CHECK(selSurvives, "A#5: selection survives all view-mode switches");

    // Single click semantics: selecting one path collapses the selection.
    panel.selectPath(paths[1]);
    CHECK(panel.selectedPaths() == QStringList{paths[1]}, "A#6: single select replaces");

    // Selection identity follows the PATH, not the row: after sorting, the
    // selected image is still the same file (A#7 part 1).
    panel.selectPath(paths[3]);
    panel.setSortMode(ThumbnailPanel::SortName);
    pump(20);
    CHECK(panel.selectedPaths() == QStringList{paths[3]},
          "A#7: selection identity survives sort changes");

    // Filtering out the selected image must CLEAR the selection, not silently
    // point at a different image (A#7 part 2).
    panel.setTypeFilter("png");
    pump(20);
    panel.selectPath(paths[0]);
    panel.setTypeFilter("jpg"); // filters everything out
    pump(20);
    CHECK(panel.selectedPaths().isEmpty(),
          "A#7: filtering out the selection clears it instead of mis-pointing");
    CHECK(panel.entries().empty(), "A#7: filter hides all entries");
    panel.setTypeFilter(""); // restore
    pump(20);

    // Filename search narrows without destroying the selection semantics. When
    // the selected image is filtered out but rows remain, promote the first
    // visible image through the shared selection.
    panel.selectPath(paths[2]);
    panel.setFilter("ba_c");
    pump(50);
    CHECK(panel.selectedPaths() == QStringList{paths[2]},
          "A#7: search keeps the selected image when it stays visible");
    panel.setFilter("ba_a"); // selected image filtered out; ba_a remains visible
    pump(50);
    CHECK(panel.selectedPaths() == QStringList{paths[0]},
          "A#7: search promotes the first visible image when selection is filtered out");
    CHECK(sel.currentImage() == paths[0],
          "A#7: search keeps SelectionModel current on the first visible image");
    panel.setFilter("");
    pump(50);

    for (const QString &p : paths)
        QFile::remove(p);
}

// The gallery Compare affordance is the primary Browse -> Select -> Compare
// handoff. Keep its count, enablement, and request payload observable through
// a real ThumbnailPanel so text regressions cannot hide behind a green engine
// test.
void testCompareSelectionAffordance(const QString &dirPath)
{
    std::cout << "── Browse Compare selection affordance ──\n";
    QDir dir(dirPath);
    dir.mkpath(".");
    QStringList paths;
    for (int i = 0; i < 9; ++i)
        paths.append(writePng(dir, QString("compare_%1.png").arg(i), QColor(20 * i, 80, 160)));

    SelectionModel sel;
    ThumbnailPanel panel;
    panel.resize(640, 480);
    panel.show();
    panel.setSelectionModel(&sel);
    panel.setDirectory(dir.absolutePath());

    QElapsedTimer scanTimer;
    scanTimer.start();
    while (panel.entries().size() != paths.size() && scanTimer.elapsed() < 8000)
        pump(10);
    CHECK(panel.entries().size() == paths.size(),
          "Compare selection affordance: gallery scan lands all nine images");

    auto *button = panel.findChild<QPushButton *>(QStringLiteral("compareSelectionButton"));
    CHECK(button != nullptr,
          "Compare selection affordance: floating button has a stable object name");
    if (!button)
        return;

    const auto nativeToolTipShows = [button](const QString &expected)
    {
        QToolTip::hideText();
        const QPoint local = button->rect().center();
        QHelpEvent event(QEvent::ToolTip, local, button->mapToGlobal(local));
        QApplication::sendEvent(button, &event);
        pump(50);
        return QToolTip::isVisible() && QToolTip::text() == expected;
    };

    panel.selectPath(paths[0]);
    CHECK(button->isVisible() && !button->isEnabled() &&
              button->text() == QStringLiteral("比较选中 (1)") &&
              button->toolTip() == QStringLiteral("需要选择 2-8 张图片才能比较（当前 1 张）"),
          "Compare selection affordance: one selection is visible with disabled guidance");

    panel.selectPaths({paths[0], paths[2]}, paths[0]);
    CHECK(button->isVisible() && button->isEnabled() &&
              button->text() == QStringLiteral("比较选中 (2)") &&
              button->toolTip() == QStringLiteral("将选中的 2 张图片送入对比"),
          "Compare selection affordance: two selections enable the exact Compare action");
    CHECK(nativeToolTipShows(QStringLiteral("将选中的 2 张图片送入对比")),
          "Compare selection affordance: enabled button shows its native tooltip");

    int requestCount = 0;
    QStringList requestedPaths;
    QObject::connect(&panel, &ThumbnailPanel::compareRequested,
                     [&requestCount, &requestedPaths](const QStringList &requested)
                     {
                         ++requestCount;
                         requestedPaths = requested;
                     });
    button->click();
    const QStringList expectedRequest = {paths[0], paths[2]};
    CHECK(requestCount == 1 && requestedPaths == expectedRequest,
          "Compare selection affordance: clicking two selections emits one ordered request");

    panel.selectPaths(paths, paths.first());
    CHECK(button->isVisible() && !button->isEnabled() &&
              button->text() == QStringLiteral("比较选中 (9)") &&
              button->toolTip() == QStringLiteral("需要选择 2-8 张图片才能比较（当前 9 张）"),
          "Compare selection affordance: nine selections remain visible but disabled");
    CHECK(nativeToolTipShows(QStringLiteral("需要选择 2-8 张图片才能比较（当前 9 张）")),
          "Compare selection affordance: disabled button shows its native tooltip");
    button->click();
    CHECK(requestCount == 1,
          "Compare selection affordance: disabled oversized selection emits no request");
}

// ─── A#8: rename / delete / undo consistency ────────────────────────────────
void testFileOpsUndo(const QString &dirPath)
{
    std::cout << "── Browse A#8: file ops + undo ──\n";
    QDir dir(dirPath);
    const QString p1 = writePng(dir, "ba_op1.png", QColor(10, 10, 10));
    const QString p2 = writePng(dir, "ba_op2.png", QColor(20, 20, 20));

    SelectionModel sel;
    CommandStack stack;
    ThumbnailPanel panel;
    panel.setSelectionModel(&sel);
    panel.setCommandStack(&stack);
    panel.setDirectory(dirPath);
    QElapsedTimer t;
    t.start();
    while (panel.entries().size() < 2 && t.elapsed() < 8000)
        pump(10);
    CHECK(panel.entries().size() >= 2, "A#8: initial scan complete");

    // Rename through the panel's REAL flow (QInputDialog + CommandStack). The
    // dialog is modal, so intercept it and type the new name.
    panel.selectPath(p1);
    const QString renamed = dirPath + "/ba_op1_renamed.png";
    QTimer renamePoller;
    renamePoller.setInterval(10);
    QObject::connect(&renamePoller, &QTimer::timeout, [&]()
                     {
                         for (QWidget *top : QApplication::topLevelWidgets())
                         {
                             auto *dlg = qobject_cast<QInputDialog *>(top);
                             if (!dlg || !dlg->isVisible())
                                 continue;
                             dlg->setTextValue("ba_op1_renamed.png");
                             dlg->accept();
                             renamePoller.stop();
                             return;
                         }
                     });
    renamePoller.start();
    panel.renameSelected();
    renamePoller.stop();
    pump(500);
    CHECK(QFile::exists(renamed) && !QFile::exists(p1),
          "A#8: panel rename reaches the disk");
    bool hasNew = false, hasOld = false;
    for (const auto &e : panel.entries())
    {
        if (e.name.contains("ba_op1_renamed"))
            hasNew = true;
        if (e.name.contains("ba_op1.png"))
            hasOld = true;
    }
    CHECK(hasNew && !hasOld, "A#8: gallery model shows the renamed file only");
    CHECK(panel.selectedPaths() == QStringList{renamed},
          "A#8: renamed file stays selected after the rescan");

    // Undo restores the old name (command stack is the same one the panel used).
    CHECK(stack.undo(), "A#8: rename undo executes");
    panel.refresh();
    pump(500);
    CHECK(QFile::exists(p1) && !QFile::exists(renamed),
          "A#8: undo restores the original file name");

    // Delete via the command stack (moves to a test trash dir): the file must
    // leave the model and the selection must not dangle.
    panel.selectPath(p2);
    const QString trash = dirPath + "/.mviewer_trash";
    CHECK(stack.execute(std::make_unique<FileDeleteCommand>(std::vector<std::string>{p2.toStdString()},
                                                            trash.toStdString())),
          "A#8: delete command executes");
    panel.refresh();
    pump(500);
    CHECK(!QFile::exists(p2), "A#8: delete removes the file from the source dir");
    bool stillListed = false;
    for (const auto &e : panel.entries())
        if (e.name.contains("ba_op2"))
            stillListed = true;
    CHECK(!stillListed, "A#8: deleted file leaves the gallery model");
    CHECK(stack.undo(), "A#8: delete undo executes");
    panel.refresh();
    pump(500);
    CHECK(QFile::exists(p2), "A#8: undo restores the deleted file");

    for (const QString &p : {p1, renamed, p2})
        QFile::remove(p);
    QDir(trash).removeRecursively();
}

// ─── A#9: degraded environments ──────────────────────────────────────────────
void testDegradedInputs(QTemporaryDir &tmp)
{
    std::cout << "── Browse A#9: degraded inputs ──\n";

    // Empty directory: clean empty gallery, zero stats, no crash.
    {
        QDir emptyDir(tmp.filePath("empty"));
        emptyDir.mkpath(".");
        SelectionModel sel;
        ThumbnailPanel panel;
        panel.setSelectionModel(&sel);
        panel.setDirectory(emptyDir.absolutePath());
        pump(300);
        CHECK(panel.entries().isEmpty(), "A#9: empty dir shows an empty gallery");
    }

    // Corrupt file (invalid PNG bytes): decode must fail gracefully and the
    // panel must record a failed placeholder, not crash.
    {
        QDir dir(tmp.filePath("corrupt"));
        dir.mkpath(".");
        const QString good = writePng(dir, "good.png", QColor(1, 2, 3));
        const QString bad = dir.filePath("bad.png");
        QFile f(bad);
        f.open(QIODevice::WriteOnly);
        f.write("\x89PNG\r\n\x1a\n this is not a png payload", 40);
        f.close();

        SelectionModel sel;
        ThumbnailPanel panel;
        panel.setSelectionModel(&sel);
        panel.setDirectory(dir.absolutePath());
        QElapsedTimer t;
        t.start();
        while (panel.entries().size() != 2 && t.elapsed() < 8000)
            pump(10);
        // Wait a decode attempt on the corrupt file through the pipeline.
        t.restart();
        while (t.elapsed() < 3000)
            pump(50);
        CHECK(panel.thumbFailed(bad), "A#9: corrupt file recorded as failed (placeholder)");
        CHECK(!panel.thumbFailed(good),
              "A#9: healthy file unaffected by corrupt sibling");
    }

    // Missing directory: no crash, empty gallery.
    {
        SelectionModel sel;
        ThumbnailPanel panel;
        panel.setSelectionModel(&sel);
        panel.setDirectory(tmp.filePath("does_not_exist"));
        pump(300);
        CHECK(panel.entries().isEmpty(), "A#9: missing dir degrades to empty gallery");
    }

    // Overlong path: no crash, no hang.
    {
        const QString deep = makeLongPathDir(tmp.filePath("deep"));
        SelectionModel sel;
        ThumbnailPanel panel;
        panel.setSelectionModel(&sel);
        panel.setDirectory(deep);
        pump(300);
        CHECK(panel.entries().size() == 0, "A#9: overlong empty dir handled");
    }
}

// ─── A#10: metadata overlay reflects the current image only ──────────────────
void testMetadataOverlayCurrent(const QString &dirPath, const QStringList &paths)
{
    std::cout << "── Browse A#10: metadata overlay currency ──\n";

    QSettings settings;
    settings.clear();
    const QString cfg = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
    if (!cfg.isEmpty())
        QDir(cfg).removeRecursively();

    MainWindow w;
    w.resize(1100, 750);
    w.show();
    pump(80);

    // The overlay lives under the parentless top-level ImageViewer window, so
    // MainWindow::findChild cannot see it — search every top-level widget.
    MetadataOverlay *overlay = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets())
    {
        overlay = top->findChild<MetadataOverlay *>();
        if (overlay)
            break;
    }
    CHECK(overlay != nullptr, "A#10: MainWindow owns a metadata overlay");
    if (!overlay)
        return;

    overlay->showForImage(paths[0]);
    const QString first = overlay->windowTitle();
    // The overlay is a paint widget; the content path is what we can observe
    // via showForImage round-trip: switching images updates the overlay to the
    // new path without stale state.
    overlay->showForImage(paths[1]);
    overlay->hide();
    overlay->showForImage(paths[2]);
    CHECK(!overlay->currentImagePath().isEmpty() && overlay->currentImagePath() == paths[2],
          "A#10: overlay follows the latest image after show/hide cycles");

    // Overlay toggle is idempotent and safe to spam.
    for (int i = 0; i < 5; ++i)
        overlay->toggle();
    CHECK(!overlay->isVisible(), "A#10: repeated toggles end in a deterministic state");
    (void)first;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-browse-acceptance-test");
    QCoreApplication::setApplicationName("mviewer-browse-acceptance-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::cerr << "no temp dir\n";
        return 1;
    }
    QDir dir(tmp.filePath("dir"));
    dir.mkpath(".");
    const QString dirPath = dir.absolutePath();

    testViewModeAndSelection(dirPath);
    testCompareSelectionAffordance(tmp.filePath("compare_selection"));
    testFileOpsUndo(dirPath);
    testDegradedInputs(tmp);
    {
        // A#10 uses MainWindow which restores sessions; keep it isolated.
        const QStringList paths = {
            writePng(dir, "ba_m1.png", QColor(1, 2, 3)),
            writePng(dir, "ba_m2.png", QColor(4, 5, 6)),
            writePng(dir, "ba_m3.png", QColor(7, 8, 9)),
        };
        testMetadataOverlayCurrent(dirPath, paths);
    }

    if (g_failures > 0)
    {
        std::cout << "browse_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "browse_acceptance_tests: PASS\n";
    return 0;
}
