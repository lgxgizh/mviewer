#include "mainwindow.h"
#include "mainwindow_p.h"

#include "ThemeMetrics.h"
#include "directorymonitor.h"
#include "naturalnameorder.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

QWidget *MainWindow::buildSecondaryBrowsePane()
{
    auto *pane = new QWidget(m_gallerySplitter);
    pane->setObjectName(QStringLiteral("secondaryBrowsePane"));
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *headerBar = new QWidget(pane);
    headerBar->setObjectName(QStringLiteral("secondaryHeaderBar"));
    headerBar->setFixedHeight(mviewer::ui::metrics::kControlHeight);
    auto *hLayout = new QHBoxLayout(headerBar);
    hLayout->setContentsMargins(4, 2, 4, 2);
    hLayout->setSpacing(4);

    m_secondaryPathEdit = new SelectAllOnActivateLineEdit(headerBar);
    m_secondaryPathEdit->setObjectName(QStringLiteral("secondaryPathEdit"));
    m_secondaryPathEdit->setPlaceholderText(tr("Enter secondary directory path..."));
    m_secondaryPathEdit->setClearButtonEnabled(true);
    connect(m_secondaryPathEdit, &QLineEdit::returnPressed, this,
            [this]()
            {
                const QString p = m_secondaryPathEdit->text().trimmed();
                if (!p.isEmpty())
                    openRightDirectory(p);
            });
    hLayout->addWidget(m_secondaryPathEdit, 1);

    m_secondaryBrowseBtn = new QPushButton(tr("Browse..."), headerBar);
    m_secondaryBrowseBtn->setObjectName(QStringLiteral("secondaryBrowseButton"));
    connect(m_secondaryBrowseBtn, &QPushButton::clicked, this,
            [this]()
            {
                const QString current =
                    m_secondaryThumbnailPanel ? m_secondaryThumbnailPanel->currentDir() : QString();
                const QString dir = QFileDialog::getExistingDirectory(
                    this, tr("Open Secondary Directory"), current);
                if (!dir.isEmpty())
                    openRightDirectory(dir);
            });
    hLayout->addWidget(m_secondaryBrowseBtn);

    m_secondaryCloneBtn = new QPushButton(tr("Clone Left"), headerBar);
    m_secondaryCloneBtn->setObjectName(QStringLiteral("secondaryCloneButton"));
    m_secondaryCloneBtn->setToolTip(tr("Sync right pane to the left pane directory"));
    connect(m_secondaryCloneBtn, &QPushButton::clicked, this, &MainWindow::cloneLeftToRightPane);
    hLayout->addWidget(m_secondaryCloneBtn);

    m_secondarySwapBtn = new QPushButton(tr("Swap"), headerBar);
    m_secondarySwapBtn->setObjectName(QStringLiteral("secondarySwapButton"));
    m_secondarySwapBtn->setToolTip(tr("Swap left and right pane directories and selections"));
    connect(m_secondarySwapBtn, &QPushButton::clicked, this, &MainWindow::swapBrowsePanes);
    hLayout->addWidget(m_secondarySwapBtn);

    m_secondaryCloseBtn = new QPushButton(QStringLiteral("X"), headerBar);
    m_secondaryCloseBtn->setObjectName(QStringLiteral("secondaryCloseButton"));
    m_secondaryCloseBtn->setToolTip(tr("Close split browse view"));
    m_secondaryCloseBtn->setFixedWidth(28);
    connect(m_secondaryCloseBtn, &QPushButton::clicked, this,
            [this]() { toggleSplitBrowse(false); });
    hLayout->addWidget(m_secondaryCloseBtn);

    layout->addWidget(headerBar);

    m_secondaryThumbnailPanel = new ThumbnailPanel(pane);
    m_secondaryThumbnailPanel->setObjectName(QStringLiteral("secondaryThumbnailPanel"));
    m_secondaryThumbnailPanel->setConsumerId(1);
    m_secondaryThumbnailPanel->setPaneSlot(SelectionModel::PaneSlot::Secondary);
    m_secondaryThumbnailPanel->setCommandStack(&m_cmdStack);
    m_secondaryThumbnailPanel->setSelectionModel(m_selection);
    m_secondaryThumbnailPanel->installEventFilter(this);
    if (m_secondaryThumbnailPanel->viewport())
        m_secondaryThumbnailPanel->viewport()->installEventFilter(this);
    layout->addWidget(m_secondaryThumbnailPanel, 1);

    m_secondaryDirectoryMonitor = new DirectoryMonitor(this);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::directorySourceChanged,
            m_secondaryDirectoryMonitor, &DirectoryMonitor::setActiveDirectory);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::directoryContentsChanged,
            m_secondaryDirectoryMonitor, &DirectoryMonitor::notifyDirectoryChanged);
    connect(m_secondaryDirectoryMonitor, &DirectoryMonitor::directoryDeltaReady,
            m_secondaryThumbnailPanel, &ThumbnailPanel::applyDirectoryDelta);

    return pane;
}

void MainWindow::toggleSplitBrowse(bool enabled)
{
    if (m_splitBrowseEnabled == enabled && m_secondaryBrowsePane &&
        m_secondaryBrowsePane->isVisible() == enabled)
        return;

    m_splitBrowseEnabled = enabled;
    if (m_actSplitBrowse && m_actSplitBrowse->isChecked() != enabled)
    {
        const QSignalBlocker blocker(m_actSplitBrowse);
        m_actSplitBrowse->setChecked(enabled);
    }

    if (m_selection)
        m_selection->setSplitModeEnabled(enabled);

    if (enabled)
    {
        if (m_secondaryBrowsePane)
        {
            m_secondaryBrowsePane->show();
            if (m_gallerySplitter)
            {
                if (!m_appState.gallerySplitterSizes.isEmpty() &&
                    m_appState.gallerySplitterSizes.size() >= 2 &&
                    m_appState.gallerySplitterSizes[0] > 0 &&
                    m_appState.gallerySplitterSizes[1] > 0)
                {
                    m_gallerySplitter->setSizes(m_appState.gallerySplitterSizes);
                }
                else
                {
                    const int w = m_gallerySplitter->width();
                    const int half = w > 0 ? w / 2 : 400;
                    m_gallerySplitter->setSizes({half, half});
                }
            }
        }
        if (m_secondaryThumbnailPanel && m_secondaryThumbnailPanel->currentDir().isEmpty())
        {
            QString targetDir = m_appState.secondaryLastDir;
            if (targetDir.isEmpty() || !QDir(targetDir).exists())
            {
                if (m_thumbnailPanel && !m_thumbnailPanel->currentDir().isEmpty())
                    targetDir = m_thumbnailPanel->currentDir();
            }
            if (!targetDir.isEmpty() && QDir(targetDir).exists())
                openRightDirectory(targetDir);
        }
    }
    else
    {
        if (m_secondaryBrowsePane)
            m_secondaryBrowsePane->hide();
        setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    }
    updateSplitBrowseCompareButton();
}

void MainWindow::openRightDirectory(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString cleaned = QDir::cleanPath(dir);
    if (!m_splitBrowseEnabled)
        toggleSplitBrowse(true);

    if (m_secondaryThumbnailPanel)
        m_secondaryThumbnailPanel->setDirectory(cleaned);
    if (m_secondaryPathEdit)
        m_secondaryPathEdit->setText(cleaned);
    if (m_secondaryDirectoryMonitor)
        m_secondaryDirectoryMonitor->setActiveDirectory(cleaned);

    m_appState.secondaryLastDir = cleaned;
    setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
}

void MainWindow::swapBrowsePanes()
{
    if (!m_thumbnailPanel || !m_secondaryThumbnailPanel)
        return;

    const QString leftDir = m_thumbnailPanel->currentDir();
    const QString rightDir = m_secondaryThumbnailPanel->currentDir();
    const QStringList leftSel =
        m_selection ? m_selection->paneSelection(SelectionModel::PaneSlot::Primary) : QStringList();
    const QStringList rightSel =
        m_selection ? m_selection->paneSelection(SelectionModel::PaneSlot::Secondary)
                    : QStringList();

    if (!rightDir.isEmpty())
    {
        changeDirectory(rightDir);
        if (m_selection)
            m_selection->setPaneSelection(SelectionModel::PaneSlot::Primary, rightSel);
    }
    if (!leftDir.isEmpty())
    {
        m_secondaryThumbnailPanel->setDirectory(leftDir);
        if (m_secondaryPathEdit)
            m_secondaryPathEdit->setText(leftDir);
        if (m_secondaryDirectoryMonitor)
            m_secondaryDirectoryMonitor->setActiveDirectory(leftDir);
        m_appState.secondaryLastDir = leftDir;
        if (m_selection)
            m_selection->setPaneSelection(SelectionModel::PaneSlot::Secondary, leftSel);
    }
    updateSplitBrowseCompareButton();
}

void MainWindow::cloneLeftToRightPane()
{
    if (!m_thumbnailPanel)
        return;
    const QString leftDir = m_thumbnailPanel->currentDir();
    if (!leftDir.isEmpty())
        openRightDirectory(leftDir);
}

void MainWindow::setActiveBrowsePane(SelectionModel::PaneSlot pane)
{
    if (m_activeBrowsePane == pane)
        return;
    m_activeBrowsePane = pane;
    if (m_selection)
        m_selection->setActivePane(pane);
}

void MainWindow::updateSplitBrowseCompareButton()
{
    const int count = m_selection ? m_selection->selection().size() : 0;
    const bool canCompare = (count >= 2 && count <= 8);
    if (m_actCompare)
    {
        m_actCompare->setEnabled(canCompare);
        if (count > 8)
        {
            m_actCompare->setToolTip(tr("Select 2 to 8 images across left and right panes to "
                                        "compare (currently %1 selected)")
                                         .arg(count));
        }
        else if (count < 2)
        {
            m_actCompare->setToolTip(
                tr("Select 2 to 8 images across left and right panes to compare"));
        }
        else
        {
            m_actCompare->setToolTip(tr("Compare %1 selected images (P)").arg(count));
        }
    }
}

QStringList MainWindow::sortPathsForCompare(const QStringList &paths) const
{
    if (paths.size() <= 1)
        return paths;
    if (!m_splitBrowseEnabled || !m_selection)
        return sortPathsByNaturalFileName(paths);

    const QStringList &primarySel = m_selection->paneSelection(SelectionModel::PaneSlot::Primary);
    const QStringList &secondarySel =
        m_selection->paneSelection(SelectionModel::PaneSlot::Secondary);

    bool hasPrimary = false;
    bool hasSecondary = false;
    for (const QString &p : paths)
    {
        if (primarySel.contains(p))
            hasPrimary = true;
        if (secondarySel.contains(p))
            hasSecondary = true;
    }
    if (!hasPrimary || !hasSecondary)
        return sortPathsByNaturalFileName(paths);

    struct ItemKey
    {
        QString path;
        QString fileName;
        int paneRank = 0;
    };
    std::vector<ItemKey> items;
    items.reserve(static_cast<size_t>(paths.size()));
    for (const QString &p : paths)
    {
        ItemKey k;
        k.path = p;
        k.fileName = QFileInfo(p).fileName();
        k.paneRank = primarySel.contains(p) ? 0 : 1;
        items.push_back(std::move(k));
    }

    std::sort(items.begin(), items.end(),
              [](const ItemKey &a, const ItemKey &b)
              {
                  const int cmp = compareNaturalName(a.fileName, b.fileName);
                  if (cmp != 0)
                      return cmp < 0;
                  if (a.paneRank != b.paneRank)
                      return a.paneRank < b.paneRank;
                  return a.path < b.path;
              });

    QStringList result;
    result.reserve(paths.size());
    for (const auto &item : items)
        result.append(item.path);
    return result;
}
