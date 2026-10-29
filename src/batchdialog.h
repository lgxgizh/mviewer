#pragma once

#include "core/batch/BatchRenameInPlace.h"

#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

class QLabel;
class QPushButton;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;
class BatchRenamePanel;

// Tools → Batch Rename. Opens on every image of the current browse folder
// (the gallery's visible, filtered list in its current order), previews
// old → new names live and renames the original files in place. Rows can be
// unchecked to leave a file out; {seq} numbering follows the checked rows.
class BatchDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit BatchDialog(QWidget *parent = nullptr);

    // Loads the folder's images; replaces any previous list (all rows checked).
    void setFolder(const QString &directory, const QStringList &imagePaths);
    QStringList filePaths() const;
    QStringList checkedPaths() const;

    // Test seams: the recycle-bin mover, and the confirmation shown before
    // unrelated existing files are replaced (receives how many).
    void setTrashFunction(mviewer::core::TrashFunction trash);
    void setReplaceConfirmation(std::function<bool(int replaceCount)> confirm);

    // "Renamed 3 files, 1 existing file moved to the Recycle Bin, 1 failed".
    static QString summaryText(const mviewer::core::InPlaceRenameReport &report);

    // Runs the rename for the checked rows (the Rename button). Returns false
    // when nothing ran (invalid pattern, nothing to rename, or declined).
    bool renameNow();

  signals:
    // After a run that renamed at least one file, so browse can refresh.
    void filesRenamed(const QString &directory, const QString &summary);

  protected:
    // Restores the last confirmed rename settings each time the dialog opens.
    void showEvent(QShowEvent *event) override;

  private:
    void refreshPreview();
    void refreshExistingNames();
    void applyReport(const std::vector<mviewer::core::InPlaceRenameEntry> &plan,
                     const mviewer::core::InPlaceRenameReport &report);
    std::vector<mviewer::core::InPlaceRenameEntry> buildPlan(int *replaceCount) const;
    bool targetExistsOutsideBatch(const QString &name) const;

    QLabel *m_folderLabel = nullptr;
    BatchRenamePanel *m_renamePanel = nullptr;
    QTreeWidget *m_list = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPushButton *m_renameBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QString m_directory;
    QSet<QString> m_existingNames; // folded names of every file in the folder
    bool m_updating = false;
    mviewer::core::TrashFunction m_trash = mviewer::core::moveFileToSystemTrash;
    std::function<bool(int)> m_confirmReplace;
};
