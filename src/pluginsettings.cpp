// M17: Plugin Settings page implementation.
#include "pluginsettings.h"

#include "Theme.h"
#include "core/plugin/PluginManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

PluginSettings::PluginSettings(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Plugin Manager"));
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

    m_scanBtn = new QPushButton(tr("Rescan"), this);
    m_scanBtn->setToolTip(tr("Reload plugins from the search paths and refresh the analyzer list"));
    connect(m_scanBtn, &QPushButton::clicked, this, &PluginSettings::rescanPlugins);
    btnLayout->addWidget(m_scanBtn);

    m_toggleBtn = new QPushButton(tr("Enable / Disable"), this);
    connect(m_toggleBtn, &QPushButton::clicked, this, &PluginSettings::onTogglePlugin);
    btnLayout->addWidget(m_toggleBtn);

    btnLayout->addStretch();

    m_addBtn = new QPushButton(tr("Add Search Path..."), this);
    connect(m_addBtn, &QPushButton::clicked, this, &PluginSettings::onAddPluginPath);
    btnLayout->addWidget(m_addBtn);

    m_browseBtn = new QPushButton(tr("Browse Search Directory..."), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &PluginSettings::onBrowsePluginDir);
    btnLayout->addWidget(m_browseBtn);

    mainLayout->addLayout(btnLayout);

    // ── Search path display ──
    auto *pathLayout = new QHBoxLayout();
    pathLayout->addWidget(new QLabel(tr("Plugin search path:"), this));
    m_searchPath = new QLineEdit(this);
    m_searchPath->setReadOnly(true);
    pathLayout->addWidget(m_searchPath);
    mainLayout->addLayout(pathLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary)));
    mainLayout->addWidget(m_statusLabel);

    // ── Close button ──
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(btnBox);
}

void PluginSettings::loadSettings()
{
    QSettings settings;
    m_disabledPlugins = settings.value("plugins/disabled", QStringList()).toStringList();
    const QStringList paths =
        settings
            .value("plugins/searchPaths",
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
        auto *item = new QListWidgetItem(tr("(No plugins detected)"));
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
        if (m_statusLabel)
            m_statusLabel->setText(tr("Loaded 0 plugins"));
        return;
    }

    const QIcon loadedIcon(QApplication::style()->standardIcon(QStyle::SP_CommandLink));
    const QIcon disabledIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserStop));

    int enabled = 0;
    for (const auto &p : plugins)
    {
        const QString pluginName = QString::fromStdString(p.name);
        bool disabled = m_disabledPlugins.contains(pluginName);

        // Build capability description from the entry's IDs
        QStringList caps;
        if (!p.analyzerId.empty())
            caps << tr("Analyzer");
        if (!p.decoderId.empty())
            caps << tr("Decoder");
        if (!p.exporterId.empty())
            caps << tr("Exporter");
        if (!p.importerId.empty())
            caps << tr("Importer");
        const QString capStr = caps.isEmpty() ? tr("General") : caps.join(", ");

        QString label;
        if (disabled)
            label = tr("[Disabled] %1 [%2]").arg(pluginName, capStr);
        else
        {
            ++enabled;
            label = tr("%1 [%2] — %3")
                        .arg(pluginName)
                        .arg(capStr)
                        .arg(QString::fromUtf8(p.path.data(), static_cast<int>(p.path.size())));
        }

        auto *item = new QListWidgetItem(disabled ? disabledIcon : loadedIcon, label);
        item->setData(Qt::UserRole, pluginName);
        item->setToolTip(QString::fromUtf8(p.path.data(), static_cast<int>(p.path.size())));
        m_list->addItem(item);
    }
    if (m_statusLabel)
        m_statusLabel->setText(
            tr("Loaded %1 plugins (%2 enabled)").arg(plugins.size()).arg(enabled));
}

void PluginSettings::rescanPlugins()
{
    // Reload every configured search path so newly dropped DLLs appear without
    // restarting the app. Then notify AnalysisPanel via pluginsChanged.
    auto &pm = PluginManager::instance();
    const QStringList paths = m_searchPath->text().split(';', Qt::SkipEmptyParts);
    int total = 0;
    for (const QString &dir : paths)
    {
        if (QDir(dir).exists())
            total += pm.loadDirectory(dir.toUtf8().toStdString());
    }
    // Also scan the default app plugins folder if not already listed.
    const QString def = QCoreApplication::applicationDirPath() + "/plugins";
    if (!paths.contains(def) && QDir(def).exists())
        total += pm.loadDirectory(def.toUtf8().toStdString());

    refreshList();
    if (m_statusLabel)
        m_statusLabel->setText(m_statusLabel->text() + tr(" · %1 loaded this scan").arg(total));
    emit pluginsChanged();
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
    emit pluginsChanged();
}

void PluginSettings::onAddPluginPath()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Plugin Search Directory"));
    if (dir.isEmpty())
        return;

    QSettings settings;
    QStringList paths = settings
                            .value("plugins/searchPaths",
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
    QStringList paths = settings
                            .value("plugins/searchPaths",
                                   QStringList{QCoreApplication::applicationDirPath() + "/plugins"})
                            .toStringList();
    paths.clear();
    settings.setValue("plugins/searchPaths", paths);
    m_searchPath->setText(QCoreApplication::applicationDirPath() + "/plugins");
}

void PluginSettings::onBrowsePluginDir()
{
    const QStringList paths = m_searchPath->text().split(';', Qt::SkipEmptyParts);
    if (!paths.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
}
