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
#include "core/SettingsIO.h"
#include "core/command/CommandStack.h"
#include "core/command/FileDeleteCommand.h"
#include "core/command/FileRenameCommand.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadataoverlay.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolTip>
#include <QUrl>

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

// Wait until the gallery selection/model has settled. A fixed pump(50) races
// the async directory scan and any leftover filter generation under CI load.
template <typename Pred> bool pumpUntil(Pred pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        if (pred())
            return true;
        pump(10);
    }
    return pred();
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
        ThumbnailPanel::List,      ThumbnailPanel::Details,   ThumbnailPanel::Filmstrip,
        ThumbnailPanel::Compact,   ThumbnailPanel::LargeIcon, ThumbnailPanel::SmallIcon,
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
    // visible image through the shared selection. Wait for the filtered model,
    // not a fixed pump: selection must be that image and the other rows gone.
    panel.selectPath(paths[2]);
    panel.setFilter("ba_c");
    CHECK(pumpUntil(
              [&]
              {
                  return panel.entries().size() == 1 &&
                         panel.selectedPaths() == QStringList{paths[2]};
              },
              2000),
          "A#7: search keeps the selected image when it stays visible");
    panel.setFilter("ba_a"); // selected image filtered out; ba_a remains visible
    CHECK(pumpUntil(
              [&]
              {
                  return panel.entries().size() == 1 &&
                         panel.selectedPaths() == QStringList{paths[0]};
              },
              2000),
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
              button->text() == QStringLiteral("比较 (P)") &&
              button->toolTip() == QStringLiteral("需要选择 2-8 张图片才能比较（当前 1 张）"),
          "Compare selection affordance: one selection is visible with disabled guidance");

    panel.selectPaths({paths[0], paths[2]}, paths[0]);
    CHECK(button->isVisible() && button->isEnabled() &&
              button->text() == QStringLiteral("比较 (P)") &&
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
              button->text() == QStringLiteral("比较 (P)") &&
              button->toolTip() == QStringLiteral("需要选择 2-8 张图片才能比较（当前 9 张）"),
          "Compare selection affordance: nine selections remain visible but disabled");
    CHECK(nativeToolTipShows(QStringLiteral("需要选择 2-8 张图片才能比较（当前 9 张）")),
          "Compare selection affordance: disabled button shows its native tooltip");
    button->click();
    CHECK(requestCount == 1,
          "Compare selection affordance: disabled oversized selection emits no request");
}

void driveRename(ThumbnailPanel &panel, const QString &typed, QMessageBox::StandardButton choice)
{
    QTimer poller;
    poller.setInterval(10);
    QObject::connect(&poller, &QTimer::timeout,
                     [&]()
                     {
                         const auto dismiss = [&](QWidget *top) -> bool
                         {
                             if (!top || !top->isVisible())
                                 return false;
                             if (auto *dlg = qobject_cast<QInputDialog *>(top))
                             {
                                 dlg->setTextValue(typed);
                                 dlg->accept();
                                 return true;
                             }
                             if (auto *box = qobject_cast<QMessageBox *>(top))
                             {
                                 QAbstractButton *btn = box->button(choice);
                                 if (!btn)
                                     btn = box->button(QMessageBox::Ok);
                                 if (!btn)
                                     return false;
                                 btn->click();
                                 return true;
                             }
                             return false;
                         };
                         for (QWidget *top : QApplication::topLevelWidgets())
                             if (dismiss(top))
                                 return;
                         dismiss(QApplication::activeModalWidget());
                     });
    poller.start();
    panel.renameSelected();
    poller.stop();
    pump(200);
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
    while (t.elapsed() < 8000)
    {
        bool hasOp = false;
        for (const auto &entry : panel.entries())
            if (entry.name == QStringLiteral("ba_op1.png"))
                hasOp = true;
        if (panel.entries().size() >= 2 && hasOp)
            break;
        pump(10);
    }
    CHECK(panel.entries().size() >= 2, "A#8: initial scan complete");

    panel.selectPath(p1);
    driveRename(panel, QStringLiteral("CON.png"), QMessageBox::Ok);
    CHECK(QFile::exists(p1), "A#8: reserved device name is rejected");
    driveRename(panel, QStringLiteral("ba:op.png"), QMessageBox::Ok);
    CHECK(QFile::exists(p1), "A#8: illegal Windows characters are rejected");
    driveRename(panel, QStringLiteral("ba_op2.png"), QMessageBox::Ok);
    CHECK(QFile::exists(p1) && QFile::exists(p2), "A#8: existing name is not overwritten");
    driveRename(panel, QStringLiteral("ba_op1.jpg"), QMessageBox::No);
    CHECK(QFile::exists(p1) && !QFile::exists(dir.filePath(QStringLiteral("ba_op1.jpg"))),
          "A#8: declining an extension change keeps the file");
    panel.selectPath(p1);
    driveRename(panel, QStringLiteral("ba_op1.jpg"), QMessageBox::Yes);
    const QString jpg = dir.filePath(QStringLiteral("ba_op1.jpg"));
    CHECK(QFile::exists(jpg) && !QFile::exists(p1), "A#8: confirming an extension change renames");
    CHECK(stack.undo(), "A#8: extension rename undo executes");
    panel.refresh();
    pump(300);
    CHECK(QFile::exists(p1) && !QFile::exists(jpg), "A#8: undo restores the original extension");
    panel.selectPath(p1);

    // Rename through the panel's REAL flow (QInputDialog + CommandStack). The
    // dialog is modal, so intercept it and type the new name.
    panel.selectPath(p1);
    const QString renamed = dirPath + "/ba_op1_renamed.png";
    QTimer renamePoller;
    renamePoller.setInterval(10);
    QObject::connect(&renamePoller, &QTimer::timeout,
                     [&]()
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
    CHECK(QFile::exists(renamed) && !QFile::exists(p1), "A#8: panel rename reaches the disk");
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
    CHECK(stack.execute(std::make_unique<FileDeleteCommand>(
              std::vector<std::string>{p2.toStdString()}, trash.toStdString())),
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
        CHECK(!panel.thumbFailed(good), "A#9: healthy file unaffected by corrupt sibling");
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

// ─── Details column resize: layout respects custom widths ────────────────────
void testDetailsColumnResize()
{
    std::cout << "── Browse: Details column resize ──\n";
    // Ensure a clean slate (prior runs / other tests may have written widths).
    QSettings().remove(QStringLiteral("ui/detailsColumnWidths"));

    ThumbnailPanel panel;
    const int defaultName = panel.detailColumnWidth(ThumbnailPanel::DetailColName);
    const int defaultRes = panel.detailColumnWidth(ThumbnailPanel::DetailColRes);
    const int defaultTotal = panel.detailContentWidth();
    CHECK(defaultName == 140, "Details: default name column width is 140");
    CHECK(defaultRes == 120, "Details: default resolution column width is 120");
    CHECK(defaultTotal > defaultName, "Details: content width includes all columns");

    panel.setDetailColumnWidth(ThumbnailPanel::DetailColName, 420);
    CHECK(panel.detailColumnWidth(ThumbnailPanel::DetailColName) == 420,
          "Details: setDetailColumnWidth updates name column");
    CHECK(panel.detailContentWidth() == defaultTotal + (420 - defaultName),
          "Details: content width tracks custom name width");

    // Minimum clamp (see kDetailMinW[DetailColName] == 80).
    panel.setDetailColumnWidth(ThumbnailPanel::DetailColName, 10);
    CHECK(panel.detailColumnWidth(ThumbnailPanel::DetailColName) == 80,
          "Details: name column respects minimum width");

    panel.setDetailColumnWidth(ThumbnailPanel::DetailColRes, 200);
    CHECK(panel.detailColumnWidth(ThumbnailPanel::DetailColRes) == 200,
          "Details: resolution column is independently resizable");

    panel.setViewMode(ThumbnailPanel::Details);
    pump(30);
    CHECK(panel.viewMode() == ThumbnailPanel::Details, "Details: view mode engages");

    // DetailsHeader is a plain QWidget subclass (no Q_OBJECT). The panel is not
    // shown in this offscreen test, so children report !isVisible(); look up by
    // objectName instead.
    panel.resize(900, 400);
    panel.show();
    pump(30);
    QWidget *header = panel.findChild<QWidget *>(QStringLiteral("detailsHeader"));
    CHECK(header != nullptr, "Details: column header widget is present");
    if (header)
    {
        CHECK(header->height() == 24, "Details: header uses the reserved strip height");
        CHECK(header->testAttribute(Qt::WA_TransparentForMouseEvents) == false,
              "Details: header accepts mouse events for resize");

        const QPoint clickName(80, 10);
        QMouseEvent press1(QEvent::MouseButtonPress, QPointF(clickName), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header, &press1);
        QMouseEvent release1(QEvent::MouseButtonRelease, QPointF(clickName), Qt::LeftButton,
                             Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(header, &release1);
        pump(20);

        CHECK(panel.sortMode() == ThumbnailPanel::SortName, "Details header click: sets SortName");
        const bool asc1 = panel.sortAscending();

        QMouseEvent press2(QEvent::MouseButtonPress, QPointF(clickName), Qt::LeftButton,
                           Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header, &press2);
        QMouseEvent release2(QEvent::MouseButtonRelease, QPointF(clickName), Qt::LeftButton,
                             Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(header, &release2);
        pump(20);

        CHECK(panel.sortMode() == ThumbnailPanel::SortName,
              "Details header repeat click: keeps SortName");
        CHECK(panel.sortAscending() == !asc1,
              "Details header repeat click: toggles ascending/descending");
    }

    // Persistence round-trip via QSettings.
    panel.setDetailColumnWidth(ThumbnailPanel::DetailColName, 333);
    panel.persistDetailColumnWidths();
    ThumbnailPanel panel2;
    CHECK(panel2.detailColumnWidth(ThumbnailPanel::DetailColName) == 333,
          "Details: column widths persist across sessions (QSettings)");
    QSettings().remove(QStringLiteral("ui/detailsColumnWidths"));
}

void testViewerDropAndWindowFit()
{
    std::cout << "── Browse: viewer drop and window fit ──\n";
    QSettings().remove(QStringLiteral("viewerGeometry"));
    ImageViewer viewer;
    viewer.resize(640, 480);
    viewer.show();
    pump(30);

    QStringList dropped;
    QObject::connect(&viewer, &ImageViewer::filesDropped, &viewer,
                     [&](const QStringList &paths) { dropped = paths; });
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/mviewer-drop.png"))});
    // Qt delivers Drop only to the widget that accepted DragEnter. A lone
    // QDropEvent is discarded in QApplication::notify and never reaches dropEvent.
    QDragEnterEvent enter(QPoint(8, 8), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &enter);
    QDropEvent drop(QPointF(8, 8), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &drop);
    CHECK(dropped.size() == 1 && dropped.first().endsWith(QStringLiteral("mviewer-drop.png")),
          "viewer drop emits the local file");

    const QSize windowed = viewer.size();
    viewer.setFullscreenRequested(true);
    viewer.setFullscreenRequested(false);
    pump(20);
    CHECK(qAbs(viewer.width() - windowed.width()) < 80 &&
              qAbs(viewer.height() - windowed.height()) < 80,
          "leaving fullscreen restores the windowed size");
    viewer.setFullscreenRequested(true);
    viewer.close();

    ImageViewer again;
    again.show();
    pump(20);
    QScreen *screen = QGuiApplication::primaryScreen();
    CHECK(screen != nullptr, "primary screen exists");
    const QRect avail = screen ? screen->availableGeometry() : QRect();
    CHECK(!again.isFullScreen() && avail.intersects(again.frameGeometry()) &&
              again.width() <= avail.width() && again.height() <= avail.height(),
          "a viewer opened after a fullscreen close fits the screen");

    QWidget parked;
    parked.resize(4000, 3000);
    parked.move(-20000, -20000);
    ImageViewer::clampWidgetToAvailableScreens(&parked);
    CHECK(avail.intersects(parked.frameGeometry()) && parked.width() <= avail.width() &&
              parked.height() <= avail.height(),
          "an off-screen window is pulled onto the available screen");
    QSettings().remove(QStringLiteral("viewerGeometry"));
    QSettings().remove(QStringLiteral("viewerScreen"));
}

void testSettingsPersistence(const QString &directory)
{
    std::cout << "── Browse: settings persistence ──\n";
    QSettings settings;
    settings.setValue(QStringLiteral("uxPass10"), 42);
    settings.sync();
    const QString file = QDir(directory).filePath(QStringLiteral("roundtrip.mvs"));
    std::string err;
    CHECK(mviewer::core::exportSettings(file.toStdString(), &err), "settings export writes a file");
    settings.setValue(QStringLiteral("uxPass10"), 7);
    settings.sync();
    CHECK(mviewer::core::importSettings(file.toStdString(), &err),
          "settings import applies the file");
    CHECK(QSettings().value(QStringLiteral("uxPass10")).toInt() == 42,
          "settings round-trip keeps the integer value");
    err.clear();
    CHECK(!mviewer::core::exportSettings(directory.toStdString(), &err) && !err.empty(),
          "settings export to a directory reports an error");
    QSettings().remove(QStringLiteral("uxPass10"));
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
    testDetailsColumnResize();
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
    testViewerDropAndWindowFit();
    testSettingsPersistence(tmp.path());

    if (g_failures > 0)
    {
        std::cout << "browse_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "browse_acceptance_tests: PASS\n";
    return 0;
}
