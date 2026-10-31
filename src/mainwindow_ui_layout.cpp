// MainWindow layout construction and command surfaces.
#include "ThemeMetrics.h"
#include "mainwindow_p.h"

#include "core/render/ZoomPercent.h"

#include <QFrame>
#include <QTimer>

namespace
{

// SelectAllOnActivateLineEdit is defined in mainwindow_p.h

QLabel *sectionHeaderLabel(QWidget *parent, const char *objectName, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setProperty("sectionHeader", true);
    label->setProperty("role", QStringLiteral("sectionHeader"));
    label->setFixedHeight(mviewer::ui::metrics::kControlHeight);
    return label;
}
} // namespace

void MainWindow::buildBrowserShell()
{
    m_actDirUp = new QAction(tr("Up one level"), this);
    m_actDirUp->setObjectName("directoryUpAction");
    m_actDirUp->setShortcut(QKeySequence("Alt+Up"));
    m_actRefresh = new QAction(tr("Refresh"), this);
    m_actRefresh->setObjectName("refreshDirectoryAction");
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    m_actOpenCurrentFolder = new QAction(tr("Open Folder"), this);
    m_actOpenCurrentFolder->setObjectName("openCurrentFolderAction");
    buildUnifiedToolbar();

    // ----- Breadcrumb navigation bar (M15 Product Shell P0) -----
    m_breadcrumb = new BreadcrumbBar(this);
    // Keep the signal path for breadcrumb navigation, but do not spend a full
    // row duplicating the editable path field in the default browser shell.
    m_breadcrumb->setObjectName("breadcrumbBar");
    m_breadcrumb->hide();

    // ----- Path input bar (UX: type a path to jump to a directory) -----
    m_pathEdit = new SelectAllOnActivateLineEdit(this);
    m_pathEdit->setObjectName("pathEdit");
    m_pathEdit->setPlaceholderText(tr("Enter a directory path and press Enter to switch..."));
    m_pathEdit->setToolTip(
        tr("Type or paste a directory path and press Enter to open it (shortcut: "
           "Ctrl+F / Alt+D to focus)."));
    m_pathEdit->setClearButtonEnabled(true);
    auto *actFocusPath = new QAction(this);
    actFocusPath->setObjectName("focusPathAction");
    actFocusPath->setShortcuts({QKeySequence("Alt+D")});
    connect(actFocusPath, &QAction::triggered, this, [this]() { focusAddressBar(); });
    addAction(actFocusPath);
}

QWidget *MainWindow::buildNavigationPanel()
{
    // ----- Left column: favorites + directory tree + preview -----
    auto *leftWidget = new QSplitter(Qt::Vertical, this);
    m_leftSplitter = leftWidget;
    m_navigationWidget = leftWidget;
    leftWidget->setObjectName("navigationPanel");

    // P0: Favorites bar — quick-access pinned directories above the tree.
    m_favoritesBar = new QListWidget(leftWidget);
    m_favoritesBar->setMaximumHeight(100);
    m_favoritesBar->hide();
    m_favoritesBar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_favoritesBar->setToolTip(
        tr("Favorites — Right-click to remove, Ctrl+D to favorite the current folder"));
    connect(m_favoritesBar, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
            { changeDirectory(item->data(Qt::UserRole).toString()); });
    m_favoritesBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_favoritesBar, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos)
            {
                auto *item = m_favoritesBar->itemAt(pos);
                if (!item)
                    return;
                QMenu menu;
                QAction *act = menu.addAction(tr("Remove from Favorites"));
                if (menu.exec(m_favoritesBar->mapToGlobal(pos)) == act)
                    removeFavorite(item->data(Qt::UserRole).toString());
            });
    auto updateFavoritesVisibility = [this]()
    {
        if (m_favoritesBar)
            m_favoritesBar->setVisible(m_favoritesBar->count() > 0);
    };
    connect(m_favoritesBar->model(), &QAbstractItemModel::rowsInserted, this,
            [updateFavoritesVisibility]() { updateFavoritesVisibility(); });
    connect(m_favoritesBar->model(), &QAbstractItemModel::rowsRemoved, this,
            [updateFavoritesVisibility]() { updateFavoritesVisibility(); });
    leftWidget->addWidget(m_favoritesBar);

    // The vertical splitter owns only complete sections. Section internals use
    // layouts so a persisted splitter state can never stretch the filter edit
    // or either fixed-height title into a large blank area.
    auto *foldersSection = new QWidget(leftWidget);
    foldersSection->setObjectName("foldersSection");
    auto *foldersLayout = new QVBoxLayout(foldersSection);
    // Finder-style sidebar: content inset from the edges so selection pills float.
    foldersLayout->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace8,
                                      mviewer::ui::metrics::kSpace8, 0);
    foldersLayout->setSpacing(mviewer::ui::metrics::kSpace4);
    foldersLayout->addWidget(
        sectionHeaderLabel(foldersSection, "foldersSectionLabel", tr("Folders")));

    m_directoryTree = new DirectoryTree(foldersSection);
    m_directoryTree->installEventFilter(this);
    if (m_directoryTree->viewport())
        m_directoryTree->viewport()->installEventFilter(this);
    foldersLayout->addWidget(m_directoryTree, 1);
    leftWidget->addWidget(foldersSection);

    auto *previewSection = new QWidget(leftWidget);
    previewSection->setObjectName("previewSection");
    auto *previewLayout = new QVBoxLayout(previewSection);
    previewLayout->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4,
                                      mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace8);
    previewLayout->setSpacing(mviewer::ui::metrics::kSpace4);
    previewLayout->addWidget(
        sectionHeaderLabel(previewSection, "previewSectionLabel", tr("Preview")));
    // The preview sits in a rounded info card. The card only frames it; the
    // panel itself (painting, presentation and flicker guards) is unchanged.
    auto *previewCard = new QFrame(previewSection);
    previewCard->setObjectName(QStringLiteral("previewCard"));
    auto *previewCardLayout = new QVBoxLayout(previewCard);
    previewCardLayout->setContentsMargins(
        mviewer::ui::metrics::kSpace4, mviewer::ui::metrics::kSpace4, mviewer::ui::metrics::kSpace4,
        mviewer::ui::metrics::kSpace4);
    previewCardLayout->setSpacing(0);
    m_previewPanel = new PreviewPanel(previewCard);
    m_previewPanel->installEventFilter(this);
    previewCardLayout->addWidget(m_previewPanel, 1);
    previewLayout->addWidget(previewCard, 1);
    leftWidget->addWidget(previewSection);

    leftWidget->setStretchFactor(0, 0); // optional favorites
    leftWidget->setStretchFactor(1, 3); // folders section
    leftWidget->setStretchFactor(2, 2); // preview section
    leftWidget->setChildrenCollapsible(false);
    leftWidget->setSizes({0, 390, 260});

    // The pre-professional-browser sidebar persisted a splitter with more
    // children. Qt can partially restore that state into the new three-section
    // splitter, producing a tiny folder tree and an oversized preview. Run once
    // after MainWindow's synchronous settings restore, then leave subsequent
    // user-adjusted proportions untouched.
    QTimer::singleShot(
        0, this,
        [this]()
        {
            constexpr int currentLayoutVersion = 1;
            QSettings settings;
            if (settings.value("browserSidebarLayoutVersion", 0).toInt() >= currentLayoutVersion)
            {
                return;
            }

            if (m_leftSplitter)
            {
                const bool showFavorites =
                    m_favoritesBar && m_favoritesBar->count() > 0 && !m_favoritesBar->isHidden();
                m_leftSplitter->setSizes({showFavorites ? 80 : 0, 390, 260});
            }
            settings.setValue("browserSidebarLayoutVersion", currentLayoutVersion);
            settings.sync();
        });

    return leftWidget;
}

QWidget *MainWindow::buildSortBar(QWidget *parent)
{
    auto *sortBar = new QWidget(parent);
    auto *sortRootLayout = new QVBoxLayout(sortBar);
    sortRootLayout->setContentsMargins(0, 0, 0, 0);
    sortRootLayout->setSpacing(mviewer::ui::metrics::kSpace4);
    // Address bar sits on the gallery toolbar strip, above Sort / Advanced Filter.
    if (m_pathEdit)
    {
        m_pathEdit->setParent(sortBar);
        sortRootLayout->addWidget(m_pathEdit);
    }
    auto *sortLayout = new QHBoxLayout;
    sortLayout->setContentsMargins(8, 8, 8, 8);
    sortLayout->setSpacing(8);
    sortRootLayout->addLayout(sortLayout);

    auto *advancedFilterPanel = new QWidget(sortBar);
    advancedFilterPanel->setObjectName("advancedFilterPanel");
    auto *advancedLayout = new QHBoxLayout(advancedFilterPanel);
    advancedLayout->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4,
                                       mviewer::ui::metrics::kSpace8,
                                       mviewer::ui::metrics::kSpace4);
    advancedLayout->setSpacing(mviewer::ui::metrics::kSpace8);
    sortRootLayout->addWidget(advancedFilterPanel);
    m_advancedFilterPanel = advancedFilterPanel;

    buildPrimarySortControls(sortBar, sortLayout);
    buildAdvancedFilterControls(sortBar, sortLayout, advancedFilterPanel, advancedLayout);
    buildSearchControls(sortBar, sortLayout, advancedFilterPanel, advancedLayout);
    polishGalleryToolbar(sortBar);
    return sortBar;
}

void MainWindow::buildPrimarySortControls(QWidget *sortBar, QHBoxLayout *sortLayout)
{
    auto *sortLabel = new QLabel(tr("Sort:"), sortBar);
    sortLabel->setObjectName(QStringLiteral("sortFieldLabel"));
    sortLayout->addWidget(sortLabel);
    m_sortCombo = new QComboBox(sortBar);
    applySortComboItems(m_sortCombo);
    sortLayout->addWidget(m_sortCombo);

    // A-2.2: sort direction toggle (ascending / descending).
    m_sortDirBtn = new QPushButton(QStringLiteral("↑"), sortBar);
    m_sortDirBtn->setObjectName("sortDirBtn");
    m_sortDirBtn->setFixedWidth(28);
    m_sortDirBtn->setCheckable(true);
    m_sortDirBtn->setToolTip(sortDirectionToolTip(true));
    sortLayout->addWidget(m_sortDirBtn);
    connect(m_sortDirBtn, &QPushButton::toggled, this,
            [this](bool descending)
            {
                if (m_sortDirBtn)
                    m_sortDirBtn->setText(descending ? "↓" : "↑");
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setSortAscending(!descending);
            });
}

void MainWindow::buildAdvancedFilterControls(QWidget *sortBar, QHBoxLayout *sortLayout,
                                             QWidget *advancedFilterPanel,
                                             QHBoxLayout *advancedLayout)
{
    // A-2.3: file type filter. Multi-select format groups (RAW unchecked by
    // default), persisted and shared by both browse panes; the right pane has
    // an identical combo in its sort row.
    m_typeFilterCombo = new FormatFilterCombo(sortBar);
    m_typeFilterCombo->setObjectName(QStringLiteral("typeFilterCombo"));
    m_typeFilterCombo->setHiddenGroups(m_hiddenFormatGroups);
    connect(m_typeFilterCombo, &FormatFilterCombo::hiddenGroupsChanged, this,
            &MainWindow::setBrowseHiddenFormatGroups);
    sortLayout->addWidget(m_typeFilterCombo);
    auto *advancedFilterToggle = new QPushButton(tr("Advanced Filter"), sortBar);
    advancedFilterToggle->setObjectName("advancedFilterToggle");
    advancedFilterToggle->setCheckable(true);
    advancedFilterToggle->setToolTip(tr("Show camera, lens, rating, and metadata filters"));
    sortLayout->addWidget(advancedFilterToggle);
    connect(advancedFilterToggle, &QPushButton::toggled, advancedFilterPanel, &QWidget::setVisible);
    auto *clearFilters = new QPushButton(tr("Clear Filters"), sortBar);
    clearFilters->setObjectName("clearFiltersButton");
    advancedLayout->addWidget(clearFilters);

    // P0 #①: metadata filters — camera / lens (substring) and ISO (exact).
    auto *camEdit = new QLineEdit(sortBar);
    camEdit->setObjectName(QStringLiteral("cameraFilterEdit"));
    camEdit->setPlaceholderText(tr("Camera"));
    camEdit->setFixedWidth(80);
    camEdit->setClearButtonEnabled(true);
    camEdit->setToolTip(tr("Filter by camera (brand/model), substring match"));
    advancedLayout->addWidget(camEdit);
    auto *lensEdit = new QLineEdit(sortBar);
    lensEdit->setObjectName(QStringLiteral("lensFilterEdit"));
    lensEdit->setPlaceholderText(tr("Lens"));
    lensEdit->setFixedWidth(95);
    lensEdit->setClearButtonEnabled(true);
    lensEdit->setToolTip(tr("Filter by lens model, substring match"));
    advancedLayout->addWidget(lensEdit);
    auto *isoSpin = new QSpinBox(sortBar);
    isoSpin->setRange(0, 65535);
    isoSpin->setObjectName(QStringLiteral("isoFilterSpin"));
    isoSpin->setSpecialValueText(tr("ISO"));
    isoSpin->setToolTip(tr("Filter by exact ISO (0 = all)"));
    isoSpin->setFixedWidth(72);
    advancedLayout->addWidget(isoSpin);
    connect(camEdit, &QLineEdit::textChanged, this, [this](const QString &t)
            { eachFilterPanel([&t](ThumbnailPanel *panel) { panel->setCameraFilter(t); }); });
    connect(lensEdit, &QLineEdit::textChanged, this, [this](const QString &t)
            { eachFilterPanel([&t](ThumbnailPanel *panel) { panel->setLensFilter(t); }); });
    connect(isoSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v)
            { eachFilterPanel([v](ThumbnailPanel *panel) { panel->setIsoFilter(v); }); });

    // P0 #①: free-form tag filter.
    auto *tagEdit = new QLineEdit(sortBar);
    tagEdit->setObjectName(QStringLiteral("tagFilterEdit"));
    tagEdit->setPlaceholderText(tr("Label"));
    tagEdit->setFixedWidth(90);
    tagEdit->setClearButtonEnabled(true);
    tagEdit->setToolTip(tr("Filter by tag exactly (empty = all)"));
    advancedLayout->addWidget(tagEdit);
    connect(tagEdit, &QLineEdit::textChanged, this, [this](const QString &t)
            { eachFilterPanel([&t](ThumbnailPanel *panel) { panel->setTagFilter(t); }); });

    // Clear Filters resets the transient filters only; the file type set is
    // a persisted browse preference (RAW hidden by default) and stays.
    connect(clearFilters, &QPushButton::clicked, this,
            [camEdit, lensEdit, isoSpin, tagEdit, this]()
            {
                camEdit->clear();
                lensEdit->clear();
                isoSpin->setValue(0);
                tagEdit->clear();
                m_searchEdit->clear();
                m_searchRecursive->setChecked(false);
                m_searchMeta->setChecked(false);
                m_ratingFilter->setCurrentIndex(0);
                m_flagFilter->setCurrentIndex(0);
            });
}

void MainWindow::buildSearchControls(QWidget *sortBar, QHBoxLayout *sortLayout,
                                     QWidget *advancedFilterPanel, QHBoxLayout *advancedLayout)
{
    // P0-2: View mode switcher (Grid / Large / Small / Detail / Filmstrip / Compact)
    m_viewModeCombo = new QComboBox(sortBar);
    m_viewModeCombo->setObjectName("thumbnailViewModeCombo");
    applyViewModeComboItems(m_viewModeCombo);
    m_viewModeCombo->setToolTip(tr("Switch thumbnail view mode (common modes Ctrl+1..6)"));
    // The segmented control is the visible switch. The combo stays in the tree
    // so objectName lookups and currentData remain the source of truth.
    m_viewModeCombo->hide();
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                m_thumbnailPanel->setViewMode(
                    static_cast<ThumbnailPanel::ViewMode>(m_viewModeCombo->currentData().toInt()));
            });

    // M18: live search bar. The field is hosted on the unified toolbar
    // (attachUnifiedToolbarControls); keep the objectName and the event filter.
    m_searchEdit = new QLineEdit(sortBar);
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setProperty("search", true);
    m_searchEdit->setPlaceholderText(tr("Filter by filename..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->installEventFilter(this);
    m_searchEdit->hide();
    m_searchRecursive = new QCheckBox(tr("Include Subfolders"), sortBar);
    advancedLayout->addWidget(m_searchRecursive);

    // P1: metadata-aware search (camera / lens / ISO / date / …).
    m_searchMeta = new QCheckBox(tr("Metadata"), sortBar);
    advancedLayout->addWidget(m_searchMeta);

    // P1: star-rating filter.
    auto *ratingLabel = new QLabel(tr("Rating:"), advancedFilterPanel);
    ratingLabel->setObjectName(QStringLiteral("ratingFilterLabel"));
    advancedLayout->addWidget(ratingLabel);
    m_ratingFilter = new QComboBox(sortBar);
    m_ratingFilter->setObjectName(QStringLiteral("ratingFilterCombo"));
    applyRatingFilterItems(m_ratingFilter);
    advancedLayout->addWidget(m_ratingFilter);

    // P3 tail: color label / reject / pick / recents filter.
    auto *flagLabel = new QLabel(tr("Flag:"), advancedFilterPanel);
    flagLabel->setObjectName(QStringLiteral("flagFilterLabel"));
    advancedLayout->addWidget(flagLabel);
    m_flagFilter = new QComboBox(sortBar);
    m_flagFilter->setObjectName(QStringLiteral("flagFilterCombo"));
    applyFlagFilterItems(m_flagFilter);
    advancedLayout->addWidget(m_flagFilter);

    auto *resetFilterBtn = new QPushButton(tr("Reset Filters"), advancedFilterPanel);
    resetFilterBtn->setObjectName("resetFiltersButton");
    resetFilterBtn->setToolTip(tr("Clear all search and filter conditions"));
    connect(resetFilterBtn, &QPushButton::clicked, this, &MainWindow::clearAllFilters);
    advancedLayout->addWidget(resetFilterBtn);

    advancedLayout->addStretch(1);
    advancedFilterPanel->hide();
}

QWidget *MainWindow::buildGalleryPanel()
{
    // ----- Right column: sort bar (top) + image gallery -----
    auto *rightWidget = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    m_gallerySplitter = new QSplitter(Qt::Horizontal, rightWidget);
    m_gallerySplitter->setObjectName(QStringLiteral("gallerySplitter"));
    m_gallerySplitter->setChildrenCollapsible(false);

    m_primaryBrowsePane = new QWidget();
    m_primaryBrowsePane->setObjectName(QStringLiteral("primaryBrowsePane"));
    auto *primaryLayout = new QVBoxLayout(m_primaryBrowsePane);
    primaryLayout->setContentsMargins(0, 0, 0, 0);
    primaryLayout->setSpacing(4);

    auto *sortBar = buildSortBar(m_primaryBrowsePane);
    primaryLayout->addWidget(sortBar);

    m_thumbnailPanel = new ThumbnailPanel(m_primaryBrowsePane);
    m_thumbnailPanel->setObjectName(QStringLiteral("thumbnailPanel"));
    m_thumbnailPanel->setConsumerId(0);
    m_thumbnailPanel->setPaneSlot(SelectionModel::PaneSlot::Primary);
    connect(m_thumbnailPanel, &ThumbnailPanel::usmCompareRequested, this,
            [this](const QString &path) { openUsmCompare(path); });
    m_thumbnailPanel->setHiddenFormatGroups(m_hiddenFormatGroups);
    wireTypeHiddenHint(m_thumbnailPanel);
    m_thumbnailPanel->setCommandStack(&m_cmdStack);
    m_thumbnailPanel->setSelectionModel(m_selection);
    placeGalleryCompareButton(sortBar);
    m_thumbnailPanel->installEventFilter(this);
    if (m_thumbnailPanel->viewport())
        m_thumbnailPanel->viewport()->installEventFilter(this);
    m_primaryBrowsePane->installEventFilter(this);
    m_thumbnailPanel->setMinimumWidth(220);
    primaryLayout->addWidget(m_thumbnailPanel, 1);
    m_gallerySplitter->addWidget(m_primaryBrowsePane);
    m_secondaryBrowsePane = buildSecondaryBrowsePane();
    m_gallerySplitter->addWidget(m_secondaryBrowsePane);
    m_secondaryBrowsePane->hide();
    m_primaryBrowsePane->lower();

    rightLayout->addWidget(m_gallerySplitter, 1);

    buildEmptyStateUi();
    m_emptyFolderTimer = new QTimer(this);
    m_emptyFolderTimer->setSingleShot(true);
    m_emptyFolderTimer->setInterval(600);
    connect(m_emptyFolderTimer, &QTimer::timeout, this, &MainWindow::updateEmptyFolderState);
    connect(m_thumbnailPanel, &ThumbnailPanel::viewModeChanged, this,
            [this](ThumbnailPanel::ViewMode mode)
            {
                for (int i = 0; i < m_viewModeCombo->count(); ++i)
                {
                    if (m_viewModeCombo->itemData(i).toInt() == static_cast<int>(mode))
                    {
                        // Do not block: the segmented control listens to the
                        // combo. Setting the same index does not re-enter.
                        m_viewModeCombo->setCurrentIndex(i);
                        break;
                    }
                }
            });
    m_thumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());
    attachUnifiedToolbarControls();

    return rightWidget;
}

void MainWindow::buildAnalysisAndSearchPanels()
{
    // ----- Analysis panel (rightmost) + Metadata panel (M18, between gallery & analysis) -----
    m_analysisPanel = new AnalysisPanel(this);
    m_analysisPanel->setObjectName("analysisPanel");
    m_analysisPanel->installEventFilter(this);
    // M21: AnalysisPanel ↔ AnalyzerModel (history / pin / result SSOT).
    m_analysisPanel->setAnalyzerModel(m_analyzer);
    connect(m_analysisPanel, &AnalysisPanel::historyImageRequested, this,
            [this](const QString &path)
            {
                if (!path.isEmpty())
                    onImageOpen(path);
            });
    // A-7.2: plugins are loaded in main() before MainWindow; refresh the combo
    // so runtime-discovered analyzers appear immediately.
    m_analysisPanel->refreshAnalyzers();
    // M15 P0#3: inject the analyzer pipeline so the panel orchestrates analyzers
    // through it instead of reaching the registry directly. MainWindow never
    // lists or creates analyzers itself — the pipeline owns that responsibility.
    m_analysisPanel->setPipeline(std::make_unique<AnalyzerPipeline>());
    // P1-6: expose a one-click report export from inside the analysis panel.
    connect(m_analysisPanel, &AnalysisPanel::exportRequested, this, [this]() { exportReport(); });
    m_metadataPanel = new MetadataPanel(this);
    m_searchPanel = new SearchPanel(this);
    m_searchPanel->setObjectName("searchPanel");
    m_searchPanel->installEventFilter(this);
}

void MainWindow::buildCentralContainer(QWidget *leftWidget, QWidget *rightWidget)
{
    // ----- 4-way horizontal split: left | gallery | analysis | search -----
    // Metadata panel is an overlay — hidden by default, shown on image click.
    auto *centralSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter = centralSplitter;
    centralSplitter->addWidget(leftWidget);
    centralSplitter->addWidget(rightWidget);
    centralSplitter->addWidget(m_analysisPanel);
    centralSplitter->addWidget(m_searchPanel);
    centralSplitter->setStretchFactor(0, 0);
    centralSplitter->setStretchFactor(1, 1);
    centralSplitter->setStretchFactor(2, 0);
    centralSplitter->setStretchFactor(3, 0);
    centralSplitter->setSizes({340, 820, 300, 240});
    // Prevent any panel from being collapsed to zero width — keeps the layout
    // usable when the window is narrow.
    centralSplitter->setChildrenCollapsible(false);
    leftWidget->setMinimumWidth(200);
    rightWidget->setMinimumWidth(320);
    m_analysisPanel->setMinimumWidth(200);
    m_searchPanel->setMinimumWidth(180);
    m_analysisPanel->hide();
    m_searchPanel->hide();
    // ----- M15: main content wrapper (breadcrumb + splitter) -----
    auto *mainContainer = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(mainContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_breadcrumb);
    // pathEdit lives in the gallery sort/toolbar strip (buildSortBar).
    mainLayout->addWidget(centralSplitter, 1);
    setCentralWidget(mainContainer);
}

void MainWindow::buildMetadataPanelUi()
{
    // ----- Metadata overlay (A-5: floating tool window, default hidden) -----
    // Not in the splitter — floats over the main area so browsing is unobstructed.
    m_metadataPanel->setParent(this);
    m_metadataPanel->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    m_metadataPanel->setAttribute(Qt::WA_ShowWithoutActivating, false);
    m_metadataPanel->setFixedSize(300, 480);
    m_metadataPanel->setWindowOpacity(0.92); // semi-transparent
    m_metadataPanel->hide();
}

void MainWindow::buildImageViewerUi()
{
    // ----- Full image viewer window -----
    m_imageViewer = new ImageViewer(nullptr);
    m_imageViewer->setWindowTitle(tr("Image Viewer - MViewer"));
    connect(m_imageViewer, &ImageViewer::usmCompareRequested, this,
            [this](const QString &path) { openUsmCompare(path); });

    // P0-3: metadata overlay on the image viewer (toggle with I / M / Image Info)
    m_metadataOverlay = new MetadataOverlay(m_imageViewer);
    m_metadataOverlay->hide();
    // Overlay visibility stays aligned with the View menu and the histogram task.
    connect(m_metadataOverlay, &MetadataOverlay::visibilityChanged, this,
            [this](bool visible)
            {
                visible ? scheduleMetadataHistogram() : cancelMetadataHistogram();
                if (m_actToggleMetadata)
                    m_actToggleMetadata->setChecked(visible);
            });

    connect(m_imageViewer, &ImageViewer::filesDropped, this, &MainWindow::handleDroppedPaths);
    m_imageViewer->installEventFilter(this);
    m_imageViewer->setMouseTracking(true);
    m_metadataHoverTimer = new QTimer(this);
    m_metadataHoverTimer->setSingleShot(true);
    m_metadataHoverTimer->setInterval(600);
}

void MainWindow::buildStatusBarUi()
{
    // P0 #①: real-time status bar — image count, total/selected size, viewer
    // zoom, and live cache hit-rate. Persistent (not transient showMessage).
    // Thin caption strip; the hairline and type size live in the theme QSS.
    statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
    statusBar()->setFixedHeight(mviewer::ui::metrics::kStatusBarHeight);
    statusBar()->setContentsMargins(0, 0, 0, 0);
    m_lblImage = new QLabel("—", this);
    m_lblCount = new QLabel(tr("0 images"), this);
    m_lblCount->setObjectName(QStringLiteral("statusImageCount"));
    m_lblSize = new QLabel(tr("Size 0 B"), this);
    m_lblSize->setObjectName(QStringLiteral("statusSelectionSize"));
    m_lblZoom = new QLabel(tr("Zoom"), this);
    m_lblCache = new QLabel(tr("Hit rate —"), this);
    m_lblRawSource = new QLabel(this);
    m_lblRawSource->setObjectName(QStringLiteral("rawSourceStatus"));
    m_lblCache->setToolTip(
        tr("Cache hit rate: improves consecutive large-image switching and thumbnail loading"));
    for (QLabel *l : {m_lblImage, m_lblCount, m_lblSize, m_lblZoom, m_lblCache, m_lblRawSource})
    {
        l->setProperty("role", QStringLiteral("caption"));
        l->setContentsMargins(mviewer::ui::metrics::kSpace8, 0, mviewer::ui::metrics::kSpace8, 0);
        statusBar()->addPermanentWidget(l);
    }
    m_lblSize->hide();
    m_lblCache->hide();
    m_lblRawSource->hide();
    m_lblZoom->setMinimumWidth(64);

    m_updateNotice = new QLabel(this);
    m_updateNotice->setObjectName(QStringLiteral("updateAvailableLink"));
    m_updateNotice->setProperty("role", QStringLiteral("caption"));
    m_updateNotice->setTextFormat(Qt::RichText);
    m_updateNotice->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_updateNotice->setContentsMargins(mviewer::ui::metrics::kSpace8, 0,
                                       mviewer::ui::metrics::kSpace8, 0);
    m_updateNotice->hide();
    statusBar()->addPermanentWidget(m_updateNotice);
    connect(m_updateNotice, &QLabel::linkActivated, this,
            [this](const QString &)
            {
                if (m_hasPendingUpdate)
                    showUpdateDialog(m_pendingUpdate);
            });

    connect(m_thumbnailPanel, &ThumbnailPanel::statsChanged, this,
            [this](int total, qint64 totalBytes, int selected, qint64 selBytes)
            {
                applyActivePaneStatusBar();
                if (m_splitBrowseEnabled || !m_lblSize)
                {
                    updateCacheStat();
                    return;
                }
                m_lblSize->setText(
                    selected > 0 ? tr("%1 selected · %2").arg(selected).arg(formatBytes(selBytes))
                                 : tr("Size %1").arg(formatBytes(totalBytes)));
                m_lblSize->hide();
                Q_UNUSED(total);
                updateCacheStat();
            });
    connect(m_imageViewer, &ImageViewer::zoomChanged, this,
            [this](int pct)
            {
                if (!m_lblZoom)
                    return;
                m_lblZoom->setText(
                    pct < 0 ? QStringLiteral("Zoom")
                            : QString::fromLatin1(mviewer::core::formatZoomPercent(pct).c_str()));
            });

    m_statTimer = new QTimer(this);
    connect(m_statTimer, &QTimer::timeout, this, &MainWindow::updateCacheStat);
    m_statTimer->start(500);

    // A-3.4: initial enablement for selection-dependent actions.
    updateSelectionActions();

    statusBar()->showMessage("Ready", 2000);
}
