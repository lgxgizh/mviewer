// Pure dual-pane transfer decisions: default directory, enablement, and shortcuts.
// Also covers right-pane directory history and the split status-bar text.
#include "secondarybrowsehistory.h"
#include "splitbrowse_statusbar.h"
#include "splitbrowse_transfer.h"

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

static void testDefaultDir()
{
    const QString other = QStringLiteral("/panes/right");
    const QString last = QStringLiteral("/remembered");
    const QString current = QStringLiteral("/panes/left");
    CHECK(defaultTransferDir(true, other, last, current) == other,
          "split mode starts in the other pane");
    CHECK(defaultTransferDir(true, QString(), last, current) == last,
          "an empty other pane falls back to the last target");
    CHECK(defaultTransferDir(true, QString(), QString(), current) == current,
          "split mode without a remembered target uses the active directory");
    CHECK(defaultTransferDir(false, other, last, current) == last,
          "single pane ignores the other directory and uses the last target");
    CHECK(defaultTransferDir(false, other, QString(), current) == current,
          "single pane without a remembered target uses the active directory");
    CHECK(defaultTransferDir(false, QString(), QString(), QString()).isEmpty(),
          "no known directory stays empty");
}

static void testDecision()
{
    CHECK(decideOtherPaneTransfer(false, 2, QStringLiteral("/a"), QStringLiteral("/b")) ==
              OtherPaneTransferDecision::Disabled,
          "single pane disables other-pane transfer");
    CHECK(decideOtherPaneTransfer(true, 0, QStringLiteral("/a"), QStringLiteral("/b")) ==
              OtherPaneTransferDecision::Disabled,
          "an empty selection disables other-pane transfer");
    CHECK(decideOtherPaneTransfer(true, 1, QStringLiteral("/a"), QString()) ==
              OtherPaneTransferDecision::Disabled,
          "an empty target disables other-pane transfer");
    CHECK(decideOtherPaneTransfer(true, 1, QStringLiteral("/a"), QStringLiteral("/a")) ==
              OtherPaneTransferDecision::SameDirectory,
          "the same directory is reported instead of transferring");
    CHECK(decideOtherPaneTransfer(true, 1, QStringLiteral("/a/b"), QStringLiteral("/a\\b")) ==
              OtherPaneTransferDecision::SameDirectory,
          "slash style does not make one directory look like two");
    CHECK(decideOtherPaneTransfer(true, 3, QStringLiteral("/left"), QStringLiteral("/right")) ==
              OtherPaneTransferDecision::Available,
          "split mode with a selection and a different target is available");
}

static void testConfirmText()
{
    const QString copyLeft =
        otherPaneTransferConfirmText(PaneTransferKind::Copy, true, 2, QStringLiteral("/right"));
    CHECK(copyLeft.contains(QStringLiteral("left pane")) &&
              copyLeft.contains(QStringLiteral("2")) && copyLeft.contains(QStringLiteral("/right")),
          "copy confirm names the left pane, the count, and the directory");
    const QString moveRight =
        otherPaneTransferConfirmText(PaneTransferKind::Move, false, 1, QStringLiteral("/left"));
    CHECK(moveRight.contains(QStringLiteral("right pane")) &&
              moveRight.contains(QStringLiteral("Move")),
          "move confirm names the right pane");
    CHECK(samePaneDirectoryMessage().contains(QStringLiteral("same directory")),
          "same-directory feedback names both panes");
}

static void testShortcuts()
{
    const TransferShortcut copy = copyToOtherPaneShortcut();
    const TransferShortcut move = moveToOtherPaneShortcut();
    CHECK(copy.key == static_cast<int>(Qt::Key_C), "copy to the other pane uses C");
    CHECK(move.key == static_cast<int>(Qt::Key_M), "move to the other pane uses M");
    CHECK(!transferShortcutConflicts(copy.key, copy.mods), "Ctrl+Alt+C is free");
    CHECK(!transferShortcutConflicts(move.key, move.mods), "Ctrl+Shift+M is free");
    CHECK(transferShortcutConflicts(static_cast<int>(Qt::Key_F5), 0), "F5 stays refresh");
    CHECK(transferShortcutConflicts(static_cast<int>(Qt::Key_F6), 0), "F6 stays focus browse");
    CHECK(transferShortcutConflicts(static_cast<int>(Qt::Key_C),
                                    static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)),
          "Ctrl+Shift+C stays copy path");
    CHECK(transferShortcutConflicts(static_cast<int>(Qt::Key_M),
                                    static_cast<int>(Qt::ControlModifier)),
          "Ctrl+M stays Move To");
    CHECK(copy.key != static_cast<int>(Qt::Key_F6) && move.key != static_cast<int>(Qt::Key_F6),
          "neither transfer shortcut is F6");
}

static void testSecondaryHistory()
{
    SecondaryBrowseHistory history;
    CHECK(!history.canBack() && !history.canForward(), "an empty history cannot move");
    history.push(QString());
    CHECK(history.entries().isEmpty() && history.index() == -1, "an empty path is ignored");
    history.push(QStringLiteral("C:/a"));
    history.push(QStringLiteral("C:\\a"));
    CHECK(history.entries().size() == 1, "slash style does not duplicate the current directory");
    history.push(QStringLiteral("C:/a/sub"));
    CHECK(history.canBack() && !history.canForward(), "a second directory enables back only");
    CHECK(history.back() == QStringLiteral("C:/a"), "back returns the previous directory");
    CHECK(history.canForward(), "back enables forward");
    CHECK(history.forward() == QStringLiteral("C:/a/sub"), "forward returns the newer directory");
    history.back();
    history.push(QStringLiteral("C:/a"));
    CHECK(history.entries().size() == 2 && history.canForward(),
          "pushing the current directory does not grow the stack");
    history.push(QStringLiteral("C:/other"));
    CHECK(!history.canForward() && history.current() == QStringLiteral("C:/other"),
          "a new directory drops the forward branch");

    SecondaryBrowseHistory capped;
    for (int i = 0; i < SecondaryBrowseHistory::kCap + 1; ++i)
        capped.push(QStringLiteral("/dir/%1").arg(i));
    CHECK(capped.entries().size() == SecondaryBrowseHistory::kCap, "history keeps the newest 50");
    CHECK(capped.current() == QStringLiteral("/dir/%1").arg(SecondaryBrowseHistory::kCap),
          "the newest directory stays current");
    CHECK(capped.entries().first() == QStringLiteral("/dir/1"), "the oldest directory is dropped");

    QStringList overflow;
    for (int i = 0; i < 55; ++i)
        overflow << QStringLiteral("/restore/%1").arg(i);
    SecondaryBrowseHistory restored;
    restored.restore(overflow, 54);
    CHECK(restored.entries().size() == SecondaryBrowseHistory::kCap, "restore keeps the newest 50");
    CHECK(restored.index() == 49, "restore shifts the index with the dropped prefix");
    CHECK(restored.current() == QStringLiteral("/restore/54"),
          "restore keeps the saved current directory");
    SecondaryBrowseHistory messy;
    messy.restore({QString(), QStringLiteral("/a"), QStringLiteral("/a"), QStringLiteral("/b")}, 5);
    CHECK(messy.entries().size() == 2 && messy.index() == 1,
          "restore drops blanks and clamps the index");
    SecondaryBrowseHistory empty;
    empty.restore({}, 3);
    CHECK(empty.index() == -1 && empty.current().isEmpty(), "restore of nothing clears the index");
}

static void testStatusBar()
{
    const QStringList pane{QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")};
    CHECK(activePaneImageIndex(pane, QStringLiteral("/b")) == 1,
          "the index comes from the active pane");
    CHECK(activePaneImageIndex(pane, QStringLiteral("/missing")) == -1,
          "a path outside the pane has no index");
    CHECK(activePaneImageIndex(pane, QString()) == -1, "an empty current path has no index");
    CHECK(paneSelectionStatusKind(true, 0) == PaneSelectionStatusKind::SplitUnion,
          "split mode keeps the union selection line");
    CHECK(paneSelectionStatusKind(false, 2) == PaneSelectionStatusKind::Selected,
          "a single-pane selection uses the selected line");
    CHECK(paneSelectionStatusKind(false, 0) == PaneSelectionStatusKind::Images,
          "a single pane with no selection uses the image count");
    CHECK(splitSelectionStatusText(2, 1) == QStringLiteral("Left 2 + Right 1 selected"),
          "split status names both selection counts");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testDefaultDir();
    testDecision();
    testConfirmText();
    testShortcuts();
    testSecondaryHistory();
    testStatusBar();
    if (g_fail)
    {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "split_transfer_tests: PASS\n";
    return 0;
}
