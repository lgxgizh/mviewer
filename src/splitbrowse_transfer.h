#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

#include <cstdint>

// Pure decisions for dual-pane copy/move. Kept out of thumbnailpanel.h so
// tests can call them without a gallery widget.

inline QLatin1String lastTransferDirKey()
{
    return QLatin1String("fileops/lastTransferDir");
}

inline QLatin1String fileTransferConfirmKey()
{
    return QLatin1String("confirmFileTransfer");
}

// Split mode prefers the other pane. Otherwise the last confirmed target wins,
// and the active directory is only the final fallback.
inline QString defaultTransferDir(bool splitActive, const QString &otherPaneDir,
                                  const QString &lastDir, const QString &currentDir)
{
    if (splitActive && !otherPaneDir.isEmpty())
        return otherPaneDir;
    if (!lastDir.isEmpty())
        return lastDir;
    return currentDir;
}

inline bool transferDirectoriesMatch(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty())
        return false;
    const QString a = QDir::cleanPath(QDir::fromNativeSeparators(left));
    const QString b = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

enum class OtherPaneTransferDecision : std::uint8_t
{
    Available,
    Disabled,
    SameDirectory
};

inline OtherPaneTransferDecision decideOtherPaneTransfer(bool splitActive, int selectionCount,
                                                         const QString &activeDir,
                                                         const QString &otherDir)
{
    if (!splitActive || selectionCount <= 0 || otherDir.isEmpty())
        return OtherPaneTransferDecision::Disabled;
    if (transferDirectoriesMatch(activeDir, otherDir))
        return OtherPaneTransferDecision::SameDirectory;
    return OtherPaneTransferDecision::Available;
}

enum class PaneTransferKind : std::uint8_t
{
    Copy,
    Move
};

inline QString otherPaneTransferConfirmText(PaneTransferKind kind, bool fromLeft, int count,
                                            const QString &destinationDir)
{
    const char *source =
        QT_TRANSLATE_NOOP("MainWindow", "Copy %1 file(s) from the left pane to %2");
    if (kind == PaneTransferKind::Copy && !fromLeft)
        source = QT_TRANSLATE_NOOP("MainWindow", "Copy %1 file(s) from the right pane to %2");
    else if (kind == PaneTransferKind::Move && fromLeft)
        source = QT_TRANSLATE_NOOP("MainWindow", "Move %1 file(s) from the left pane to %2");
    else if (kind == PaneTransferKind::Move)
        source = QT_TRANSLATE_NOOP("MainWindow", "Move %1 file(s) from the right pane to %2");
    return QCoreApplication::translate("MainWindow", source).arg(count).arg(destinationDir);
}

inline QString samePaneDirectoryMessage()
{
    return QCoreApplication::translate("MainWindow", "Both panes show the same directory");
}

struct TransferShortcut
{
    int key = 0;
    int mods = 0;
};

// Ctrl+Shift+C is already "copy path". Ctrl+M is Move To and Compare.
// F5 is refresh and F6 is focus browse. These two chords are outside that set.
inline TransferShortcut copyToOtherPaneShortcut()
{
    return {static_cast<int>(Qt::Key_C), static_cast<int>(Qt::ControlModifier | Qt::AltModifier)};
}

inline TransferShortcut moveToOtherPaneShortcut()
{
    return {static_cast<int>(Qt::Key_M), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)};
}

// True when `key`+`mods` is already owned by a browse command. Used by the
// unit test so a future edit cannot silently reuse F5, F6, copy-path, or Ctrl+M.
inline bool transferShortcutConflicts(int key, int mods)
{
    const int clean = mods & ~static_cast<int>(Qt::KeypadModifier);
    struct Chord
    {
        int key;
        int mods;
    };
    const Chord occupied[] = {
        {static_cast<int>(Qt::Key_F5), 0},
        {static_cast<int>(Qt::Key_F6), 0},
        {static_cast<int>(Qt::Key_F7), 0},
        {static_cast<int>(Qt::Key_C), static_cast<int>(Qt::ControlModifier)},
        {static_cast<int>(Qt::Key_C), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_M), static_cast<int>(Qt::ControlModifier)},
        {static_cast<int>(Qt::Key_S), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_F), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_A), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_I), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_R), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_H), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_B), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_O), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_Z), static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)},
        {static_cast<int>(Qt::Key_A), static_cast<int>(Qt::ControlModifier | Qt::AltModifier)},
        {static_cast<int>(Qt::Key_V), static_cast<int>(Qt::ControlModifier | Qt::AltModifier)},
        {static_cast<int>(Qt::Key_Left), static_cast<int>(Qt::ControlModifier | Qt::AltModifier)},
        {static_cast<int>(Qt::Key_Right), static_cast<int>(Qt::ControlModifier | Qt::AltModifier)},
        {static_cast<int>(Qt::Key_9), static_cast<int>(Qt::ControlModifier | Qt::AltModifier)},
    };
    for (const Chord &chord : occupied)
    {
        if (chord.key == key && chord.mods == clean)
            return true;
    }
    return false;
}
