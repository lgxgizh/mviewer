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

QWidget *MainWindow::buildTertiaryHeaderBar(QWidget *pane)
{
    auto *headerBar = new QWidget(pane);
    headerBar->setObjectName(QStringLiteral("tertiaryHeaderBar"));
    auto *headerLayout = new QVBoxLayout(headerBar);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(mviewer::ui::metrics::kSpace4);

    auto *row1Widget = new QWidget(headerBar);
    auto *hLayout = new QHBoxLayout(row1Widget);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(4);

    // Row 1 is the address bar only, like the left pane's. Back / forward /
    // up for the third pane run through the main toolbar (and Alt+Left/Right,
    // Backspace, mouse back/forward) while the third pane is active.
    m_tertiaryPathEdit = new SelectAllOnActivateLineEdit(row1Widget);
    m_tertiaryPathEdit->setObjectName(QStringLiteral("tertiaryPathEdit"));
    m_tertiaryPathEdit->setPlaceholderText(tr("Enter tertiary directory path..."));
    m_tertiaryPathEdit->setClearButtonEnabled(true);
    connect(m_tertiaryPathEdit, &QLineEdit::returnPressed, this,
            [this]()
            {
                const QString typed = m_tertiaryPathEdit->text().trimmed();
                if (typed.isEmpty())
                    return;
                const QString text = QDir::fromNativeSeparators(typed);
                const QFileInfo info(text);
                if (QDir(text).exists() && info.exists() && info.isDir())
                {
                    openThirdDirectory(QDir::cleanPath(text));
                    return;
                }
                if (statusBar())
                    statusBar()->showMessage(tr("Path does not exist: %1").arg(text), 5000);
                const QString current =
                    m_tertiaryThumbnailPanel ? m_tertiaryThumbnailPanel->currentDir() : QString();
                if (!current.isEmpty())
                    m_tertiaryPathEdit->setText(QDir::toNativeSeparators(current));
            });
    hLayout->addWidget(m_tertiaryPathEdit, 1);

    headerLayout->addWidget(row1Widget);
    QHBoxLayout *actionsLayout = buildTertiarySortControls(headerBar, headerLayout);
    m_tertiaryBrowseBtn = new QPushButton(tr("Browse..."), headerBar);
    m_tertiaryBrowseBtn->setObjectName(QStringLiteral("tertiaryBrowseButton"));
    connect(m_tertiaryBrowseBtn, &QPushButton::clicked, this,
            [this]()
            {
                const QString current =
                    m_tertiaryThumbnailPanel ? m_tertiaryThumbnailPanel->currentDir() : QString();
                const QString dir =
                    QFileDialog::getExistingDirectory(this, tr("Open Tertiary Directory"), current);
                if (!dir.isEmpty())
                    openThirdDirectory(dir);
            });
    actionsLayout->addWidget(m_tertiaryBrowseBtn);

    m_tertiaryCloneBtn = new QPushButton(tr("Clone Left"), headerBar);
    m_tertiaryCloneBtn->setObjectName(QStringLiteral("tertiaryCloneButton"));
    m_tertiaryCloneBtn->setToolTip(tr("Sync third pane to the left pane directory"));
    connect(m_tertiaryCloneBtn, &QPushButton::clicked, this, &MainWindow::cloneLeftToThirdPane);
    actionsLayout->addWidget(m_tertiaryCloneBtn);

    m_tertiarySwapBtn = new QPushButton(tr("Swap"), headerBar);
    m_tertiarySwapBtn->setObjectName(QStringLiteral("tertiarySwapButton"));
    m_tertiarySwapBtn->setToolTip(tr("Swap left and third pane directories and selections"));
    connect(m_tertiarySwapBtn, &QPushButton::clicked, this, &MainWindow::swapThirdBrowsePane);
    actionsLayout->addWidget(m_tertiarySwapBtn);

    m_tertiaryCloseBtn = new QPushButton(QStringLiteral("×"), headerBar);
    m_tertiaryCloseBtn->setObjectName(QStringLiteral("tertiaryCloseButton"));
    m_tertiaryCloseBtn->setToolTip(tr("Close Triple Browse View"));
    m_tertiaryCloseBtn->setAccessibleName(tr("Close Triple Browse View"));
    m_tertiaryCloseBtn->setFixedWidth(28);
    connect(m_tertiaryCloseBtn, &QPushButton::clicked, this,
            [this]() { toggleTripleBrowse(false); });
    actionsLayout->addWidget(m_tertiaryCloseBtn);

    polishGalleryToolbar(headerBar);
    return headerBar;
}

void MainWindow::wireTertiaryPanelSignals()
{
    connect(m_thumbnailPanel, &ThumbnailPanel::viewModeChanged, this,
            [this](ThumbnailPanel::ViewMode mode)
            {
                if (m_tertiaryThumbnailPanel && m_tripleBrowseEnabled &&
                    m_tertiaryThumbnailPanel->viewMode() != mode)
                {
                    m_tertiaryThumbnailPanel->setViewMode(mode);
                }
            });
    connect(m_thumbnailPanel, &ThumbnailPanel::thumbSizeChanged, this,
            [this](int size)
            {
                if (m_tertiaryThumbnailPanel && m_tripleBrowseEnabled &&
                    m_tertiaryThumbnailPanel->thumbSize() != size)
                {
                    m_tertiaryThumbnailPanel->setThumbSize(size);
                }
            });
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::thumbSizeChanged, this,
            [this](int size)
            {
                if (m_thumbnailPanel && m_tripleBrowseEnabled &&
                    m_thumbnailPanel->thumbSize() != size)
                {
                    m_thumbnailPanel->setThumbSize(size);
                }
            });

    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::sortChanged, this,
            [this](ThumbnailPanel::SortMode mode, bool ascending)
            {
                if (m_tertiarySortCombo)
                {
                    const int idx = m_tertiarySortCombo->findData(mode);
                    if (idx >= 0 && m_tertiarySortCombo->currentIndex() != idx)
                    {
                        const QSignalBlocker b(m_tertiarySortCombo);
                        m_tertiarySortCombo->setCurrentIndex(idx);
                    }
                }
                if (m_tertiarySortDirBtn)
                {
                    const bool descending = !ascending;
                    if (m_tertiarySortDirBtn->isChecked() != descending)
                    {
                        const QSignalBlocker b(m_tertiarySortDirBtn);
                        m_tertiarySortDirBtn->setChecked(descending);
                        m_tertiarySortDirBtn->setText(descending ? QStringLiteral("↓")
                                                                 : QStringLiteral("↑"));
                    }
                }
            });

    m_tertiaryDirectoryMonitor = new DirectoryMonitor(this);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::directorySourceChanged,
            m_tertiaryDirectoryMonitor, &DirectoryMonitor::setActiveDirectory);
    connect(m_tertiaryThumbnailPanel, &ThumbnailPanel::directoryContentsChanged,
            m_tertiaryDirectoryMonitor, &DirectoryMonitor::notifyDirectoryChanged);
    connect(m_tertiaryDirectoryMonitor, &DirectoryMonitor::directoryDeltaReady,
            m_tertiaryThumbnailPanel, &ThumbnailPanel::applyDirectoryDelta);
    wireTertiaryBrowseHostSignals();
}

QWidget *MainWindow::buildTertiaryBrowsePane()
{
    auto *pane = new QWidget();
    pane->setObjectName(QStringLiteral("tertiaryBrowsePane"));
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *headerBar = buildTertiaryHeaderBar(pane);
    layout->addWidget(headerBar);

    m_tertiaryThumbnailPanel = new ThumbnailPanel(pane);
    m_tertiaryThumbnailPanel->setObjectName(QStringLiteral("tertiaryThumbnailPanel"));
    m_tertiaryThumbnailPanel->setConsumerId(2);
    m_tertiaryThumbnailPanel->setPaneSlot(SelectionModel::PaneSlot::Tertiary);
    m_tertiaryThumbnailPanel->setCommandStack(&m_cmdStack);
    m_tertiaryThumbnailPanel->setSelectionModel(m_selection);
    m_tertiaryThumbnailPanel->setMinimumWidth(220);
    layout->addWidget(m_tertiaryThumbnailPanel, 1);
    m_tertiaryThumbnailPanel->setHiddenFormatGroups(m_hiddenFormatGroups);
    wireTypeHiddenHint(m_tertiaryThumbnailPanel);
    if (m_tertiaryTypeFilterCombo)
    {
        QWidget *row2 = m_tertiaryTypeFilterCombo->parentWidget();
        dockCompareButton(m_tertiaryThumbnailPanel, row2,
                          row2 ? qobject_cast<QHBoxLayout *>(row2->layout()) : nullptr,
                          m_tertiaryTypeFilterCombo);
    }

    if (m_thumbnailPanel)
    {
        m_tertiaryThumbnailPanel->setThumbSize(m_thumbnailPanel->thumbSize());
        m_tertiaryThumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());
        m_tertiaryThumbnailPanel->setSort(m_thumbnailPanel->sortMode(),
                                          m_thumbnailPanel->sortAscending());
    }

    wireTertiaryPanelSignals();
    // One filter on every widget in the pane, including ones added above.
    // A second install would deliver Back/Alt+Left twice.
    pane->installEventFilter(this);
    const auto watched = pane->findChildren<QWidget *>();
    for (QWidget *child : watched)
        child->installEventFilter(this);
    return pane;
}

QHBoxLayout *MainWindow::buildTertiarySortControls(QWidget *headerBar, QVBoxLayout *headerLayout)
{
    auto *row2Widget = new QWidget(headerBar);
    auto *row2Layout = new QHBoxLayout(row2Widget);
    row2Layout->setContentsMargins(8, 8, 8, 8);
    row2Layout->setSpacing(8);

    auto *sortLabel = new QLabel(tr("Sort:"), row2Widget);
    sortLabel->setObjectName(QStringLiteral("tertiarySortLabel"));
    row2Layout->addWidget(sortLabel);

    m_tertiarySortCombo = new QComboBox(row2Widget);
    m_tertiarySortCombo->setObjectName(QStringLiteral("tertiarySortCombo"));
    applySortComboItems(m_tertiarySortCombo);
    row2Layout->addWidget(m_tertiarySortCombo);

    m_tertiarySortDirBtn = new QPushButton(QStringLiteral("↑"), row2Widget);
    m_tertiarySortDirBtn->setObjectName(QStringLiteral("tertiarySortDirBtn"));
    m_tertiarySortDirBtn->setFixedWidth(28);
    m_tertiarySortDirBtn->setCheckable(true);
    m_tertiarySortDirBtn->setToolTip(sortDirectionToolTip(false));
    row2Layout->addWidget(m_tertiarySortDirBtn);

    m_tertiaryTypeFilterCombo = new FormatFilterCombo(row2Widget);
    m_tertiaryTypeFilterCombo->setObjectName(QStringLiteral("tertiaryTypeFilterCombo"));
    m_tertiaryTypeFilterCombo->setProperty("pill", true);
    m_tertiaryTypeFilterCombo->setHiddenGroups(m_hiddenFormatGroups);
    connect(m_tertiaryTypeFilterCombo, &FormatFilterCombo::hiddenGroupsChanged, this,
            &MainWindow::setBrowseHiddenFormatGroups);
    row2Layout->addWidget(m_tertiaryTypeFilterCombo);

    row2Layout->addStretch(1);
    headerLayout->addWidget(row2Widget);

    connect(m_tertiarySortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (m_tertiaryThumbnailPanel && m_tertiarySortCombo)
                {
                    m_tertiaryThumbnailPanel->setSortMode(static_cast<ThumbnailPanel::SortMode>(
                        m_tertiarySortCombo->currentData().toInt()));
                }
            });

    connect(m_tertiarySortDirBtn, &QPushButton::toggled, this,
            [this](bool descending)
            {
                if (m_tertiarySortDirBtn)
                    m_tertiarySortDirBtn->setText(descending ? QStringLiteral("↓")
                                                             : QStringLiteral("↑"));
                if (m_tertiaryThumbnailPanel)
                    m_tertiaryThumbnailPanel->setSortAscending(!descending);
            });
    return row2Layout;
}
