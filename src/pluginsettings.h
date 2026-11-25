// M17: Plugin Settings page — list installed plugins with enable/disable toggles.
#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>

class QSettings;

class PluginSettings : public QDialog
{
    Q_OBJECT
  public:
    explicit PluginSettings(QWidget *parent = nullptr);

  signals:
    // Emitted after Trust and load or an enable-toggle so AnalysisPanel can
    // refreshAnalyzers(). Rescan does not emit this.
    void pluginsChanged();

  private slots:
    void refreshList();
    void rescanPlugins();
    void onTrustAndLoad();
    void onRevokeTrust();
    void onTogglePlugin();
    void onAddPluginPath();
    void onRemovePluginPath();
    void onBrowsePluginDir();

  private:
    void setupUi();
    void loadSettings();
    void saveSettings();

    QListWidget *m_list = nullptr;
    QPushButton *m_toggleBtn = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QPushButton *m_scanBtn = nullptr;
    QPushButton *m_trustBtn = nullptr;
    QPushButton *m_revokeBtn = nullptr;
    QLineEdit *m_searchPath = nullptr;
    QLabel *m_statusLabel = nullptr;
    QStringList m_disabledPlugins;
    // Parallel lists filled by Rescan. Paths and SHA-256 only. Rescan does not map.
    QStringList m_discoveredPaths;
    QStringList m_discoveredHashes;
};
