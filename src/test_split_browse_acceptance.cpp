// Acceptance test suite for Split Browse View (RFC ADR-001/005/014/018).
//
// Covers verification matrix:
//   TC-SB-01: Default state (disabled/hidden) and toggle mechanics
//   TC-SB-02: Cross-pane click without clearing (1+1 natural selection)
//   TC-SB-03: 2..8 image selection boundary check
//   TC-SB-04: Matching filename pairing sort and caption disambiguation
//   TC-SB-05: Deduplication when both panes select identical file paths
//   TC-SB-06: Selection convergence to primary on closing secondary pane
//   TC-SB-07: Dual-pane thumbnail pipeline consumer isolation
//   TC-SB-08: AppState session persistence round-trip (scroll, last image, sort)
//   TC-SB-09: Parallel column layout, sort independence and mode sync
//   TC-SB-13: Compare uses the cross-pane union (1 left + 1 right)
//   TC-SB-14: Right header matches the left; docked Compare button
//   TC-SB-15: Multi-select file type filter, RAW hidden by default
//   TC-SB-16: Type filter change keeps selection, current item and scroll
//   TC-SB-17: Type-filtered-only folder shows a "hidden by type" hint + Show

#include "appstate.h"
#include "browseformats.h"
#include "compareworkspace_caption.h"
#include "core/image/ImageFormats.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "directorytree.h"
#include "formatfiltercombo.h"
#include "mainwindow.h"
#include "naturalnameorder.h"
#include "selectionmodel.h"
#include "splitbrowse_delete.h"
#include "thumbnailpanel.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace
{

int g_failures = 0;

#define EXPECT(cond, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl;                 \
            ++g_failures;                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[PASS] " << (msg) << std::endl;                                          \
        }                                                                                          \
    } while (0)

void createDummyImage(const QString &filePath)
{
    QFileInfo info(filePath);
    QDir().mkpath(info.absolutePath());
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::blue);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    if (!img.save(&file, "PNG"))
    {
        file.close();
        QFile::remove(filePath);
        return;
    }
    file.flush();
    file.close();
}

void testDefaultAndToggle(MainWindow &mw)
{
    std::cout << "--- TC-SB-01: Default state & toggle ---" << std::endl;
    EXPECT(!mw.isSplitBrowseEnabled(), "Split browse is disabled by default");
    EXPECT(mw.secondaryBrowsePaneWidget() != nullptr,
           "Secondary browse pane widget is constructed");
    EXPECT(mw.secondaryBrowsePaneWidget()->isHidden(), "Secondary browse pane is initially hidden");

    mw.toggleSplitBrowse(true);
    EXPECT(mw.isSplitBrowseEnabled(), "Split browse is enabled after toggle(true)");
    EXPECT(!mw.secondaryBrowsePaneWidget()->isHidden(),
           "Secondary browse pane is visible after toggle(true)");

    mw.toggleSplitBrowse(false);
    EXPECT(!mw.isSplitBrowseEnabled(), "Split browse is disabled after toggle(false)");
    EXPECT(mw.secondaryBrowsePaneWidget()->isHidden(),
           "Secondary browse pane is hidden after toggle(false)");
}

void testCrossPaneNaturalSelection()
{
    std::cout << "--- TC-SB-02: Cross-pane click without clearing ---" << std::endl;
    SelectionModel sel;
    sel.setSplitModeEnabled(true);

    const QString p1 = QStringLiteral("D:/baseline/0001.png");
    const QString p2 = QStringLiteral("D:/candidate/0001.png");

    sel.setPaneSelection(SelectionModel::PaneSlot::Primary, {p1}, p1);
    EXPECT(sel.paneSelection(SelectionModel::PaneSlot::Primary) == QStringList{p1},
           "Primary pane selection recorded");
    EXPECT(sel.selection() == QStringList{p1}, "Merged selection matches single item");

    // Click in right pane must NOT clear left pane selection
    sel.setPaneSelection(SelectionModel::PaneSlot::Secondary, {p2}, p2);
    EXPECT(sel.paneSelection(SelectionModel::PaneSlot::Primary) == QStringList{p1},
           "Primary pane selection preserved after secondary click");
    EXPECT(sel.paneSelection(SelectionModel::PaneSlot::Secondary) == QStringList{p2},
           "Secondary pane selection recorded");

    const QStringList merged = sel.selection();
    EXPECT(merged.size() == 2, "Merged selection has exactly 2 images");
    EXPECT(merged.contains(p1) && merged.contains(p2), "Merged selection contains both items");
}

void testCompareBoundary(MainWindow &mw)
{
    std::cout << "--- TC-SB-03: 2..8 image selection boundary ---" << std::endl;
    mw.toggleSplitBrowse(true);
    SelectionModel *sel = mw.selectionModel();
    sel->setSplitModeEnabled(true);

    sel->clear();
    mw.updateSplitBrowseCompareButton();
    EXPECT(!mw.compareAction()->isEnabled(), "Compare is disabled for 0 images");

    sel->setPaneSelection(SelectionModel::PaneSlot::Primary, {QStringLiteral("A1")});
    mw.updateSplitBrowseCompareButton();
    EXPECT(!mw.compareAction()->isEnabled(), "Compare is disabled for 1 image");

    sel->setPaneSelection(SelectionModel::PaneSlot::Secondary, {QStringLiteral("B1")});
    mw.updateSplitBrowseCompareButton();
    EXPECT(mw.compareAction()->isEnabled(), "Compare is enabled for 2 images (1+1)");

    QStringList leftFour{QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"),
                         QStringLiteral("A4")};
    QStringList rightFour{QStringLiteral("B1"), QStringLiteral("B2"), QStringLiteral("B3"),
                          QStringLiteral("B4")};
    sel->setPaneSelection(SelectionModel::PaneSlot::Primary, leftFour);
    sel->setPaneSelection(SelectionModel::PaneSlot::Secondary, rightFour);
    mw.updateSplitBrowseCompareButton();
    EXPECT(mw.compareAction()->isEnabled(), "Compare is enabled for 8 images (4+4)");

    QStringList rightFive = rightFour;
    rightFive.append(QStringLiteral("B5"));
    sel->setPaneSelection(SelectionModel::PaneSlot::Secondary, rightFive);
    mw.updateSplitBrowseCompareButton();
    EXPECT(!mw.compareAction()->isEnabled(), "Compare is disabled for 9 images (>8)");
}

void testPairingSortAndCaptionDisambiguation(MainWindow &mw)
{
    std::cout << "--- TC-SB-04: Pairing sort & caption disambiguation ---" << std::endl;
    mw.toggleSplitBrowse(true);
    SelectionModel *sel = mw.selectionModel();
    sel->setSplitModeEnabled(true);

    const QString a1 = QStringLiteral("D:/runs/baseline/0001.png");
    const QString a2 = QStringLiteral("D:/runs/baseline/0002.png");
    const QString b1 = QStringLiteral("D:/runs/candidate/0001.png");
    const QString b2 = QStringLiteral("D:/runs/candidate/0002.png");

    sel->setPaneSelection(SelectionModel::PaneSlot::Primary, {a1, a2});
    sel->setPaneSelection(SelectionModel::PaneSlot::Secondary, {b1, b2});

    QStringList unsorted{a2, b2, a1, b1};
    QStringList sorted = mw.sortPathsForCompare(unsorted);

    EXPECT(sorted.size() == 4, "Sorted count matches");
    EXPECT(sorted.at(0) == a1, "Pair 1 baseline is first");
    EXPECT(sorted.at(1) == b1, "Pair 1 candidate is second");
    EXPECT(sorted.at(2) == a2, "Pair 2 baseline is third");
    EXPECT(sorted.at(3) == b2, "Pair 2 candidate is fourth");

    QStringList captions = disambiguateCaptions(sorted);
    EXPECT(captions.size() == 4, "Disambiguated caption count matches");
    EXPECT(captions.at(0) == QStringLiteral("baseline/0001.png"),
           "Caption disambiguates baseline/0001");
    EXPECT(captions.at(1) == QStringLiteral("candidate/0001.png"),
           "Caption disambiguates candidate/0001");
    EXPECT(captions.at(2) == QStringLiteral("baseline/0002.png"),
           "Caption disambiguates baseline/0002");
    EXPECT(captions.at(3) == QStringLiteral("candidate/0002.png"),
           "Caption disambiguates candidate/0002");
}

void testDeduplication()
{
    std::cout << "--- TC-SB-05: Deduplication for shared directory selection ---" << std::endl;
    SelectionModel sel;
    sel.setSplitModeEnabled(true);

    const QString dup = QStringLiteral("D:/common/shared.png");
    const QString uniqueA = QStringLiteral("D:/common/a.png");
    const QString uniqueB = QStringLiteral("D:/common/b.png");

    sel.setPaneSelection(SelectionModel::PaneSlot::Primary, {uniqueA, dup});
    sel.setPaneSelection(SelectionModel::PaneSlot::Secondary, {dup, uniqueB});

    const QStringList merged = sel.selection();
    EXPECT(merged.size() == 3, "Merged selection removes duplicate item");
    EXPECT(merged.count(dup) == 1, "Duplicate appears exactly once in merged set");
}

void testSelectionConvergenceOnClose()
{
    std::cout << "--- TC-SB-06: Selection convergence on closing secondary pane ---" << std::endl;
    SelectionModel sel;
    sel.setSplitModeEnabled(true);

    const QString p1 = QStringLiteral("D:/left/0001.png");
    const QString p2 = QStringLiteral("D:/right/0001.png");
    const QString p3 = QStringLiteral("D:/right/0002.png");

    sel.setPaneSelection(SelectionModel::PaneSlot::Primary, {p1});
    sel.setPaneSelection(SelectionModel::PaneSlot::Secondary, {p2, p3});
    EXPECT(sel.selection().size() == 3, "Total 3 selected in split mode");

    sel.setSplitModeEnabled(false);
    EXPECT(sel.selection() == QStringList{p1}, "Converges to primary pane selection only");
    EXPECT(sel.paneSelection(SelectionModel::PaneSlot::Secondary).isEmpty(),
           "Secondary pane selection is cleared on close");
}

void testThumbnailPipelineIsolation()
{
    std::cout << "--- TC-SB-07: Dual-pane thumbnail pipeline isolation ---" << std::endl;
    ThumbnailPipeline &pipe = ThumbnailPipeline::instance();

    pipe.setSourcesForConsumer(0, {"/dirA/1.png", "/dirA/2.png"});
    pipe.setSourcesForConsumer(1, {"/dirB/1.png", "/dirB/2.png"});

    const uint64_t gen0 = pipe.consumerGeneration(0);
    const uint64_t gen1 = pipe.consumerGeneration(1);

    // Switching dir on consumer 1 must NOT bump consumer 0 generation
    pipe.setSourcesForConsumer(1, {"/dirB_new/1.png"});
    EXPECT(pipe.consumerGeneration(0) == gen0, "Consumer 0 generation is undisturbed");
    EXPECT(pipe.consumerGeneration(1) > gen1, "Consumer 1 generation advanced");
}

void testAppStatePersistence()
{
    std::cout << "--- TC-SB-08: AppState session persistence round-trip ---" << std::endl;
    AppState s;
    s.splitBrowseEnabled = true;
    s.secondaryLastDir = QStringLiteral("D:/experiments/candidate_v2");
    s.secondaryLastImage = QStringLiteral("D:/experiments/candidate_v2/0001.png");
    s.secondaryThumbScroll = 180;
    s.gallerySplitterSizes = {480, 520};
    s.hasSecondarySort = true;
    s.secondarySortField = static_cast<int>(ThumbnailPanel::SortSize);
    s.secondarySortAscending = false;

    EXPECT(s.save(), "AppState saves successfully");

    AppState loaded = AppState::load();
    EXPECT(loaded.splitBrowseEnabled == true, "splitBrowseEnabled restored");
    EXPECT(loaded.secondaryLastDir == s.secondaryLastDir, "secondaryLastDir restored");
    EXPECT(loaded.secondaryLastImage == s.secondaryLastImage, "secondaryLastImage restored");
    EXPECT(loaded.secondaryThumbScroll == 180, "secondaryThumbScroll restored");
    EXPECT(loaded.gallerySplitterSizes.size() >= 2, "gallerySplitterSizes restored");
    EXPECT(loaded.hasSecondarySort, "hasSecondarySort restored");
    EXPECT(loaded.secondarySortField == static_cast<int>(ThumbnailPanel::SortSize),
           "secondarySortField restored");
    EXPECT(loaded.secondarySortAscending == false, "secondarySortAscending restored");
    AppState().save();
}

bool waitUntil(const std::function<bool()> &pred, int tries = 80)
{
    for (int i = 0; i < tries; ++i)
    {
        if (pred())
            return true;
        QApplication::processEvents();
        QThread::msleep(25);
    }
    return pred();
}

// Layout can settle a row or so away from the saved scrollbar value.
bool restoredScrollNear(int saved, int restored, int rowStep, int scrollMax)
{
    if (saved <= 0 || restored <= 0)
        return false;
    const int step = qMax(rowStep, 1);
    const int tolerance = qMax(step, qMax(scrollMax / 10, 10));
    return qAbs(restored - saved) <= tolerance;
}

void testSessionRestoreMainWindow()
{
    std::cout << "--- TC-SB-08b: MainWindow restores right pane scroll, image, sort ---"
              << std::endl;
    QTemporaryDir tmp;
    EXPECT(tmp.isValid(), "temp dir for restore test");
    if (!tmp.isValid())
        return;
    const QString leftDir = tmp.filePath(QStringLiteral("left"));
    const QString rightDir = tmp.filePath(QStringLiteral("right"));
    QDir().mkpath(leftDir);
    QDir().mkpath(rightDir);
    constexpr int kRightImages = 24;
    for (int i = 0; i < 4; ++i)
        createDummyImage(
            QDir(leftDir).filePath(QStringLiteral("l%1.png").arg(i, 2, 10, QChar('0'))));
    QStringList rightPaths;
    rightPaths.reserve(kRightImages);
    for (int i = 0; i < kRightImages; ++i)
    {
        const QString path =
            QDir(rightDir).filePath(QStringLiteral("r%1.png").arg(i, 2, 10, QChar('0')));
        createDummyImage(path);
        rightPaths.append(path);
    }
    const QString lastImage = rightPaths.last();

    int savedScroll = 0;
    {
        MainWindow mw;
        mw.resize(1024, 768);
        mw.show();
        QApplication::processEvents();
        mw.openExternalTargets({leftDir});
        mw.toggleSplitBrowse(true);
        mw.openRightDirectory(rightDir);
        if (auto *sp = mw.findChild<QSplitter *>(QStringLiteral("gallerySplitter")))
            sp->setSizes({400, 400});
        EXPECT(waitUntil(
                   [&]()
                   {
                       auto *sec = mw.secondaryThumbnailPanel();
                       return sec && sec->pathList().size() >= kRightImages;
                   },
                   160),
               "right pane scan populated");
        ThumbnailPanel *sec = mw.secondaryThumbnailPanel();
        if (!sec)
            return;
        sec->setSort(ThumbnailPanel::SortSize, false);
        sec->selectPath(lastImage);
        EXPECT(waitUntil(
                   [&]()
                   {
                       auto *bar = sec->verticalScrollBar();
                       return bar && bar->maximum() > 0;
                   },
                   160),
               "right pane has a scroll range");
        if (auto *bar = sec->verticalScrollBar(); bar && bar->maximum() > 0)
        {
            const int target = qMax(1, bar->maximum() / 2);
            bar->setValue(target);
            QApplication::processEvents();
            savedScroll = sec->scrollOffset();
        }
        EXPECT(savedScroll > 0, "saved a non-zero right pane scroll");
        AppState persist;
        persist.splitBrowseEnabled = true;
        persist.lastDir = leftDir;
        persist.secondaryLastDir = rightDir;
        persist.secondaryLastImage = lastImage;
        persist.secondaryThumbScroll = savedScroll;
        persist.hasSecondarySort = true;
        persist.secondarySortField = static_cast<int>(ThumbnailPanel::SortSize);
        persist.secondarySortAscending = false;
        persist.gallerySplitterSizes = {400, 400};
        EXPECT(persist.save(), "restore fixture AppState saved");
    }

    MainWindow restored;
    restored.resize(1024, 768);
    restored.show();
    for (int i = 0; i < 40; ++i)
        QApplication::processEvents();
    EXPECT(restored.isSplitBrowseEnabled(), "restored window enables split browse");
    EXPECT(waitUntil(
               [&]()
               {
                   auto *sec = restored.secondaryThumbnailPanel();
                   return sec && !sec->isHidden() && sec->pathList().size() >= kRightImages;
               },
               160),
           "restored right pane populated");
    ThumbnailPanel *sec = restored.secondaryThumbnailPanel();
    if (!sec)
        return;
    EXPECT(sec->sortMode() == ThumbnailPanel::SortSize, "right pane sort field restored");
    EXPECT(!sec->sortAscending(), "right pane sort order restored independently");
    EXPECT(sec->currentDir() == QDir::cleanPath(rightDir) ||
               QDir(sec->currentDir()) == QDir(rightDir),
           "right pane directory restored");
    EXPECT(waitUntil(
               [&]()
               {
                   const int span =
                       sec->verticalScrollBar() ? sec->verticalScrollBar()->maximum() : 0;
                   return restoredScrollNear(savedScroll, sec->scrollOffset(),
                                             sec->gridSize().height(), span);
               },
               160),
           "right pane scroll restored");
    const QString current = sec->pathList().value(sec->currentIndex().row());
    EXPECT(QFileInfo(current).fileName() == QFileInfo(lastImage).fileName() ||
               sec->rowForPath(lastImage) == sec->currentIndex().row(),
           "right pane last image restored");
}

void testActivePaneRouting(MainWindow &mw)
{
    std::cout << "--- TC-SB-10: Active pane routing ---" << std::endl;
    mw.toggleSplitBrowse(true);
    EXPECT(mw.activeThumbnailPanel() ==
               mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel")),
           "active panel is primary by default");
    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
    EXPECT(mw.activeThumbnailPanel() == mw.secondaryThumbnailPanel(),
           "active pane routing picks the secondary panel");
    mw.setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    EXPECT(mw.activeThumbnailPanel() != mw.secondaryThumbnailPanel(),
           "active pane routing returns to primary");
}

void testDeleteUnionPrompt()
{
    std::cout << "--- TC-SB-11: Delete-union confirm counts ---" << std::endl;
    const QStringList left{QStringLiteral("L1"), QStringLiteral("L2")};
    const QStringList right{QStringLiteral("R1"), QStringLiteral("L2")};
    const SplitDeletePrompt prompt = splitDeletePrompt(left, right);
    EXPECT(prompt.leftCount == 2, "left pane contributes 2");
    EXPECT(prompt.rightCount == 2, "right pane contributes 2");
    EXPECT(prompt.totalCount == 3, "union total is 3 unique paths");
    const QStringList uni = splitDeleteUnion(left, right);
    EXPECT(uni.size() == 3, "union list has 3 paths");
    const QString msg = splitDeleteConfirmMessage(prompt, true);
    EXPECT(msg.contains(QStringLiteral("left: 2")), "confirm mentions left count");
    EXPECT(msg.contains(QStringLiteral("right: 2")), "confirm mentions right count");
}

void testToolbarActionIcons(MainWindow &mw)
{
    std::cout << "--- TC-SB-12: Toolbar actions have icons ---" << std::endl;
    auto *bar = mw.findChild<QToolBar *>(QStringLiteral("browserToolBar"));
    EXPECT(bar != nullptr, "main browse toolbar exists");
    if (!bar)
        return;
    int actions = 0;
    int withIcon = 0;
    for (QAction *act : bar->actions())
    {
        if (!act || act->isSeparator())
            continue;
        // Widget-backed items (spacers, insertWidget hosts, QWidgetAction) are
        // not icon tool buttons; the Sidebar collapse control is one of these.
        if (qobject_cast<QWidgetAction *>(act))
            continue;
        if (QWidget *hosted = bar->widgetForAction(act);
            hosted && !qobject_cast<QToolButton *>(hosted))
            continue;
        ++actions;
        if (!act->icon().isNull())
            ++withIcon;
        else
            std::cerr << "missing toolbar icon: " << act->text().toStdString() << std::endl;
    }
    EXPECT(actions > 0, "toolbar has actions");
    EXPECT(actions == withIcon, "every QAction on the main toolbar has a non-null icon");
    auto *splitAct = mw.findChild<QAction *>(QStringLiteral("splitBrowseAction"));
    QAction *compareAct = mw.compareAction();
    EXPECT(splitAct && compareAct &&
               splitAct->icon().pixmap(18, 18).toImage() !=
                   compareAct->icon().pixmap(18, 18).toImage(),
           "Split Browse and Compare toolbar icons are distinct");
}

void testParallelColumnLayoutAndSync(MainWindow &mw)
{
    std::cout << "--- TC-SB-09: Parallel column layout, sort independence & mode sync ---"
              << std::endl;
    mw.toggleSplitBrowse(true);

    EXPECT(mw.primaryBrowsePaneWidget() != nullptr, "Primary browse pane exists");
    EXPECT(mw.secondaryBrowsePaneWidget() != nullptr, "Secondary browse pane exists");
    EXPECT(!mw.primaryBrowsePaneWidget()->isHidden(), "Primary browse pane is visible");
    EXPECT(!mw.secondaryBrowsePaneWidget()->isHidden(), "Secondary browse pane is visible");

    auto *priPath =
        mw.primaryBrowsePaneWidget()->findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    auto *secPath =
        mw.secondaryBrowsePaneWidget()->findChild<QLineEdit *>(QStringLiteral("secondaryPathEdit"));
    EXPECT(priPath != nullptr, "Primary address bar is inside primary pane");
    EXPECT(secPath != nullptr, "Secondary address bar is inside secondary pane");

    auto *priSort = mw.primaryBrowsePaneWidget()->findChild<QComboBox *>();
    auto *secSort = mw.secondaryBrowsePaneWidget()->findChild<QComboBox *>(
        QStringLiteral("secondarySortCombo"));
    EXPECT(priSort != nullptr, "Primary sort combo exists");
    EXPECT(secSort != nullptr, "Secondary sort combo exists");

    auto *priSortDir =
        mw.primaryBrowsePaneWidget()->findChild<QPushButton *>(QStringLiteral("sortDirBtn"));
    auto *secSortDir = mw.secondaryBrowsePaneWidget()->findChild<QPushButton *>(
        QStringLiteral("secondarySortDirBtn"));
    EXPECT(priSortDir != nullptr, "Primary sort dir button exists");
    EXPECT(secSortDir != nullptr, "Secondary sort dir button exists");

    auto *secThumb = mw.secondaryBrowsePaneWidget()->findChild<ThumbnailPanel *>(
        QStringLiteral("secondaryThumbnailPanel"));
    EXPECT(secThumb != nullptr, "Secondary thumbnail panel exists");

    secSort->setCurrentIndex(secSort->findData(ThumbnailPanel::SortSize));
    EXPECT(secThumb->sortMode() == ThumbnailPanel::SortSize, "Secondary sort changed to SortSize");

    secSortDir->setChecked(true);
    EXPECT(!secThumb->sortAscending(), "Secondary sort order toggled to descending");

    mw.toggleSplitBrowse(false);
    EXPECT(mw.secondaryBrowsePaneWidget()->isHidden(),
           "Secondary pane hidden after toggleSplitBrowse(false)");
}

bool listsSuffix(const ThumbnailPanel *panel, const QString &suffix)
{
    if (!panel)
        return false;
    for (const QString &path : panel->pathList())
        if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void writeBrowseFixture(const QString &dir, const QString &tag)
{
    createDummyImage(QDir(dir).filePath(tag + QStringLiteral("_a.png")));
    createDummyImage(QDir(dir).filePath(tag + QStringLiteral("_b.png")));
    QImage bmp(16, 16, QImage::Format_RGB32);
    bmp.fill(Qt::red);
    bmp.save(QDir(dir).filePath(tag + QStringLiteral("_c.bmp")), "BMP");
    // Not a decodable DNG; it only has to be listed (or hidden) by suffix.
    QFile raw(QDir(dir).filePath(tag + QStringLiteral("_d.dng")));
    if (raw.open(QIODevice::WriteOnly))
        raw.write("II*\0fake");
}

// Regression: Compare with one image selected in each pane did nothing,
// because the button / P / Enter / context menu only read their own pane.
void testUnionCompareAndHeader(const QString &leftDir, const QString &rightDir)
{
    std::cout << "--- TC-SB-13/14: union Compare, right header, docked button ---" << std::endl;
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    mw.openExternalTargets({leftDir});
    mw.toggleSplitBrowse(true);
    mw.openRightDirectory(rightDir);
    ThumbnailPanel *left = mw.findChild<ThumbnailPanel *>(QStringLiteral("thumbnailPanel"));
    ThumbnailPanel *right = mw.secondaryThumbnailPanel();
    EXPECT(waitUntil(
               [&]()
               {
                   return left && right && left->pathList().size() >= 2 &&
                          right->pathList().size() >= 2;
               },
               160),
           "both panes listed their fixtures");
    if (!left || !right || left->pathList().isEmpty() || right->pathList().isEmpty())
        return;
    const QString leftPick = left->pathList().constFirst();
    const QString rightPick = right->pathList().constFirst();
    left->selectPath(leftPick);
    right->selectPath(rightPick);
    QApplication::processEvents();
    QStringList emitted;
    int emits = 0;
    auto record = [&](const QStringList &paths)
    {
        emitted = paths;
        ++emits;
    };
    QObject::connect(left, &ThumbnailPanel::compareRequested, &mw, record);
    QObject::connect(right, &ThumbnailPanel::compareRequested, &mw, record);
    EXPECT(left->compareSelection().size() == 2 && right->compareSelection().size() == 2,
           "each pane's compare set is the 1+1 union");
    right->requestCompare();
    EXPECT(emits == 1 && emitted.contains(leftPick) && emitted.contains(rightPick),
           "right pane Compare sends both panes' picks");
    EXPECT(mw.compareAction()->isEnabled(), "Compare action enabled for 1+1");
    mw.compareAction()->trigger();
    EXPECT(emits == 2 && emitted.size() == 2, "Compare action (P) sends the 1+1 union");

    const auto buttons = mw.findChildren<QPushButton *>(QStringLiteral("compareSelectionButton"));
    EXPECT(buttons.size() == 2, "one Compare button per pane");
    for (QPushButton *btn : buttons)
    {
        EXPECT(btn->isVisible() && btn->isEnabled(), "Compare button live for the 1+1 union");
        EXPECT(!left->isAncestorOf(btn) && !right->isAncestorOf(btn),
               "Compare button docked in the toolbar, not over the list header");
    }
    QWidget *secPane = mw.secondaryBrowsePaneWidget();
    EXPECT(secPane &&
               !secPane->findChild<QPushButton *>(QStringLiteral("secondaryDirBackButton")) &&
               !secPane->findChild<QPushButton *>(QStringLiteral("secondaryDirForwardButton")) &&
               !secPane->findChild<QPushButton *>(QStringLiteral("secondaryDirUpButton")),
           "right header has no back / forward / up buttons");
    auto *secPath = mw.findChild<QLineEdit *>(QStringLiteral("secondaryPathEdit"));
    auto *row1 = secPath ? secPath->parentWidget() : nullptr;
    EXPECT(row1 && row1->findChildren<QWidget *>(Qt::FindDirectChildrenOnly).size() == 1,
           "right address bar row holds only the path field, like the left");
}

#include "test_split_browse_mirror.inc"
#include "test_split_browse_state.inc"
#include "test_split_browse_typefilter.inc"

void testBrowseUx()
{
    QTemporaryDir tmp;
    EXPECT(tmp.isValid(), "temp dir for browse UX tests");
    if (!tmp.isValid())
        return;
    const QString leftDir = tmp.filePath(QStringLiteral("uxleft"));
    const QString rightDir = tmp.filePath(QStringLiteral("uxright"));
    QDir().mkpath(leftDir);
    QDir().mkpath(rightDir);
    writeBrowseFixture(leftDir, QStringLiteral("l"));
    writeBrowseFixture(rightDir, QStringLiteral("r"));
    testUnionCompareAndHeader(leftDir, rightDir);
    testFormatFilter(leftDir, rightDir);
    const QString stateDir = tmp.filePath(QStringLiteral("uxstate"));
    const QString rawDir = tmp.filePath(QStringLiteral("uxrawonly"));
    QDir().mkpath(stateDir);
    QDir().mkpath(rawDir);
    testTypeFilterKeepsState(stateDir);
    testTypeHiddenHint(rawDir);
    const QString thirdDir = tmp.filePath(QStringLiteral("uxthird"));
    QDir().mkpath(thirdDir);
    writeBrowseFixture(thirdDir, QStringLiteral("t"));
    testSplitMirrorsSinglePane(leftDir, rightDir, thirdDir);
    testSplitBrowseState(leftDir, rightDir, thirdDir);
}

} // namespace

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mviewer-split-browse-test"));
    QCoreApplication::setApplicationName(QStringLiteral("mviewer-split-browse-test"));
    std::cout << "=== Running Split Browse Acceptance Tests ===" << std::endl;

    MainWindow mw;
    mw.resize(1024, 768);

    testDefaultAndToggle(mw);
    testCrossPaneNaturalSelection();
    testCompareBoundary(mw);
    testPairingSortAndCaptionDisambiguation(mw);
    testDeduplication();
    testSelectionConvergenceOnClose();
    testThumbnailPipelineIsolation();
    testAppStatePersistence();
    testParallelColumnLayoutAndSync(mw);
    testActivePaneRouting(mw);
    testDeleteUnionPrompt();
    testToolbarActionIcons(mw);
    testSessionRestoreMainWindow();
    testBrowseUx();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PASS" : "FAILURES DETECTED") << " ("
              << g_failures << " failures)" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
