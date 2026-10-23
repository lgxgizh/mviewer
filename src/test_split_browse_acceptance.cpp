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
//   TC-SB-08: AppState session persistence round-trip

#include "appstate.h"
#include "compareworkspace_caption.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "mainwindow.h"
#include "naturalnameorder.h"
#include "selectionmodel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>

namespace
{

int g_failures = 0;

#define EXPECT(cond, msg)                                                                     \
    do                                                                                        \
    {                                                                                         \
        if (!(cond))                                                                          \
        {                                                                                     \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl;           \
            ++g_failures;                                                                     \
        }                                                                                     \
        else                                                                                  \
        {                                                                                     \
            std::cout << "[PASS] " << (msg) << std::endl;                                     \
        }                                                                                     \
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
    EXPECT(mw.secondaryBrowsePaneWidget() != nullptr, "Secondary browse pane widget is constructed");
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
    EXPECT(captions.at(0) == QStringLiteral("baseline/0001.png"), "Caption disambiguates baseline/0001");
    EXPECT(captions.at(1) == QStringLiteral("candidate/0001.png"), "Caption disambiguates candidate/0001");
    EXPECT(captions.at(2) == QStringLiteral("baseline/0002.png"), "Caption disambiguates baseline/0002");
    EXPECT(captions.at(3) == QStringLiteral("candidate/0002.png"), "Caption disambiguates candidate/0002");
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

    EXPECT(s.save(), "AppState saves successfully");

    AppState loaded = AppState::load();
    EXPECT(loaded.splitBrowseEnabled == true, "splitBrowseEnabled restored");
    EXPECT(loaded.secondaryLastDir == s.secondaryLastDir, "secondaryLastDir restored");
    EXPECT(loaded.secondaryThumbScroll == 180, "secondaryThumbScroll restored");
    EXPECT(loaded.gallerySplitterSizes.size() >= 2, "gallerySplitterSizes restored");
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
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

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PASS" : "FAILURES DETECTED") << " ("
              << g_failures << " failures)" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
