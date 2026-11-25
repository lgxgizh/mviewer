// M17: Plugin Settings page implementation.
#include "pluginsettings.h"

#include "Theme.h"
#include "core/plugin/PluginManager.h"
#include "core/plugin/PluginPathPolicy.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace
{

constexpr int kRoleName = Qt::UserRole;
constexpr int kRolePath = Qt::UserRole + 1;
constexpr int kRoleSha = Qt::UserRole + 2;

void discoverDirectory(const QString &dir, QStringList &paths, QStringList &hashes)
{
    if (!QDir(dir).exists())
        return;
    const auto candidates = PluginManager::scanDirectory(dir.toUtf8().toStdString());
    for (const std::string &path : candidates)
    {
        std::string hashError;
        const std::string hash = mviewer::core::pluginFileSha256(path, hashError);
        if (!mviewer::core::isPluginSha256Hex(hash))
            continue;
        const QString pathText = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
        if (paths.contains(pathText))
            continue;
        paths.append(pathText);
        hashes.append(QString::fromStdString(hash));
    }
}

bool userConfirmsPluginTrust(QWidget *parent, const std::vector<ExplicitPluginTrust> &items)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("Trust and load plugins"));
    box.setText(QObject::tr("Trust these native plugin files and load them into this process?"));
    box.setInformativeText(QString::fromStdString(pluginTrustPromptBody(items)));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    if (QAbstractButton *yes = box.button(QMessageBox::Yes))
        yes->setText(QObject::tr("Trust and load"));
    return box.exec() == QMessageBox::Yes;
}

} // namespace

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

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mainLayout->addWidget(m_list);

    auto *btnLayout = new QHBoxLayout();

    m_scanBtn = new QPushButton(tr("Rescan"), this);
    m_scanBtn->setToolTip(
        tr("Find plugin files and show each path and SHA-256. Does not load them."));
    connect(m_scanBtn, &QPushButton::clicked, this, &PluginSettings::rescanPlugins);
    btnLayout->addWidget(m_scanBtn);

    m_trustBtn = new QPushButton(tr("Trust and load..."), this);
    m_trustBtn->setToolTip(
        tr("Ask before mapping the selected plugin files. The default is Cancel."));
    connect(m_trustBtn, &QPushButton::clicked, this, &PluginSettings::onTrustAndLoad);
    btnLayout->addWidget(m_trustBtn);

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
    const QIcon loadedIcon(QApplication::style()->standardIcon(QStyle::SP_CommandLink));
    const QIcon disabledIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserStop));
    const QIcon foundIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));

    int enabled = 0;
    for (const auto &p : plugins)
    {
        const QString pluginName = QString::fromStdString(p.name);
        const bool disabled = m_disabledPlugins.contains(pluginName);
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
        item->setData(kRoleName, pluginName);
        item->setData(kRolePath, QString::fromUtf8(p.path.data(), static_cast<int>(p.path.size())));
        m_list->addItem(item);
    }

    int notLoaded = 0;
    for (int i = 0; i < m_discoveredPaths.size() && i < m_discoveredHashes.size(); ++i)
    {
        const QString path = m_discoveredPaths.at(i);
        if (pm.isLoaded(path.toUtf8().toStdString()))
            continue;
        const QString sha = m_discoveredHashes.at(i);
        auto *item =
            new QListWidgetItem(foundIcon, tr("Not loaded — %1 — SHA-256 %2").arg(path, sha));
        item->setData(kRolePath, path);
        item->setData(kRoleSha, sha);
        item->setToolTip(path);
        m_list->addItem(item);
        ++notLoaded;
    }

    if (m_list->count() == 0)
    {
        auto *item = new QListWidgetItem(tr("(No plugins detected)"));
        item->setFlags(Qt::NoItemFlags);
        m_list->addItem(item);
    }
    if (m_statusLabel)
    {
        m_statusLabel->setText(tr("Loaded %1 plugins (%2 enabled), %3 not loaded")
                                   .arg(static_cast<int>(plugins.size()))
                                   .arg(enabled)
                                   .arg(notLoaded));
    }
}

void PluginSettings::rescanPlugins()
{
    // Discovery only. Hashing reads bytes. It does not map unknown modules.
    m_discoveredPaths.clear();
    m_discoveredHashes.clear();
    const QStringList paths = m_searchPath->text().split(';', Qt::SkipEmptyParts);
    for (const QString &dir : paths)
        discoverDirectory(dir, m_discoveredPaths, m_discoveredHashes);
    const QString def = QCoreApplication::applicationDirPath() + "/plugins";
    if (!paths.contains(def))
        discoverDirectory(def, m_discoveredPaths, m_discoveredHashes);

    refreshList();
    if (m_statusLabel)
    {
        m_statusLabel->setText(m_statusLabel->text() +
                               tr(" · Discovered %1 file(s). This scan did not load plugins.")
                                   .arg(m_discoveredPaths.size()));
    }
}

void PluginSettings::onTrustAndLoad()
{
    std::vector<ExplicitPluginTrust> selected;
    const auto items = m_list->selectedItems();
    for (QListWidgetItem *item : items)
    {
        if (!item)
            continue;
        const QString path = item->data(kRolePath).toString();
        const QString listedSha = item->data(kRoleSha).toString();
        if (path.isEmpty() || listedSha.isEmpty())
            continue;
        std::string hashError;
        const std::string hash =
            mviewer::core::pluginFileSha256(path.toUtf8().toStdString(), hashError);
        if (!mviewer::core::isPluginSha256Hex(hash))
            continue;
        ExplicitPluginTrust one;
        one.path = path.toUtf8().toStdString();
        one.sha256Hex = hash;
        selected.push_back(std::move(one));
    }
    if (selected.empty())
    {
        if (m_statusLabel)
            m_statusLabel->setText(tr("Select a discovered plugin file. Nothing was loaded."));
        return;
    }

    if (!userConfirmsPluginTrust(this, selected))
    {
        if (m_statusLabel)
            m_statusLabel->setText(tr("Trust and load cancelled. Nothing was loaded."));
        return;
    }

    const auto results = PluginManager::instance().commitExplicitTrust(selected, true);
    QSettings settings;
    QStringList trusted = settings.value(QStringLiteral("plugins/trustedSha256")).toStringList();
    int loaded = 0;
    for (const ExplicitPluginTrustResult &result : results)
    {
        if (result.loaded)
            ++loaded;
        if (!result.persist)
            continue;
        const QString hex = QString::fromStdString(result.sha256Hex);
        if (!hex.isEmpty() && !trusted.contains(hex))
            trusted.append(hex);
    }
    settings.setValue(QStringLiteral("plugins/trustedSha256"), trusted);
    refreshList();
    if (m_statusLabel)
        m_statusLabel->setText(m_statusLabel->text() + tr(" · %1 trusted and loaded").arg(loaded));
    if (loaded > 0)
        emit pluginsChanged();
}

void PluginSettings::onTogglePlugin()
{
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QString name = item->data(kRoleName).toString();
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
