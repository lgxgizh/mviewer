#pragma once

#include <QString>
#include <QStringList>

class ICommand;
class ThumbnailPanel;

// Paths just copied or moved into one gallery pane. Merged into the next
// incremental selection so an async filter still selects them.
struct ThumbnailPanelTransferState
{
    QStringList pendingSelection;

    void mergeIntoNextSelection(QStringList &previousSelection, QString &previousCurrent)
    {
        if (pendingSelection.isEmpty())
            return;
        for (const QString &path : pendingSelection)
        {
            if (!path.isEmpty() && !previousSelection.contains(path))
                previousSelection.append(path);
        }
        if (previousCurrent.isEmpty())
            previousCurrent = pendingSelection.last();
        pendingSelection.clear();
    }
};

// Copy and move into another directory, including the other split-browse pane.
// ThumbnailPanel friends this type: the operations have to use the private
// command path (progress, undo, busy flag) and the selection model's split-mode
// bit, which are not on the public API. Everything else goes through public
// methods (selectedPaths, currentDir, paneSlot, signals).
class ThumbnailPanelTransfer
{
  public:
    static void copyPathsInto(ThumbnailPanel &panel, const QStringList &paths,
                              const QString &destinationDirectory);
    static void movePathsInto(ThumbnailPanel &panel, const QStringList &paths,
                              const QString &destinationDirectory);
    static void copySelectedToOtherPane(ThumbnailPanel &panel);
    static void moveSelectedToOtherPane(ThumbnailPanel &panel);
    static void showTransferredFiles(ThumbnailPanel &panel, const QString &destinationDirectory,
                                     const QStringList &createdPaths);
    static void publishTransferIfAny(ThumbnailPanel &panel, const ICommand *command);
};
