// M17: Plugin Settings page implementation.
#include "pluginsettings.h"

#include "core/plugin/PluginManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

PluginSettings::PluginSettings(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("插件管理"));
    resize(600, 420);
    setupUi();
    loadSettings();
    refreshList();
}

void PluginSettings::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // ── Plugin list ──
    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    mainLayout->addWidget(m_list);

    // ── Buttons ──
    auto *btnLayout = new QHBoxLayout();

    m_scanBtn = new QPushButton(tr("重新扫描"), this);
    connect(m_scanBtn, &QPushButton::clicked, this, &PluginSettings::refreshList);
    btnLayout->addWidget(m_scanBtn);

    m_toggleBtn = new QPushButton(tr("启用 / 禁用"), this);
    connect(m_toggleBtn, &QPushButton::clicked, this, &PluginSettings::onTogglePlugin);
    btnLayout->addWidget(m_toggleBtn);

    btnLayout->addStretch();

    m_addBtn = new QPushButton(tr("添加搜索路径..."), this);
    connect(m_addBtn, &QPushButton::clicked, this, &PluginSettings::onAddPluginPath);
    btnLayout->addWidget(m_addBtn);

    m_browseBtn = new QPushButton(tr("浏览搜索目录..."), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &PluginSettings::onBrowsePluginDir);
    btnLayout->addWidget(m_browseBtn);

    mainLayout->addLayout(btnLayout);

    // ── Search path display ──
    auto *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(new QLabel(tr("插件搜索路径:"), this));
    m_searchPath = new QLineEdit(this);
    m_searchPath->setReadOnly(true);
    pathLayout->addWidget(m_searchPath);
    mainLayout->addLayout(pathLayout);

    // ── Close button ──
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(btnBox);
}

void PluginSettings::loadSettings()
{
    QSettings settings;
    m_disabledPlugins =
        settings.value("plugins/disabled", QStringList()).toStringList();
    const QStringList paths =
        settings.value("plugins/searchPaths",
                       QStringList{QCoreApplication::applicationDirPath() + "/plugins"})
            .toStringList();
    if (!paths.isEmpty())
        m_searchPath->setText(paths.join(';'));
}

void PluginSettings::saveSettings()
{
    QSettings settings;
    settings.setValue("plugins/disabled", m_disabledPlugins);
}

void PluginSettings::refreshList()
{
    m_list->clear();

    auto &pm = PluginManager::instance();
    const auto plugins = pm.loadedPlugins();

    if (plugins.empty())
    {
        auto *item = new QListWidgetItem(tr("(未检测到任何插件)"));
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
        return;
    }

    const QIcon loadedIcon(
        QApplication::style()->standardIcon(QStyle::SP_CommandLink));
    const QIcon disabledIcon(
        QApplication::style()->standardIcon(QStyle::SP_BrowserStop));

    for (const auto &p : plugins)
    {
        const QString pluginName = QString::fromStdString(p.name);
        bool disabled = m_disabledPlugins.contains(pluginName);

        // Build capability description from the entry's IDs
        QStringList caps;
        if (!p.analyzerId.empty())
            caps << tr("分析器");
        if (!p.decoderId.empty())
            caps << tr("解码器");
        if (!p.exporterId.empty())
            caps << tr("导出器");
        const QString capStr = caps.isEmpty() ? tr("通用") : caps.join(", ");

        QString label;
        if (disabled)
            label = tr("[已禁用] %1 [%2]")
                        .arg(pluginName, capStr);
        else
            label = tr("%1 [%2] — %3")
                        .arg(pluginName)
                        .arg(capStr)
                        .arg(QString::fromStdString(p.path));

        auto *item = new QListWidgetItem(disabled ? disabledIcon : loadedIcon, label);
        item->setData(Qt::UserRole, pluginName);
        item->setToolTip(QString::fromStdString(p.path));
        m_list->addItem(item);
    }
}

void PluginSettings::onTogglePlugin()
{
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();
    if (name.isEmpty())
        return;

    if (m_disabledPlugins.contains(name))
        m_disabledPlugins.removeAll(name);
    else
        m_disabledPlugins.append(name);

    saveSettings();
    refreshList();
}

void PluginSettings::onAddPluginPath()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("添加插件搜索目录"));
    if (dir.isEmpty())
        return;

    QSettings settings;
    QStringList paths =
        settings.value("plugins/searchPaths",
                       QStringList{QCoreApplication::applicationDirPath() + "/plugins"})
            .toStringList();
    if (!paths.contains(dir))
    {
        paths.append(dir);
        settings.setValue("plugins/searchPaths", paths);
        m_searchPath->setText(paths.join(';'));
    }
}

void PluginSettings::onRemovePluginPath()
{
    QSettings settings;
    QStringList paths =
        settings.value("plugins/searchPaths",
                       QStringList{QCoreApplication::applicationDirPath() + "/plugins"})
            .toStringList();
    paths.clear();
    settings.setValue("plugins/searchPaths", paths);
    m_searchPath->setText(QCoreApplication::applicationDirPath() + "/plugins");
}

void PluginSettings::onBrowsePluginDir()
{
    const QStringList paths =
        m_searchPath->text().split(';', Qt::SkipEmptyParts);
    if (!paths.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
}
