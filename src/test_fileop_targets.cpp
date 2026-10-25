// Pure file-op target, rename-row, and split-selection decisions.
#include "fileop_targets.h"
#include "splitbrowse_selection.h"

#include <QCoreApplication>

#include <iostream>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << (msg) << "\n";                                                \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << (msg) << "\n";                                                \
        }                                                                                          \
    } while (0)

static void testTargets()
{
    const QStringList browse = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                QStringLiteral("d")};
    const QStringList viewer =
        resolveFileOpTargets(FileOpContext::Viewer, QStringLiteral("c"), browse);
    CHECK(viewer == QStringList{QStringLiteral("c")}, "viewer names only the open image");
    CHECK(resolveFileOpTargets(FileOpContext::Viewer, QString(), browse).isEmpty(),
          "an empty viewer path deletes nothing");
    CHECK(resolveFileOpTargets(FileOpContext::Browse, QStringLiteral("c"), browse) == browse,
          "browse keeps the caller list");
}

static void testNextIndex()
{
    CHECK(nextIndexAfterRemoval(2, 4, {2}) == 2, "deleting the third of four shows the next");
    CHECK(nextIndexAfterRemoval(3, 4, {3}) == 2, "deleting the last shows the new last");
    CHECK(nextIndexAfterRemoval(0, 1, {0}) == -1, "deleting the only image closes the viewer");
    CHECK(nextIndexAfterRemoval(1, 3, {0, 1, 2}) == -1, "deleting every image closes the viewer");
    CHECK(nextIndexAfterRemoval(0, 4, {0}) == 0, "deleting the first shows the following image");
    CHECK(nextIndexAfterRemoval(2, 4, {0}) == 1, "an earlier removal shifts the kept index");
    CHECK(nextIndexAfterRemoval(2, 4, {2, 2, 9, -1}) == 2,
          "duplicate and out-of-range indexes are ignored");
}

static void testRenameRow()
{
    CHECK(renameTargetRow({3, 1, 4}, 3) == 3, "the current row wins when it is selected");
    CHECK(renameTargetRow({3, 1, 4}, 0) == 1, "otherwise the lowest selected row wins");
    CHECK(renameTargetRow({}, 2) == -1, "no selection has no rename target");
    CHECK(renameTargetRow({2, 2, -3, 5}, -1) == 2, "duplicate and negative rows are ignored");
}

static void testKeys()
{
    CHECK(viewerFileOpForKey(Qt::Key_F2, Qt::NoModifier) == ViewerFileOp::Rename, "F2 renames");
    CHECK(viewerFileOpForKey(Qt::Key_Delete, Qt::NoModifier) == ViewerFileOp::Delete,
          "Delete deletes");
    CHECK(viewerFileOpForKey(Qt::Key_M, Qt::ControlModifier) == ViewerFileOp::Move, "Ctrl+M moves");
    CHECK(viewerFileOpForKey(Qt::Key_Backspace, Qt::NoModifier) == ViewerFileOp::None,
          "Backspace is not Delete");
    CHECK(viewerFileOpForKey(Qt::Key_F2, Qt::ControlModifier) == ViewerFileOp::None,
          "Ctrl+F2 is not rename");
    CHECK(viewerFileOpForKey(Qt::Key_F2, Qt::KeypadModifier) == ViewerFileOp::Rename,
          "keypad modifier does not hide F2");
    CHECK(viewerOwnsFileOpKeys(true, false, false), "an event on the viewer is owned there");
    CHECK(viewerOwnsFileOpKeys(false, true, true), "the active visible viewer owns the keys");
    CHECK(!viewerOwnsFileOpKeys(false, true, false),
          "a viewer behind the browser does not steal keys");
}

static void testSplitPlan()
{
    const QStringList left = {QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("gone")};
    const QStringList right = {QStringLiteral("R1")};
    const QStringList leftListed = {QStringLiteral("L1"), QStringLiteral("L2")};
    const auto plan =
        splitSelectionRestorePlan(true, left, QStringLiteral("L2"), right, QStringLiteral("R1"),
                                  leftListed, right, {}, {}, QStringLiteral("L2"));
    CHECK(plan.split, "split plan stays split");
    CHECK(plan.primaryModel == left && plan.secondaryModel == right, "models keep both snapshots");
    CHECK(plan.primaryView == leftListed, "the view drops paths missing from the pane");
    CHECK(plan.secondaryView == right, "the right view keeps its one path");
    CHECK(plan.primaryView.size() == 2 && plan.secondaryView.size() == 1,
          "left stays 2 and right stays 1");

    const QStringList single = {QStringLiteral("A"), QStringLiteral("B")};
    const auto flat = splitSelectionRestorePlan(false, left, {}, right, {}, {}, {}, single,
                                                QStringLiteral("B"), QStringLiteral("B"));
    CHECK(!flat.split && flat.primaryModel.isEmpty() && flat.secondaryModel.isEmpty(),
          "single pane does not invent pane snapshots");
    CHECK(flat.singleSelection == single && flat.singleCurrent == QStringLiteral("B"),
          "single pane passes the selection through");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testTargets();
    testNextIndex();
    testRenameRow();
    testKeys();
    testSplitPlan();
    if (g_fail > 0)
    {
        std::cerr << "fileop_targets_tests: FAIL (" << g_fail << ")\n";
        return 1;
    }
    std::cout << "fileop_targets_tests: PASS\n";
    return 0;
}
