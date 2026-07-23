// M17: Plugin Settings page — list installed plugins with enable/disable toggles.
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

class QSettings;

class PluginSettings : public QDialog
{
    Q_OBJECT
  public:
    explicit PluginSettings(QWidget *parent = nullptr);

  private slots:
    void refreshList();
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
    QLineEdit *m_searchPath = nullptr;
    QStringList m_disabledPlugins;
};
