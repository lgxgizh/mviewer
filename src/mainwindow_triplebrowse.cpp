#include "mainwindow_p.h"
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace
{
// Keep directory actions reachable without forcing three wide one-line headers.
void compactPaneHeader(QComboBox *sort, const QList<QPushButton *> &buttons, bool compact)
{
    QWidget *normalRow = sort ? sort->parentWidget() : nullptr;
    QWidget *header = normalRow ? normalRow->parentWidget() : nullptr;
    auto *root = header ? qobject_cast<QVBoxLayout *>(header->layout()) : nullptr;
    auto *normal = normalRow ? qobject_cast<QHBoxLayout *>(normalRow->layout()) : nullptr;
    if (!root || !normal)
        return;
    auto *row = header->findChild<QWidget *>(QStringLiteral("compactBrowseActions"));
    if (!row && compact)
    {
        row = new QWidget(header);
        row->setObjectName(QStringLiteral("compactBrowseActions"));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 0, 8, 8);
        layout->setSpacing(8);
        layout->addStretch(1);
        root->addWidget(row);
    }
    if (!row)
        return;
    auto *target = compact ? qobject_cast<QHBoxLayout *>(row->layout()) : normal;
    for (QPushButton *button : buttons)
        if (button)
            target->addWidget(button);
    row->setVisible(compact);
}
} // namespace

void MainWindow::openThirdDirectory(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString cleaned = QDir::cleanPath(dir);
    if (!m_tripleBrowseEnabled)
    {
        // The right pane opens `dir` below; do not load the left folder first.
        m_tripleOpeningTarget = true;
        toggleTripleBrowse(true);
        m_tripleOpeningTarget = false;
    }

    if (m_tertiaryThumbnailPanel)
        m_tertiaryThumbnailPanel->setDirectory(cleaned);
    if (m_tertiaryPathEdit)
        m_tertiaryPathEdit->setText(cleaned);

    m_appState.tertiaryLastDir = cleaned;
    m_tertiaryBrowseHistory.push(cleaned);
    setActiveBrowsePane(SelectionModel::PaneSlot::Tertiary);
    updateSwapButtonEnabled();
    updateTertiaryNavButtons();
    syncActiveBrowserTitle();
}

void MainWindow::swapThirdBrowsePane()
{
    if (!m_thumbnailPanel || !m_tertiaryThumbnailPanel)
        return;
    if (m_thumbnailPanel->currentDir().isEmpty() ||
        m_tertiaryThumbnailPanel->currentDir().isEmpty())
        return;

    const QString leftDir = m_thumbnailPanel->currentDir();
    const QString rightDir = m_tertiaryThumbnailPanel->currentDir();
    const auto leftSort = m_thumbnailPanel->sortMode();
    const bool leftAsc = m_thumbnailPanel->sortAscending();
    const auto rightSort = m_tertiaryThumbnailPanel->sortMode();
    const bool rightAsc = m_tertiaryThumbnailPanel->sortAscending();
    const QStringList leftSel =
        m_selection ? m_selection->paneSelection(SelectionModel::PaneSlot::Primary) : QStringList();
    const QStringList rightSel =
        m_selection ? m_selection->paneSelection(SelectionModel::PaneSlot::Tertiary)
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
        m_tertiaryThumbnailPanel->setDirectory(leftDir);
        m_tertiaryThumbnailPanel->setSort(leftSort, leftAsc);
        if (m_tertiaryPathEdit)
            m_tertiaryPathEdit->setText(leftDir);
        m_appState.tertiaryLastDir = leftDir;
        m_tertiaryBrowseHistory.push(QDir::cleanPath(leftDir));
        if (m_selection)
            m_selection->setPaneSelection(SelectionModel::PaneSlot::Tertiary, leftSel);
    }
    updateTertiaryNavButtons();
    updateSplitBrowseCompareButton();
}

void MainWindow::cloneLeftToThirdPane()
{
    if (!m_thumbnailPanel)
        return;
    const QString leftDir = m_thumbnailPanel->currentDir();
    if (!leftDir.isEmpty())
        openThirdDirectory(leftDir);
}

void MainWindow::toggleTripleBrowse(bool enabled)
{
    setBrowseColumnCount(enabled ? 3 : 2);
}

void MainWindow::setBrowseColumnCount(int count)
{
    if (count < 1 || count > 3 || count == browseColumnCount())
        return;
    const bool wasSplit = m_splitBrowseEnabled;
    const bool wasTriple = m_tripleBrowseEnabled;
    m_splitBrowseEnabled = count >= 2;
    m_tripleBrowseEnabled = count == 3;
    compactPaneHeader(
        m_secondarySortCombo,
        {m_secondaryBrowseBtn, m_secondaryCloneBtn, m_secondarySwapBtn, m_secondaryCloseBtn},
        m_tripleBrowseEnabled);
    compactPaneHeader(
        m_tertiarySortCombo,
        {m_tertiaryBrowseBtn, m_tertiaryCloneBtn, m_tertiarySwapBtn, m_tertiaryCloseBtn},
        m_tripleBrowseEnabled);
    if (m_actSplitBrowse)
    {
        const QSignalBlocker blocker(m_actSplitBrowse);
        m_actSplitBrowse->setChecked(count == 2);
    }
    if (m_actTripleBrowse)
    {
        const QSignalBlocker blocker(m_actTripleBrowse);
        m_actTripleBrowse->setChecked(count == 3);
    }
    if (m_selection)
        m_selection->setBrowsePaneCount(count);
    if (!m_splitBrowseEnabled)
        disableSplitBrowseUi();
    else if (!wasSplit)
        enableSplitBrowseUi();

    if (m_tripleBrowseEnabled && !wasTriple && m_tertiaryThumbnailPanel)
    {
        m_tertiaryBrowsePane->show();
        m_tertiaryThumbnailPanel->setThumbSize(m_thumbnailPanel->thumbSize());
        m_tertiaryThumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());
        if (m_splitRestoringSession)
        {
            applyTertiarySortFromState();
            const QString target = m_appState.tertiaryLastDir;
            if (!target.isEmpty())
                openThirdDirectory(target);
        }
        else
        {
            m_tertiaryThumbnailPanel->setSort(m_thumbnailPanel->sortMode(),
                                              m_thumbnailPanel->sortAscending());
            if (!m_tripleOpeningTarget && !m_thumbnailPanel->currentDir().isEmpty())
                mirrorPrimaryIntoTertiary();
        }
    }
    else if (!m_tripleBrowseEnabled && m_tertiaryBrowsePane)
    {
        m_pendingTertiaryScrollRestore = false;
        m_tertiaryBrowsePane->hide();
        if (m_selection && m_splitBrowseEnabled)
            m_selection->clearPane(SelectionModel::PaneSlot::Tertiary);
        m_tertiaryThumbnailPanel->selectPaths({}, {}, false);
        if (m_activeBrowsePane == SelectionModel::PaneSlot::Tertiary)
            setActiveBrowsePane(SelectionModel::PaneSlot::Primary);
    }
    retranslateSplitHeader();
    applyGallerySplitterSizes();
    updateActivePaneIndicator();
    updateSplitBrowseCompareButton();
    applyActivePaneStatusBar();
    syncOtherPaneDirectories();
}
