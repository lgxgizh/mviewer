#pragma once

#include "core/batch/BatchRename.h"

#include <QString>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;

// Rename pattern + find/replace editor with a live preview and inline errors.
// Shared by BatchDialog (「重命名」) and ExportDialog (「批量重命名」). The host
// restores the last confirmed settings with loadSettings() and saves them with
// saveSettings() only when the user confirms the run.
class BatchRenamePanel : public QWidget
{
    Q_OBJECT
  public:
    explicit BatchRenamePanel(QWidget *parent = nullptr);

    mviewer::core::BatchRenameOptions options() const;
    void setOptions(const mviewer::core::BatchRenameOptions &options);

    void setSourceFiles(const QStringList &files);
    // Extension (no dot) the output is re-encoded to; empty keeps the source's.
    void setTargetExtension(const QString &extension);
    // QSettings group used by loadSettings()/saveSettings().
    void setSettingsGroup(const QString &group);

    bool isValid() const;
    QString errorMessage() const;
    QLineEdit *patternEdit() const;

    void loadSettings();
    void saveSettings() const;

  signals:
    void validityChanged(bool isValid);

  private:
    void buildFindReplaceRows();
    void updatePreview();

    QLineEdit *m_patternEdit = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QCheckBox *m_useRegexCheck = nullptr;
    QCheckBox *m_caseSensitiveCheck = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_previewLabel = nullptr;

    QStringList m_sourceFiles;
    QString m_targetExtension;
    QString m_settingsGroup = QStringLiteral("batchRename");
    bool m_isValid = true;
    QString m_errorMessage;
};
