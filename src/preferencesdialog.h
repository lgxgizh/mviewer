#pragma once

#include <QDialog>

class QTabWidget;
class QComboBox;
class QSpinBox;
class QCheckBox;

// F1 (M22): centralized Preferences dialog. Reads/writes the existing QSettings
// keys plus the new toggles introduced by F2/F3/F4 (autoAlignBeforeDiff,
// defaultAnalysisOverlay). No new persistence layer — QSettings stays the
// single source of truth. Emits settingsChanged() so the main window can
// re-apply view/sort/slideshow live.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

  signals:
    void settingsChanged();

  private slots:
    void accept() override;

  private:
    QComboBox *m_viewMode = nullptr;
    QComboBox *m_sortMode = nullptr;
    QSpinBox *m_thumbSize = nullptr;
    QSpinBox *m_slideshowInterval = nullptr;
    QCheckBox *m_confirmDelete = nullptr;
    QCheckBox *m_autoAlign = nullptr;
    QComboBox *m_analysisOverlay = nullptr;
};
