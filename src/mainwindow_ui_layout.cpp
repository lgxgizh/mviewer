// MainWindow layout construction and command surfaces.
#include "mainwindow_p.h"

#include "ShortcutHelper.h"
#include "Theme.h"
#include "ThemeIcons.h"
#include "core/render/ZoomPercent.h"

#include <QFocusEvent>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>

namespace
{

// Address bar: first click / focus selects all (Ctrl+A look) so the user can
// immediately type a new path or copy. Later clicks while focused keep the
// caret so partial edits still work.
class SelectAllOnActivateLineEdit : public QLineEdit
{
  public:
    using QLineEdit::QLineEdit;

  protected:
    void focusInEvent(QFocusEvent *event) override
    {
        QLineEdit::focusInEvent(event);
        if (event->reason() == Qt::MouseFocusReason || event->reason() == Qt::TabFocusReason ||
            event->reason() == Qt::BacktabFocusReason ||
            event->reason() == Qt::ShortcutFocusReason || event->reason() == Qt::OtherFocusReason)
        {
            // Defer past the activating mouse press, which would otherwise place
            // a caret and clear the selection we want.
            QTimer::singleShot(0, this,
                               [this]()
                               {
                                   if (hasFocus())
                                       selectAll();
                               });
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const bool gainingFocus = !hasFocus();
        QLineEdit::mousePressEvent(event);
        if (gainingFocus)
            selectAll();
    }
};

QString actTip(const QString &label, const QAction *action)
{
    return mviewer::ui::appendShortcutHint(label, mviewer::ui::actionShortcutHint(action));
}

QLabel *sectionHeaderLabel(QWidget *parent, const char *objectName, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setProperty("sectionHeader", true);
    label->setFixedHeight(28);
    label->setStyleSheet(
        QStringLiteral("QLabel{color:%1;background:%2;padding-left:8px;}")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextSecondary),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg1)));
    return label;
}
} // namespace

void MainWindow::buildBrowserShell()
{
    m_actDirUp = new QAction(tr("上一级"), this);
    m_actDirUp->setObjectName("directoryUpAction");
    m_actDirUp->setShortcut(QKeySequence("Alt+Up"));
    m_actDirUp->setToolTip(actTip(tr("上一级目录"), m_actDirUp));
    m_actRefresh = new QAction(tr("刷新"), this);
    m_actRefresh->setObjectName("refreshDirectoryAction");
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    m_actRefresh->setToolTip(actTip(tr("刷新目录与缩略图"), m_actRefresh));
    auto *browserToolBar = new QToolBar(tr("浏览工具栏"), this);
    addToolBar(Qt::TopToolBarArea, browserToolBar);
    browserToolBar->setObjectName("browserToolBar");
    browserToolBar->setMovable(false);
    browserToolBar->setFloatable(false);
    browserToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    browserToolBar->setIconSize(QSize(18, 18));
    auto addBrowserAction =
        [browserToolBar](QAction *action, const char *iconName, const QString &tip = {})
    {
        if (!action)
            return;
        action->setIcon(mviewer::ui::toolbarIcon(iconName));
        if (!tip.isEmpty())
            action->setToolTip(tip);
        browserToolBar->addAction(action);
    };
    addBrowserAction(m_actOpenDir, "open", actTip(tr("打开目录"), m_actOpenDir));
    addBrowserAction(m_actDirBack, "back", actTip(tr("上一个目录"), m_actDirBack));
    addBrowserAction(m_actDirForward, "forward", actTip(tr("下一个目录"), m_actDirForward));
    addBrowserAction(m_actDirUp, "up");
    addBrowserAction(m_actRefresh, "refresh");
    browserToolBar->addSeparator();
    addBrowserAction(m_actAddFavorite, "favorite", actTip(tr("收藏当前目录"), m_actAddFavorite));
    addBrowserAction(m_actToggleAnalysis, "analysis", actTip(tr("分析面板"), m_actToggleAnalysis));
    addBrowserAction(m_actToggleSearch, "search", actTip(tr("全局搜索"), m_actToggleSearch));
    browserToolBar->addSeparator();
    addBrowserAction(m_actBrowseWorkspace, "browse");
    browserToolBar->addSeparator();
    if (m_actRotateCCW)
    {
        m_actRotateCCW->setIcon(mviewer::ui::toolbarIcon("rotate_ccw"));
        m_actRotateCCW->setToolTip(actTip(tr("逆时针旋转 90° 并覆盖原文件"), m_actRotateCCW));
        browserToolBar->addAction(m_actRotateCCW);
    }
    if (m_actRotateCW)
    {
        m_actRotateCW->setIcon(mviewer::ui::toolbarIcon("rotate_cw"));
        m_actRotateCW->setToolTip(actTip(tr("顺时针旋转 90° 并覆盖原文件"), m_actRotateCW));
        browserToolBar->addAction(m_actRotateCW);
    }
    if (m_actCompare)
    {
        m_actCompare->setIcon(mviewer::ui::toolbarIcon("compare"));
        m_actCompare->setObjectName("compareAction");
        m_actCompare->setToolTip(actTip(tr("选择 2–8 张图片进行比较"), m_actCompare));
        browserToolBar->addAction(m_actCompare);
    }
    browserToolBar->setIconSize(QSize(16, 16));
    installNavigationCollapse();

    // ----- Breadcrumb navigation bar (M15 Product Shell P0) -----
    m_breadcrumb = new BreadcrumbBar(this);
    // Keep the signal path for breadcrumb navigation, but do not spend a full
    // row duplicating the editable path field in the default browser shell.
    m_breadcrumb->setObjectName("breadcrumbBar");
    m_breadcrumb->hide();

    // ----- Path input bar (UX: type a path to jump to a directory) -----
    m_pathEdit = new SelectAllOnActivateLineEdit(this);
    m_pathEdit->setObjectName("pathEdit");
    m_pathEdit->setPlaceholderText("输入目录路径并按 Enter 切换...");
    m_pathEdit->setToolTip(
        "输入或粘贴目录路径，按 Enter 键进入该目录（快捷键: Ctrl+F / Alt+D 聚焦）。");
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
    m_favoritesBar->setStyleSheet(
        QStringLiteral("QListWidget { background: %1; border: none; }"
                       "QListWidget::item { padding: 3px 8px; color: %2; }"
                       "QListWidget::item:hover { background: %3; }")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg0),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg3)));
    m_favoritesBar->setToolTip("收藏目录 — 右键移除，Ctrl+D 收藏当前目录");
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
                QAction *act = menu.addAction("移除收藏");
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
    foldersLayout->setContentsMargins(0, 0, 0, 0);
    foldersLayout->setSpacing(2);
    foldersLayout->addWidget(
        sectionHeaderLabel(foldersSection, "foldersSectionLabel", tr("文件夹")));

    m_directoryTree = new DirectoryTree(foldersSection);
    m_directoryTree->installEventFilter(this);
    if (m_directoryTree->viewport())
        m_directoryTree->viewport()->installEventFilter(this);
    foldersLayout->addWidget(m_directoryTree, 1);
    leftWidget->addWidget(foldersSection);

    auto *previewSection = new QWidget(leftWidget);
    previewSection->setObjectName("previewSection");
    auto *previewLayout = new QVBoxLayout(previewSection);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(2);
    previewLayout->addWidget(sectionHeaderLabel(previewSection, "previewSectionLabel", tr("预览")));
    m_previewPanel = new PreviewPanel(previewSection);
    m_previewPanel->installEventFilter(this);
    previewLayout->addWidget(m_previewPanel, 1);
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
    sortRootLayout->setSpacing(2);
    // Address bar sits on the gallery toolbar strip, above 排序 / 高级筛选.
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
    advancedLayout->setContentsMargins(6, 2, 6, 4);
    advancedLayout->setSpacing(6);
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
    sortLayout->addWidget(new QLabel("排序：", sortBar));
    m_sortCombo = new QComboBox(sortBar);
    m_sortCombo->addItem("文件名", ThumbnailPanel::SortName);
    m_sortCombo->addItem("日期", ThumbnailPanel::SortDate);
    m_sortCombo->addItem("大小", ThumbnailPanel::SortSize);
    m_sortCombo->addItem("分辨率", ThumbnailPanel::SortResolution);
    m_sortCombo->addItem("类型", ThumbnailPanel::SortType);
    m_sortCombo->addItem("评分", ThumbnailPanel::SortRating);
    m_sortCombo->addItem("相机", ThumbnailPanel::SortCamera);
    m_sortCombo->addItem("镜头", ThumbnailPanel::SortLens);
    sortLayout->addWidget(m_sortCombo);

    // A-2.2: sort direction toggle (ascending / descending).
    m_sortDirBtn = new QPushButton("↑", sortBar);
    m_sortDirBtn->setObjectName("sortDirBtn");
    m_sortDirBtn->setFixedWidth(28);
    m_sortDirBtn->setCheckable(true);
    m_sortDirBtn->setToolTip(
        "↑升序：A→Z（数字按大小）、从早到晚、从小到大；↓相反。相同项仍按文件名");
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
    // A-2.3: file-type quick filter buttons.
    auto *typeFilterCombo = new QComboBox(sortBar);
    typeFilterCombo->setObjectName("typeFilterCombo");
    typeFilterCombo->addItem("全部类型", "");
    typeFilterCombo->addItem("JPG", "jpg,jpeg");
    typeFilterCombo->addItem("PNG", "png");
    typeFilterCombo->addItem("TIFF", "tif,tiff");
    typeFilterCombo->addItem("WebP", "webp");
    typeFilterCombo->addItem("RAW", "cr2,cr3,nef,nrw,arw,dng,orf,rw2,pef,raf");
    typeFilterCombo->setToolTip("按文件类型过滤");

    advancedLayout->addWidget(typeFilterCombo);
    auto *advancedFilterToggle = new QPushButton("高级筛选", sortBar);
    advancedFilterToggle->setObjectName("advancedFilterToggle");
    advancedFilterToggle->setCheckable(true);
    advancedFilterToggle->setToolTip("显示相机、镜头、评分和元数据筛选");
    sortLayout->addWidget(advancedFilterToggle);
    connect(advancedFilterToggle, &QPushButton::toggled, advancedFilterPanel, &QWidget::setVisible);
    auto *clearFilters = new QPushButton("清除筛选", sortBar);
    clearFilters->setObjectName("clearFiltersButton");
    advancedLayout->addWidget(clearFilters);

    // P0 #①: metadata filters — camera / lens (substring) and ISO (exact).
    auto *camEdit = new QLineEdit(sortBar);
    camEdit->setPlaceholderText("相机");
    camEdit->setFixedWidth(80);
    camEdit->setClearButtonEnabled(true);
    camEdit->setToolTip(tr("按相机(品牌/型号)过滤，子串匹配"));
    advancedLayout->addWidget(camEdit);
    auto *lensEdit = new QLineEdit(sortBar);
    lensEdit->setPlaceholderText("镜头");
    lensEdit->setFixedWidth(95);
    lensEdit->setClearButtonEnabled(true);
    lensEdit->setToolTip(tr("按镜头型号过滤，子串匹配"));
    advancedLayout->addWidget(lensEdit);
    auto *isoSpin = new QSpinBox(sortBar);
    isoSpin->setRange(0, 65535);
    isoSpin->setSpecialValueText("ISO");
    isoSpin->setToolTip(tr("按 ISO 精确过滤 (0 = 全部)"));
    isoSpin->setFixedWidth(72);
    advancedLayout->addWidget(isoSpin);
    connect(camEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setCameraFilter(t); });
    connect(lensEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setLensFilter(t); });
    connect(isoSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) { m_thumbnailPanel->setIsoFilter(v); });

    // P0 #①: free-form tag filter.
    auto *tagEdit = new QLineEdit(sortBar);
    tagEdit->setPlaceholderText("标签");
    tagEdit->setFixedWidth(90);
    tagEdit->setClearButtonEnabled(true);
    tagEdit->setToolTip(tr("按标签精确过滤 (空 = 全部)"));
    advancedLayout->addWidget(tagEdit);
    connect(tagEdit, &QLineEdit::textChanged, this,
            [this](const QString &t) { m_thumbnailPanel->setTagFilter(t); });

    connect(typeFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, typeFilterCombo]()
            {
                if (m_thumbnailPanel)
                    m_thumbnailPanel->setTypeFilter(typeFilterCombo->currentData().toString());
            });
    connect(clearFilters, &QPushButton::clicked, this,
            [typeFilterCombo, camEdit, lensEdit, isoSpin, tagEdit, this]()
            {
                typeFilterCombo->setCurrentIndex(0);
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
    m_viewModeCombo->addItem("网格 (Ctrl+1)", ThumbnailPanel::Thumbnail);
    m_viewModeCombo->addItem("大图标", ThumbnailPanel::LargeIcon);
    m_viewModeCombo->addItem("小图标 (Ctrl+5)", ThumbnailPanel::SmallIcon);
    m_viewModeCombo->addItem("列表 (Ctrl+2)", ThumbnailPanel::List);
    m_viewModeCombo->addItem("详情 (Ctrl+3)", ThumbnailPanel::Details);
    m_viewModeCombo->addItem("胶片条 (Ctrl+4)", ThumbnailPanel::Filmstrip);
    m_viewModeCombo->addItem("紧凑 (Ctrl+6)", ThumbnailPanel::Compact);
    m_viewModeCombo->setToolTip("切换缩略图视图模式（常用模式 Ctrl+1..6）");
    sortLayout->addWidget(m_viewModeCombo);
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                m_thumbnailPanel->setViewMode(
                    static_cast<ThumbnailPanel::ViewMode>(m_viewModeCombo->currentData().toInt()));
            });

    // M18: live search bar.
    sortLayout->addWidget(new QLabel("搜索：", sortBar));
    m_searchEdit = new QLineEdit(sortBar);
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setPlaceholderText("按文件名过滤...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->installEventFilter(this);
    sortLayout->addWidget(m_searchEdit, 1);
    m_searchRecursive = new QCheckBox("包含子目录", sortBar);
    advancedLayout->addWidget(m_searchRecursive);

    // P1: metadata-aware search (camera / lens / ISO / date / …).
    m_searchMeta = new QCheckBox("元数据", sortBar);
    advancedLayout->addWidget(m_searchMeta);

    // P1: star-rating filter.
    advancedLayout->addWidget(new QLabel("评分:", advancedFilterPanel));
    m_ratingFilter = new QComboBox(sortBar);
    m_ratingFilter->addItem("全部", 0);
    m_ratingFilter->addItem("★ 及以上", 1);
    m_ratingFilter->addItem("★★ 及以上", 2);
    m_ratingFilter->addItem("★★★ 及以上", 3);
    m_ratingFilter->addItem("★★★★ 及以上", 4);
    m_ratingFilter->addItem("★★★★★", 5);
    advancedLayout->addWidget(m_ratingFilter);

    // P3 tail: color label / reject / pick / recents filter.
    advancedLayout->addWidget(new QLabel("标记:", advancedFilterPanel));
    m_flagFilter = new QComboBox(sortBar);
    m_flagFilter->addItem("全部", 0);
    m_flagFilter->addItem("已收藏", 1);
    m_flagFilter->addItem("已拒绝", 2);
    m_flagFilter->addItem("最近浏览", 3);
    m_flagFilter->addItem("红标", 11);
    m_flagFilter->addItem("橙标", 12);
    m_flagFilter->addItem("黄标", 13);
    m_flagFilter->addItem("绿标", 14);
    m_flagFilter->addItem("蓝标", 15);
    m_flagFilter->addItem("紫标", 16);
    advancedLayout->addWidget(m_flagFilter);

    auto *resetFilterBtn = new QPushButton(tr("重置筛选"), advancedFilterPanel);
    resetFilterBtn->setObjectName("resetFiltersButton");
    resetFilterBtn->setToolTip(tr("清除所有搜索和过滤条件"));
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

    auto *sortBar = buildSortBar(rightWidget);
    rightLayout->addWidget(sortBar);

    m_thumbnailPanel = new ThumbnailPanel(rightWidget);
    placeGalleryCompareButton(sortBar);
    m_thumbnailPanel->setCommandStack(&m_cmdStack);   // A-10: reversible file ops
    m_thumbnailPanel->setSelectionModel(m_selection); // P0-2: gallery hover -> SSOT
    m_thumbnailPanel->installEventFilter(this);
    if (m_thumbnailPanel->viewport())
        m_thumbnailPanel->viewport()->installEventFilter(this);
    rightLayout->addWidget(m_thumbnailPanel, 1);

    // Empty-state hint overlay: shown until the first directory is opened.
    m_emptyState = new QLabel(m_thumbnailPanel);
    m_emptyState->setObjectName(QStringLiteral("emptyStateLabel"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_emptyState->setTextFormat(Qt::RichText);
    // clang-format off
    m_emptyState->setText(tr("<table align='center'><tr><td bgcolor='#1c1c1f' align='center' style='padding:22px 32px; border:1px solid #333338; border-radius:8px;'>"
                             "<div style='color:#f4f4f5; font-size:15px; font-weight:600; margin-bottom:8px;'>\u6253\u5f00\u4e00\u4e2a\u6587\u4ef6\u5939\u4ee5\u5f00\u59cb\u6d4f\u89c8</div>"
                             "<div style='color:#a1a1aa; font-size:12px;'>"
                             "<span style='background-color:#2a2b30; color:#e4e4e7;'>&nbsp;Ctrl+O&nbsp;</span> "
                             "\u6253\u5f00\u76ee\u5f55 &nbsp;\u00b7&nbsp; \u4e5f\u53ef\u5c06\u56fe\u7247\u6216\u6587\u4ef6\u5939\u62d6\u5165\u7a97\u53e3</div>"
                             "</td></tr></table>"));
    // clang-format on
    m_emptyState->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyState->show();
    updateEmptyState();

    // Empty-folder hint: a directory is open but nothing is displayable
    // (no image files, or every entry is hidden by filters). Deferred via
    // a short timer so the transient pre-scan zero cannot flash it.
    m_emptyFolderLabel = new QLabel(m_thumbnailPanel);
    m_emptyFolderLabel->setObjectName(QStringLiteral("emptyFolderLabel"));
    m_emptyFolderLabel->setAlignment(Qt::AlignCenter);
    m_emptyFolderLabel->setWordWrap(true);
    m_emptyFolderLabel->setTextFormat(Qt::RichText);
    // clang-format off
    m_emptyFolderLabel->setText(tr("<table align='center'><tr><td bgcolor='#1c1c1f' align='center' style='padding:22px 32px; border:1px solid #333338; border-radius:8px;'>"
                                   "<div style='color:#f4f4f5; font-size:15px; font-weight:600; margin-bottom:8px;'>\u6b64\u6587\u4ef6\u5939\u4e2d\u6ca1\u6709\u53ef\u663e\u793a\u7684\u56fe\u7247</div>"
                                   "<div style='color:#a1a1aa; font-size:12px;'>\u5c1d\u8bd5\u6e05\u9664\u7b5b\u9009\u6216\u6362\u4e00\u4e2a\u6587\u4ef6\u5939</div>"
                                   "</td></tr></table>"));
    // clang-format on
    m_emptyFolderLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyFolderLabel->hide();
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
                        const QSignalBlocker blocker(m_viewModeCombo);
                        m_viewModeCombo->setCurrentIndex(i);
                        break;
                    }
                }
            });
    m_thumbnailPanel->setViewMode(m_thumbnailPanel->viewMode());

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
    m_metadataPanel->setStyleSheet(
        QStringLiteral("MetadataPanel { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: 6px; }")
            .arg(mviewer::ui::Theme::themeRgba(mviewer::ui::ThemeRole::Bg0, 220),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Border)));
    m_metadataPanel->hide();
}

void MainWindow::buildImageViewerUi()
{
    // ----- Full image viewer window -----
    m_imageViewer = new ImageViewer(nullptr);
    m_imageViewer->setWindowTitle("图片查看 - MViewer");

    // P0-3: metadata overlay on the image viewer (toggle with I / M / 图片信息)
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
    m_lblImage = new QLabel("—", this);
    m_lblCount = new QLabel("图片 0", this);
    m_lblSize = new QLabel("大小 0 B", this);
    m_lblZoom = new QLabel("缩放 —", this);
    m_lblCache = new QLabel("命中率 —", this);
    m_lblCache->setToolTip(tr("缓存命中率: 提升连续大图切换与缩略图加载性能"));
    for (QLabel *l : {m_lblImage, m_lblCount, m_lblSize, m_lblZoom, m_lblCache})
    {
        l->setContentsMargins(8, 0, 8, 0);
        statusBar()->addPermanentWidget(l);
    }
    m_lblSize->hide();
    m_lblCache->hide();
    m_lblZoom->setMinimumWidth(64);

    connect(m_thumbnailPanel, &ThumbnailPanel::statsChanged, this,
            [this](int total, qint64 totalBytes, int selected, qint64 selBytes)
            {
                const int idx = m_imageList ? m_imageList->indexOf(currentImagePath()) : -1;
                if (total > 0 && idx >= 0)
                    m_lblCount->setText(QString("图片 %1 / %2").arg(idx + 1).arg(total));
                else
                    m_lblCount->setText(QString("图片 %1").arg(total));
                m_lblSize->setText(
                    selected > 0 ? QString("已选 %1 · %2").arg(selected).arg(formatBytes(selBytes))
                                 : QString("大小 %1").arg(formatBytes(totalBytes)));
                updateCacheStat();
            });
    connect(m_imageViewer, &ImageViewer::zoomChanged, this,
            [this](int pct)
            {
                if (!m_lblZoom)
                    return;
                m_lblZoom->setText(
                    pct < 0 ? QStringLiteral("缩放 —")
                            : QString::fromLatin1(mviewer::core::formatZoomPercent(pct).c_str()));
            });

    m_statTimer = new QTimer(this);
    connect(m_statTimer, &QTimer::timeout, this, &MainWindow::updateCacheStat);
    m_statTimer->start(500);

    // A-3.4: initial enablement for selection-dependent actions.
    updateSelectionActions();

    statusBar()->showMessage("就绪", 2000);
}
