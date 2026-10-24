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

#include "appstate.h"
#include "compareworkspace_caption.h"
#include "core/thumbnail/ThumbnailPipeline.h"
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
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>
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
    img.save(filePath);
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
    createDummyImage(QDir(leftDir).filePath(QStringLiteral("l1.png")));
    createDummyImage(QDir(leftDir).filePath(QStringLiteral("l2.png")));
    const QString r1 = QDir(rightDir).filePath(QStringLiteral("r1.png"));
    const QString r2 = QDir(rightDir).filePath(QStringLiteral("r2.png"));
    createDummyImage(r1);
    createDummyImage(r2);

    int savedScroll = 0;
    {
        MainWindow mw;
        mw.resize(1024, 768);
        mw.show();
        QApplication::processEvents();
        mw.openExternalTargets({leftDir});
        mw.toggleSplitBrowse(true);
        mw.openRightDirectory(rightDir);
        EXPECT(waitUntil(
                   [&]()
                   {
                       auto *sec = mw.secondaryThumbnailPanel();
                       return sec && sec->pathList().size() >= 2;
                   }),
               "right pane scan populated");
        ThumbnailPanel *sec = mw.secondaryThumbnailPanel();
        if (!sec)
            return;
        sec->setSort(ThumbnailPanel::SortSize, false);
        sec->selectPath(r2);
        if (sec->verticalScrollBar())
        {
            sec->verticalScrollBar()->setMaximum(400);
            sec->verticalScrollBar()->setValue(48);
            savedScroll = sec->scrollOffset();
        }
        AppState persist;
        persist.splitBrowseEnabled = true;
        persist.lastDir = leftDir;
        persist.secondaryLastDir = rightDir;
        persist.secondaryLastImage = r2;
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
                   return sec && sec->isVisible() && sec->pathList().size() >= 2;
               }),
           "restored right pane populated");
    ThumbnailPanel *sec = restored.secondaryThumbnailPanel();
    if (!sec)
        return;
    EXPECT(sec->sortMode() == ThumbnailPanel::SortSize, "right pane sort field restored");
    EXPECT(!sec->sortAscending(), "right pane sort order restored independently");
    EXPECT(sec->currentDir() == QDir::cleanPath(rightDir) ||
               QDir(sec->currentDir()) == QDir(rightDir),
           "right pane directory restored");
    if (savedScroll > 0)
        EXPECT(sec->scrollOffset() == savedScroll, "right pane scroll restored");
    const QString current = sec->pathList().value(sec->currentIndex().row());
    EXPECT(QFileInfo(current).fileName() == QStringLiteral("r2.png") ||
               sec->rowForPath(r2) == sec->currentIndex().row(),
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
        ++actions;
        if (!act->icon().isNull())
            ++withIcon;
        else
            std::cerr << "missing toolbar icon: " << act->text().toStdString() << std::endl;
    }
    EXPECT(actions > 0, "toolbar has actions");
    EXPECT(actions == withIcon, "every QAction on the main toolbar has a non-null icon");
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

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PASS" : "FAILURES DETECTED") << " ("
              << g_failures << " failures)" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
