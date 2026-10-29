#include "batchdialog.h"
#include "Theme.h"
#include "ThemeMetrics.h"
#include "batchrenamepanel.h"

#include "core/batch/BatchRename.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr int kPathRole = Qt::UserRole;

// Same identity rule as the in-place rename: Windows names ignore case.
QString nameKey(const QString &name)
{
#ifdef Q_OS_WIN
    return name.toCaseFolded();
#else
    return name;
#endif
}

QColor roleColor(mviewer::ui::ThemeRole role)
{
    return QColor(mviewer::ui::Theme::themeColor(role));
}

QString fromUtf8(const std::string &text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

BatchDialog::BatchDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Batch Rename"));
    setMinimumSize(520, 400);
    resize(640, 520);
    setSizeGripEnabled(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(mviewer::ui::metrics::kSpace12, mviewer::ui::metrics::kSpace12,
                                   mviewer::ui::metrics::kSpace12, mviewer::ui::metrics::kSpace12);
    mainLayout->setSpacing(mviewer::ui::metrics::kSpace8);

    m_folderLabel = new QLabel;
    m_folderLabel->setObjectName(QStringLiteral("batchFolderLabel"));
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_folderLabel);

    m_renamePanel = new BatchRenamePanel;
    m_renamePanel->setPreviewVisible(false);
    m_renamePanel->patternEdit()->setPlaceholderText(QStringLiteral("{name}_{seq:3}"));
    mainLayout->addWidget(m_renamePanel);

    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("batchRenameList"));
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({tr("Current name"), tr("New name")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_list->setToolTip(tr("Uncheck a file to leave it out of the rename"));
    mainLayout->addWidget(m_list, 1);

    m_summaryLabel = new QLabel;
    m_summaryLabel->setObjectName(QStringLiteral("batchSummaryLabel"));
    m_summaryLabel->setWordWrap(true);
    mainLayout->addWidget(m_summaryLabel);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_renameBtn = new QPushButton(tr("Rename"));
    m_renameBtn->setObjectName(QStringLiteral("batchRenameButton"));
    m_renameBtn->setDefault(true);
    m_closeBtn = new QPushButton(tr("Close"));
    m_closeBtn->setObjectName(QStringLiteral("batchCloseButton"));
    buttons->addWidget(m_renameBtn);
    buttons->addWidget(m_closeBtn);
    mainLayout->addLayout(buttons);

    connect(m_renamePanel, &BatchRenamePanel::optionsChanged, this, &BatchDialog::refreshPreview);
    connect(m_list, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *, int column)
            {
                if (column == 0)
                    refreshPreview();
            });
    connect(m_renameBtn, &QPushButton::clicked, this, [this]() { renameNow(); });
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_confirmReplace = [this](int count)
    {
        return QMessageBox::question(
                   this, tr("Batch Rename"),
                   count == 1
                       ? tr("A file with the new name already exists and is not part of this "
                            "rename. Move it to the Recycle Bin and replace it?")
                       : tr("%1 files with the new names already exist and are not part "
                            "of this rename. Move them to the Recycle Bin and replace them?")
                             .arg(count),
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
    };
    m_renamePanel->loadSettings();
    refreshPreview();
}

void BatchDialog::showEvent(QShowEvent *event)
{
    m_renamePanel->loadSettings();
    QDialog::showEvent(event);
}

void BatchDialog::setFolder(const QString &directory, const QStringList &imagePaths)
{
    m_directory = directory;
    if (m_directory.isEmpty() && !imagePaths.isEmpty())
        m_directory = QFileInfo(imagePaths.first()).absolutePath();
    m_updating = true;
    m_list->clear();
    QList<QTreeWidgetItem *> items;
    for (const QString &path : imagePaths)
    {
        auto *item = new QTreeWidgetItem(QStringList{QFileInfo(path).fileName(), QString()});
        item->setData(0, kPathRole, path);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setToolTip(0, QDir::toNativeSeparators(path));
        items.append(item);
    }
    m_list->addTopLevelItems(items);
    m_updating = false;
    m_folderLabel->setText(
        (imagePaths.size() == 1 ? tr("%1 · 1 image") : tr("%1 · %2 images").arg(imagePaths.size()))
            .arg(QDir::toNativeSeparators(m_directory)));
    refreshExistingNames();
    refreshPreview();
}

QStringList BatchDialog::filePaths() const
{
    QStringList paths;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        paths << m_list->topLevelItem(i)->data(0, kPathRole).toString();
    return paths;
}

QStringList BatchDialog::checkedPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
    {
        const QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            paths << item->data(0, kPathRole).toString();
    }
    return paths;
}

void BatchDialog::setTrashFunction(mviewer::core::TrashFunction trash)
{
    m_trash = std::move(trash);
}

void BatchDialog::setReplaceConfirmation(std::function<bool(int)> confirm)
{
    m_confirmReplace = std::move(confirm);
}

void BatchDialog::refreshExistingNames()
{
    m_existingNames.clear();
    if (m_directory.isEmpty())
        return;
    const QStringList names =
        QDir(m_directory).entryList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &name : names)
        m_existingNames.insert(nameKey(name));
}

bool BatchDialog::targetExistsOutsideBatch(const QString &name) const
{
    if (!m_existingNames.contains(nameKey(name)))
        return false;
    for (const QString &path : checkedPaths())
    {
        if (nameKey(QFileInfo(path).fileName()) == nameKey(name))
            return false;
    }
    return true;
}

void BatchDialog::refreshPreview()
{
    if (m_updating)
        return;
    const QStringList checked = checkedPaths();
    m_renamePanel->setSourceFiles(checked);
    std::vector<std::string> files;
    files.reserve(static_cast<size_t>(checked.size()));
    for (const QString &path : checked)
        files.push_back(path.toUtf8().toStdString());
    std::string error;
    const auto results =
        mviewer::core::validateBatchRename(files, m_renamePanel->options(), &error);

    QSet<QString> checkedNames;
    for (const QString &path : checked)
        checkedNames.insert(nameKey(QFileInfo(path).fileName()));

    m_updating = true;
    int changes = 0;
    int replaces = 0;
    size_t next = 0;
    const QColor muted = roleColor(mviewer::ui::ThemeRole::TextDisabled);
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        item->setToolTip(1, QString());
        if (item->checkState(0) != Qt::Checked || next >= results.size())
        {
            item->setText(1, QStringLiteral("—"));
            item->setForeground(1, muted);
            continue;
        }
        const auto &result = results[next++];
        const QString newName = fromUtf8(result.newName);
        item->setText(1, newName);
        item->setForeground(1, m_list->palette().text());
        if (!result.valid)
        {
            item->setForeground(1, roleColor(mviewer::ui::ThemeRole::Danger));
            item->setToolTip(1, fromUtf8(result.errorMessage));
        }
        else if (newName == item->text(0))
        {
            item->setForeground(1, muted);
        }
        else
        {
            ++changes;
            if (!checkedNames.contains(nameKey(newName)) &&
                m_existingNames.contains(nameKey(newName)))
            {
                ++replaces;
                item->setForeground(1, roleColor(mviewer::ui::ThemeRole::Accent));
                item->setToolTip(1, tr("Replaces an existing file (it goes to the Recycle Bin)"));
            }
        }
    }
    m_updating = false;

    QString summary;
    if (!error.empty())
        summary.clear(); // the rename panel already shows the pattern error
    else if (checked.isEmpty())
        summary = tr("No files selected");
    else
        summary = tr("%1 of %2 files will be renamed in place")
                      .arg(changes)
                      .arg(m_list->topLevelItemCount());
    if (error.empty() && replaces > 0)
        summary +=
            (replaces == 1 ? tr("; 1 existing file will be replaced (moved to the Recycle Bin)")
                           : tr("; %1 existing files will be replaced (moved to the Recycle Bin)")
                                 .arg(replaces));
    m_summaryLabel->setText(summary);
    m_renameBtn->setEnabled(error.empty() && changes > 0);
}

std::vector<mviewer::core::InPlaceRenameEntry> BatchDialog::buildPlan(int *replaceCount) const
{
    const QStringList checked = checkedPaths();
    std::vector<std::string> files;
    for (const QString &path : checked)
        files.push_back(path.toUtf8().toStdString());
    const auto results = mviewer::core::validateBatchRename(files, m_renamePanel->options());
    std::vector<mviewer::core::InPlaceRenameEntry> plan;
    int replaces = 0;
    for (const auto &result : results)
    {
        if (!result.valid)
            return {};
    }
    for (const auto &result : results)
    {
        if (fromUtf8(result.newName) == QFileInfo(fromUtf8(result.originalPath)).fileName())
            continue;
        if (targetExistsOutsideBatch(fromUtf8(result.newName)))
            ++replaces;
        plan.push_back({result.originalPath, result.newName});
    }
    if (replaceCount)
        *replaceCount = replaces;
    return plan;
}

bool BatchDialog::renameNow()
{
    refreshPreview();
    if (!m_renamePanel->isValid() || !m_renameBtn->isEnabled())
        return false;
    int replaces = 0;
    const auto plan = buildPlan(&replaces);
    if (plan.empty())
        return false;
    if (replaces > 0 && m_confirmReplace && !m_confirmReplace(replaces))
        return false;

    m_renamePanel->saveSettings(); // remembered only when the run is confirmed
    const auto report = mviewer::core::renameInPlace(plan, m_trash);
    applyReport(plan, report);
    const QString summary = summaryText(report);
    if (report.renamed > 0 || report.replaced > 0)
        emit filesRenamed(m_directory, summary);
    if (report.failed == 0)
    {
        accept();
        return true;
    }
    QStringList failures;
    for (const auto &item : report.items)
    {
        if (item.outcome == mviewer::core::InPlaceRenameOutcome::Failed)
            failures << fromUtf8(item.errorMessage);
    }
    m_summaryLabel->setText(summary + QStringLiteral("\n") +
                            failures.mid(0, 3).join(QStringLiteral("\n")));
    m_summaryLabel->setToolTip(failures.join(QStringLiteral("\n")));
    return true;
}

void BatchDialog::applyReport(const std::vector<mviewer::core::InPlaceRenameEntry> &plan,
                              const mviewer::core::InPlaceRenameReport &report)
{
    QHash<QString, QString> renamed;
    for (size_t i = 0; i < plan.size() && i < report.items.size(); ++i)
    {
        if (report.items[i].outcome == mviewer::core::InPlaceRenameOutcome::Renamed)
            renamed.insert(fromUtf8(plan[i].sourcePath), fromUtf8(report.items[i].targetPath));
    }
    m_updating = true;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = m_list->topLevelItem(i);
        const QString path = item->data(0, kPathRole).toString();
        const auto it = renamed.constFind(path);
        if (it == renamed.constEnd())
            continue;
        item->setData(0, kPathRole, it.value());
        item->setText(0, QFileInfo(it.value()).fileName());
        item->setToolTip(0, QDir::toNativeSeparators(it.value()));
    }
    m_updating = false;
    refreshExistingNames();
    refreshPreview();
}

QString BatchDialog::summaryText(const mviewer::core::InPlaceRenameReport &report)
{
    QString text =
        report.renamed == 1 ? tr("Renamed 1 file") : tr("Renamed %1 files").arg(report.renamed);
    if (report.replaced == 1)
        text += tr(", 1 existing file moved to the Recycle Bin");
    else if (report.replaced > 1)
        text += tr(", %1 existing files moved to the Recycle Bin").arg(report.replaced);
    if (report.failed > 0)
        text += tr(", %1 failed").arg(report.failed);
    return text;
}
