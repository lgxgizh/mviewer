#include "mainwindow.h"
#include "mainwindow_p.h"

#include "ThemeMetrics.h"
#include "directorymonitor.h"
#include "formatfiltercombo.h"
#include "naturalnameorder.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

QWidget *MainWindow::buildSecondaryHeaderBar(QWidget *pane)
{
    auto *headerBar = new QWidget(pane);
    headerBar->setObjectName(QStringLiteral("secondaryHeaderBar"));
    auto *headerLayout = new QVBoxLayout(headerBar);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(mviewer::ui::metrics::kSpace4);

    auto *row1Widget = new QWidget(headerBar);
    auto *hLayout = new QHBoxLayout(row1Widget);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(4);

    // Row 1 is the address bar only, like the left pane's. Back / forward /
    // up for the right pane run through the main toolbar (and Alt+Left/Right,
    // Backspace, mouse back/forward) while the right pane is active.
    m_secondaryPathEdit = new SelectAllOnActivateLineEdit(row1Widget);
    m_secondaryPathEdit->setObjectName(QStringLiteral("secondaryPathEdit"));
    m_secondaryPathEdit->setPlaceholderText(tr("Enter secondary directory path..."));
    m_secondaryPathEdit->setClearButtonEnabled(true);
    connect(m_secondaryPathEdit, &QLineEdit::returnPressed, this,
            [this]()
            {
                const QString typed = m_secondaryPathEdit->text().trimmed();
                if (typed.isEmpty())
                    return;
                const QString text = QDir::fromNativeSeparators(typed);
                const QFileInfo info(text);
                if (QDir(text).exists() && info.exists() && info.isDir())
                {
                    openRightDirectory(QDir::cleanPath(text));
                    return;
                }
                if (statusBar())
                    statusBar()->showMessage(tr("Path does not exist: %1").arg(text), 5000);
                const QString current =
                    m_secondaryThumbnailPanel ? m_secondaryThumbnailPanel->currentDir() : QString();
                if (!current.isEmpty())
                    m_secondaryPathEdit->setText(QDir::toNativeSeparators(current));
            });
    hLayout->addWidget(m_secondaryPathEdit, 1);

    headerLayout->addWidget(row1Widget);
    QHBoxLayout *actionsLayout = buildSecondarySortControls(headerBar, headerLayout);
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
    actionsLayout->addWidget(m_secondaryBrowseBtn);

    m_secondaryCloneBtn = new QPushButton(tr("Clone Left"), headerBar);
    m_secondaryCloneBtn->setObjectName(QStringLiteral("secondaryCloneButton"));
    m_secondaryCloneBtn->setToolTip(tr("Sync right pane to the left pane directory"));
    connect(m_secondaryCloneBtn, &QPushButton::clicked, this, &MainWindow::cloneLeftToRightPane);
    actionsLayout->addWidget(m_secondaryCloneBtn);

    m_secondarySwapBtn = new QPushButton(tr("Swap"), headerBar);
    m_secondarySwapBtn->setObjectName(QStringLiteral("secondarySwapButton"));
    m_secondarySwapBtn->setToolTip(tr("Swap left and right pane directories and selections"));
    connect(m_secondarySwapBtn, &QPushButton::clicked, this, &MainWindow::swapBrowsePanes);
    actionsLayout->addWidget(m_secondarySwapBtn);

    m_secondaryCloseBtn = new QPushButton(QStringLiteral("×"), headerBar);
    m_secondaryCloseBtn->setObjectName(QStringLiteral("secondaryCloseButton"));
    m_secondaryCloseBtn->setToolTip(tr("Close Split Browse View"));
    m_secondaryCloseBtn->setAccessibleName(tr("Close Split Browse View"));
    m_secondaryCloseBtn->setFixedWidth(28);
    connect(m_secondaryCloseBtn, &QPushButton::clicked, this,
            [this]() { toggleSplitBrowse(false); });
    actionsLayout->addWidget(m_secondaryCloseBtn);

    polishGalleryToolbar(headerBar);
    return headerBar;
}

void MainWindow::wireSecondaryPanelSignals()
{
    connect(m_thumbnailPanel, &ThumbnailPanel::viewModeChanged, this,
            [this](ThumbnailPanel::ViewMode mode)
            {
                if (m_secondaryThumbnailPanel && m_splitBrowseEnabled &&
                    m_secondaryThumbnailPanel->viewMode() != mode)
                {
                    m_secondaryThumbnailPanel->setViewMode(mode);
                }
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::thumbSizeChanged, this,
            [this](int size)
            {
                if (m_secondaryThumbnailPanel && m_splitBrowseEnabled &&
                    m_secondaryThumbnailPanel->thumbSize() != size)
                {
                    m_secondaryThumbnailPanel->setThumbSize(size);
                }
            });
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::thumbSizeChanged, this,
            [this](int size)
            {
                if (m_thumbnailPanel && m_splitBrowseEnabled &&
                    m_thumbnailPanel->thumbSize() != size)
                {
                    m_thumbnailPanel->setThumbSize(size);
                }
            });

    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::sortChanged, this,
            [this](ThumbnailPanel::SortMode mode, bool ascending)
            {
                if (m_secondarySortCombo)
                {
                    const int idx = m_secondarySortCombo->findData(mode);
                    if (idx >= 0 && m_secondarySortCombo->currentIndex() != idx)
                    {
                        const QSignalBlocker b(m_secondarySortCombo);
                        m_secondarySortCombo->setCurrentIndex(idx);
                    }
                }
                if (m_secondarySortDirBtn)
                {
                    const bool descending = !ascending;
                    if (m_secondarySortDirBtn->isChecked() != descending)
                    {
                        const QSignalBlocker b(m_secondarySortDirBtn);
                        m_secondarySortDirBtn->setChecked(descending);
                        m_secondarySortDirBtn->setText(descending ? QStringLiteral("↓")
                                                                  : QStringLiteral("↑"));
                    }
                }
            });

    m_secondaryDirectoryMonitor = new DirectoryMonitor(this);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::directorySourceChanged,
            m_secondaryDirectoryMonitor, &DirectoryMonitor::setActiveDirectory);
    connect(m_secondaryThumbnailPanel, &ThumbnailPanel::directoryContentsChanged,
            m_secondaryDirectoryMonitor, &DirectoryMonitor::notifyDirectoryChanged);
    connect(m_secondaryDirectoryMonitor, &DirectoryMonitor::directoryDeltaReady,
            m_secondaryThumbnailPanel, &ThumbnailPanel::applyDirectoryDelta);
    wireSecondaryBrowseHostSignals();
}

QWidget *MainWindow::buildSecondaryBrowsePane()
{
    auto *pane = new QWidget();
    pane->setObjectName(QStringLiteral("secondaryBrowsePane"));
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *headerBar = buildSecondaryHeaderBar(pane);
    layout->addWidget(headerBar);

    m_secondaryThumbnailPanel = new ThumbnailPanel(pane);
    m_secondaryThumbnailPanel->setObjectName(QStringLiteral("secondaryThumbnailPanel"));
    m_secondaryThumbnailPanel->setConsumerId(1);
    m_secondaryThumbnailPanel->setPaneSlot(SelectionModel::PaneSlot::Secondary);
    m_secondaryThumbnailPanel->setCommandStack(&m_cmdStack);
    m_secondaryThumbnailPanel->setSelectionModel(m_selection);
    m_secondaryThumbnailPanel->setMinimumWidth(220);
    layout->addWidget(m_secondaryThumbnailPanel, 1);
    m_secondaryThumbnailPanel->setHiddenFormatGroups(m_hiddenFormatGroups);
    wireTypeHiddenHint(m_secondaryThumbnailPanel);
    if (m_secondaryTypeFilterCombo)
    {
        QWidget *row2 = m_secondaryTypeFilterCombo->parentWidget();
        dockCompareButton(m_secondaryThumbnailPanel, row2,
                          row2 ? qobject_cast<QHBoxLayout *>(row2->layout()) : nullptr,
                          m_secondaryTypeFilterCombo);
    }

    if (m_thumbnailPanel)
    {
        m_secondaryThumbnailPanel->setThumbSize(m_thumbnailPanel->thumbSize());
        m_secondaryThumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());
        m_secondaryThumbnailPanel->setSort(m_thumbnailPanel->sortMode(),
                                           m_thumbnailPanel->sortAscending());
    }

    wireSecondaryPanelSignals();
    // One filter on every widget in the pane, including ones added above.
    // A second install would deliver Back/Alt+Left twice.
    pane->installEventFilter(this);
    const auto watched = pane->findChildren<QWidget *>();
    for (QWidget *child : watched)
        child->installEventFilter(this);
    return pane;
}

QHBoxLayout *MainWindow::buildSecondarySortControls(QWidget *headerBar, QVBoxLayout *headerLayout)
{
    auto *row2Widget = new QWidget(headerBar);
    auto *row2Layout = new QHBoxLayout(row2Widget);
    row2Layout->setContentsMargins(8, 8, 8, 8);
    row2Layout->setSpacing(8);

    auto *sortLabel = new QLabel(tr("Sort:"), row2Widget);
    sortLabel->setObjectName(QStringLiteral("secondarySortLabel"));
    row2Layout->addWidget(sortLabel);

    m_secondarySortCombo = new QComboBox(row2Widget);
    m_secondarySortCombo->setObjectName(QStringLiteral("secondarySortCombo"));
    applySortComboItems(m_secondarySortCombo);
    row2Layout->addWidget(m_secondarySortCombo);

    m_secondarySortDirBtn = new QPushButton(QStringLiteral("↑"), row2Widget);
    m_secondarySortDirBtn->setObjectName(QStringLiteral("secondarySortDirBtn"));
    m_secondarySortDirBtn->setFixedWidth(28);
    m_secondarySortDirBtn->setCheckable(true);
    m_secondarySortDirBtn->setToolTip(sortDirectionToolTip(false));
    row2Layout->addWidget(m_secondarySortDirBtn);

    m_secondaryTypeFilterCombo = new FormatFilterCombo(row2Widget);
    m_secondaryTypeFilterCombo->setObjectName(QStringLiteral("secondaryTypeFilterCombo"));
    m_secondaryTypeFilterCombo->setProperty("pill", true);
    m_secondaryTypeFilterCombo->setHiddenGroups(m_hiddenFormatGroups);
    connect(m_secondaryTypeFilterCombo, &FormatFilterCombo::hiddenGroupsChanged, this,
            &MainWindow::setBrowseHiddenFormatGroups);
    row2Layout->addWidget(m_secondaryTypeFilterCombo);

    row2Layout->addStretch(1);
    headerLayout->addWidget(row2Widget);

    connect(m_secondarySortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (m_secondaryThumbnailPanel && m_secondarySortCombo)
                {
                    m_secondaryThumbnailPanel->setSortMode(static_cast<ThumbnailPanel::SortMode>(
                        m_secondarySortCombo->currentData().toInt()));
                }
            });

    connect(m_secondarySortDirBtn, &QPushButton::toggled, this,
            [this](bool descending)
            {
                if (m_secondarySortDirBtn)
                    m_secondarySortDirBtn->setText(descending ? QStringLiteral("↓")
                                                              : QStringLiteral("↑"));
                if (m_secondaryThumbnailPanel)
                    m_secondaryThumbnailPanel->setSortAscending(!descending);
            });
    return row2Layout;
}

void MainWindow::toggleSplitBrowse(bool enabled)
{
    if (m_splitBrowseEnabled == enabled && m_secondaryBrowsePane &&
        m_secondaryBrowsePane->isVisibleTo(this) == enabled)
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
        enableSplitBrowseUi();
    else
        disableSplitBrowseUi();
    updateSplitBrowseCompareButton();
    syncOtherPaneDirectories();
}

void MainWindow::openRightDirectory(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString cleaned = QDir::cleanPath(dir);
    if (!m_splitBrowseEnabled)
    {
        // The right pane opens `dir` below; do not load the left folder first.
        m_splitOpeningTarget = true;
        toggleSplitBrowse(true);
        m_splitOpeningTarget = false;
    }

    if (m_secondaryThumbnailPanel)
        m_secondaryThumbnailPanel->setDirectory(cleaned);
    if (m_secondaryPathEdit)
        m_secondaryPathEdit->setText(cleaned);

    m_appState.secondaryLastDir = cleaned;
    m_secondaryBrowseHistory.push(cleaned);
    setActiveBrowsePane(SelectionModel::PaneSlot::Secondary);
    updateSwapButtonEnabled();
    updateSecondaryNavButtons();
    // setDirectory clears the filename query. Copy the shared filters back,
    // including when this pane was already the active one.
    if (m_secondaryThumbnailPanel)
        reapplySharedBrowseFilters(m_secondaryThumbnailPanel);
    syncActiveBrowserTitle();
}

void MainWindow::swapBrowsePanes()
{
    if (!m_thumbnailPanel || !m_secondaryThumbnailPanel)
        return;
    if (m_thumbnailPanel->currentDir().isEmpty() ||
        m_secondaryThumbnailPanel->currentDir().isEmpty())
        return;

    const QString leftDir = m_thumbnailPanel->currentDir();
    const QString rightDir = m_secondaryThumbnailPanel->currentDir();
    const auto leftSort = m_thumbnailPanel->sortMode();
    const bool leftAsc = m_thumbnailPanel->sortAscending();
    const auto rightSort = m_secondaryThumbnailPanel->sortMode();
    const bool rightAsc = m_secondaryThumbnailPanel->sortAscending();
    const QStringList leftSel =
        m_selection ? m_selection->paneSelection(SelectionModel::PaneSlot::Primary) : QStringList();
    const QStringList rightSel =
        m_selection ? m_selection->paneSelection(SelectionModel::PaneSlot::Secondary)
                    : QStringList();

    if (!rightDir.isEmpty())
    {
        changeDirectory(rightDir);
        m_thumbnailPanel->setSort(rightSort, rightAsc);
        if (m_selection)
            m_selection->setPaneSelection(SelectionModel::PaneSlot::Primary, rightSel);
    }
    if (!leftDir.isEmpty())
    {
        m_secondaryThumbnailPanel->setDirectory(leftDir);
        m_secondaryThumbnailPanel->setSort(leftSort, leftAsc);
        if (m_secondaryPathEdit)
            m_secondaryPathEdit->setText(leftDir);
        m_appState.secondaryLastDir = leftDir;
        m_secondaryBrowseHistory.push(QDir::cleanPath(leftDir));
        if (m_selection)
            m_selection->setPaneSelection(SelectionModel::PaneSlot::Secondary, leftSel);
        reapplySharedBrowseFilters(m_secondaryThumbnailPanel);
    }
    updateSecondaryNavButtons();
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
    updateActivePaneIndicator();
    updateNavigationActions();
    syncActiveBrowserTitle();
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
    // Each pane's Compare button counts the cross-pane union, so a selection
    // change in one pane must refresh the other pane's button too.
    for (ThumbnailPanel *panel : {m_thumbnailPanel, m_secondaryThumbnailPanel})
    {
        if (panel)
            panel->refreshCompareButton();
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
