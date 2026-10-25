// Pure dual-pane transfer decisions: default directory, enablement, and shortcuts.
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
    CHECK(copyLeft.contains(QStringLiteral("left pane")) && copyLeft.contains(QStringLiteral("2")) &&
              copyLeft.contains(QStringLiteral("/right")),
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testDefaultDir();
    testDecision();
    testConfirmText();
    testShortcuts();
    if (g_fail)
    {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "split_transfer_tests: PASS\n";
    return 0;
}
