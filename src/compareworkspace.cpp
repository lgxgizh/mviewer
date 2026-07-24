#include "compareworkspace.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QPointer>

#include "core/EventBus.h"
#include "core/compare/DifferenceEngine.h"
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <vector>

namespace
{

// UI 边界转换：核心层 ImageFrame -> Qt QImage，复用 mvcore::toQImage。
QImage imageObjectToQImage(const ImageFrame *img)
{
    if (!img)
        return QImage();
    return mvcore::toQImage(img->pixels());
}

} // namespace

CompareWorkspace::CompareWorkspace(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // m_engine is default-constructed as a member; no assignment needed.
    m_syncZoomChk = new QCheckBox("同步缩放(&Z)", this);
    m_syncZoomChk->setChecked(true);
    m_syncDragChk = new QCheckBox("同步拖动(&D)", this);
    m_syncDragChk->setChecked(true);

    auto applySync = [this](bool)
    {
        if (!m_syncZoom || !m_syncDrag)
        {
            // 关闭任一同步时,用当前 fit 结果初始化每张图的独立变换
            fitAll();
        }
        update();
    };
    connect(m_syncZoomChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncZoom = on;
                m_engine.setSyncEnabled(m_syncZoom && m_syncDrag);
                applySync(on);
            });
    connect(m_syncDragChk, &QCheckBox::toggled, this,
            [this, applySync](bool on)
            {
                m_syncDrag = on;
                m_engine.setSyncEnabled(m_syncZoom && m_syncDrag);
                applySync(on);
            });

    // Async diff result delivery. requestDiff() computes the diff on a worker
    // thread and publishes "CompareEngine.DiffResult" on the EventBus from that
    // thread. We hop to the UI thread before repainting (the engine pointer in
    // ctx identifies which CompareEngine produced it).
    m_diffSubId = EventBus::instance().subscribe("CompareEngine.DiffResult",
                                                 [this](void *ctx)
                                                 {
                                                     if (ctx != static_cast<void *>(&m_engine))
                                                         return;
                                                     // Repaint on the UI thread;
                                                     // refreshDiffOverlay() reads lastDiffImage().
                                                     QPointer<CompareWorkspace> guard(this);
                                                     QMetaObject::invokeMethod(
                                                         this,
                                                         [guard]()
                                                         {
                                                             if (!guard)
                                                                 return;
                                                             guard->refreshDiffOverlay();
                                                         },
                                                         Qt::QueuedConnection);
                                                 });

    auto *syncBar = new QWidget(this);
    auto *syncLayout = new QHBoxLayout(syncBar);
    syncLayout->setContentsMargins(0, 0, 0, 0);
    syncLayout->setSpacing(12);
    syncLayout->addWidget(m_syncZoomChk);
    syncLayout->addWidget(m_syncDragChk);

    // M14-3: blink (flicker) compare — rapid toggle between base and target.
    // Click the button (or press B) to start/stop rapid blinking.
    m_blinkChk = new QCheckBox("闪烁对比(&B)", this);
    m_blinkChk->setToolTip(tr("点击开始/停止快速闪烁切换（快捷键: B）"));
    connect(m_blinkChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    startBlink(150); // fast flicker
                else
                    stopBlink();
            });
    syncLayout->addWidget(m_blinkChk);

    // P0-4: split / swipe compare for exactly two images.
    m_splitChk = new QCheckBox("左右分割(&S)", this);
    m_splitChk->setEnabled(false);
    connect(m_splitChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on && m_swipeChk)
                    m_swipeChk->setChecked(false);
                if (on && m_overlayChk)
                    m_overlayChk->setChecked(false);
                if (m_grid)
                    m_grid->setVisible(!isSplitOrSwipe());
                update();
            });
    syncLayout->addWidget(m_splitChk);

    m_swipeChk = new QCheckBox("滑动对比(&W)", this);
    m_swipeChk->setEnabled(false);
    connect(m_swipeChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on && m_splitChk)
                    m_splitChk->setChecked(false);
                if (on && m_overlayChk)
                    m_overlayChk->setChecked(false);
                if (m_grid)
                    m_grid->setVisible(!isSplitOrSwipe());
                update();
            });
    syncLayout->addWidget(m_swipeChk);

    // A-4.1: Overlay compare mode — semi-transparent blend of the two images.
    m_overlayChk = new QCheckBox("叠加对比(&O)", this);
    m_overlayChk->setEnabled(false);
    connect(m_overlayChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on && m_splitChk)
                    m_splitChk->setChecked(false);
                if (on && m_swipeChk)
                    m_swipeChk->setChecked(false);
                if (m_grid)
                    m_grid->setVisible(!on && !isSplitOrSwipe());
                update();
            });
    syncLayout->addWidget(m_overlayChk);

    // A-4.5: continuous compare — walk consecutive pairs without reopening.
    m_prevPairBtn = new QPushButton("◀ 上一对", this);
    m_prevPairBtn->setToolTip(tr("比较上一对图片 (PageUp)"));
    m_prevPairBtn->setEnabled(false);
    connect(m_prevPairBtn, &QPushButton::clicked, this, &CompareWorkspace::prevPair);
    syncLayout->addWidget(m_prevPairBtn);

    m_nextPairBtn = new QPushButton("下一对 ▶", this);
    m_nextPairBtn->setToolTip(tr("比较下一对图片 (PageDown)"));
    m_nextPairBtn->setEnabled(false);
    connect(m_nextPairBtn, &QPushButton::clicked, this, &CompareWorkspace::nextPair);
    syncLayout->addWidget(m_nextPairBtn);

    // M15: threshold slider for difference heatmap (0-255).
    auto *thresholdLabel = new QLabel("阈值:", this);
    syncLayout->addWidget(thresholdLabel);
    m_thresholdSlider = new QSlider(Qt::Horizontal, this);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(0);
    m_thresholdSlider->setMaximumWidth(120);
    m_thresholdSlider->setToolTip("差异阈值: 低于此值的像素将被隐藏");
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                m_thresholdValue = static_cast<uint8_t>(value);
                refreshDiffOverlay();
            });
    syncLayout->addWidget(m_thresholdSlider);
    m_thresholdLabel = new QLabel("0", this);
    m_thresholdLabel->setMinimumWidth(24);
    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            [this](int value) { m_thresholdLabel->setText(QString::number(value)); });
    syncLayout->addWidget(m_thresholdLabel);

    // P0 #③: explicit multi-layout selector (auto / single / 2-4 columns / row).
    auto *layoutLabel = new QLabel(tr("布局:"), this);
    syncLayout->addWidget(layoutLabel);
    m_layoutCombo = new QComboBox(this);
    m_layoutCombo->addItems(
        {tr("自动"), tr("单列"), tr("2 列"), tr("3 列"), tr("4 列"), tr("一行")});
    m_layoutCombo->setCurrentIndex(0);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CompareWorkspace::onLayoutChanged);
    syncLayout->addWidget(m_layoutCombo);

    // P0 #③: inspector + histogram side panel toggle.
    m_sideChk = new QCheckBox(tr("检视面板"), this);
    m_sideChk->setChecked(false);
    connect(m_sideChk, &QCheckBox::toggled, this, &CompareWorkspace::onSideToggled);
    syncLayout->addWidget(m_sideChk);

    // M16.1: cursor-sync crosshair (n/n). When on, hovering any cell draws a
    // crosshair at the same image-space point across all compared cells, and the
    // inspector samples every cell at that point.
    m_crosshairChk = new QCheckBox(tr("同步准星"), this);
    m_crosshairChk->setChecked(false);
    syncLayout->addWidget(m_crosshairChk);

    // M16.1: focus-lock / reference pin (n/1). Locks a cell as the comparison
    // reference; diff overlays and inspector deltas use it as the base.
    m_focusBtn = new QPushButton(tr("锁定基准"), this);
    m_focusBtn->setCheckable(true);
    connect(m_focusBtn, &QPushButton::toggled, this,
            [this](bool on)
            {
                // Lock the focus on the cell under the cursor (fall back to cell 0 when
                // none is hovered). Toggling off clears the lock.
                const int idx = (on && m_hoverIdx >= 0) ? m_hoverIdx : (on ? 0 : m_focusIndex);
                onFocusRequested(on ? idx : -1);
            });
    syncLayout->addWidget(m_focusBtn);
    m_focusLabel = new QLabel(tr("基准: —"), this);
    m_focusLabel->setMinimumWidth(60);
    syncLayout->addWidget(m_focusLabel);

    syncLayout->addStretch(1);

    m_grid = new QWidget;
    m_layout = new QGridLayout(m_grid);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(0, 0, 0, 0);

    // QScrollArea wraps the grid so 2×4 layouts (5-8 images) can scroll
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(false);
    scroll->setWidget(m_grid);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    // P0 #③: right-side inspector/histogram panel (collapsible).
    m_sidePanel = new QWidget(this);
    m_sidePanel->setFixedWidth(280);
    auto *sideLay = new QVBoxLayout(m_sidePanel);
    sideLay->setContentsMargins(4, 4, 4, 4);
    sideLay->setSpacing(6);

    sideLay->addWidget(new QLabel(tr("像素检视"), this));
    m_inspector = new QTableWidget(this);
    m_inspector->setColumnCount(6);
    m_inspector->setHorizontalHeaderLabels(
        {tr("#"), tr("名称"), tr("R"), tr("G"), tr("B"), tr("Δ")});
    m_inspector->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inspector->setSelectionMode(QAbstractItemView::NoSelection);
    m_inspector->horizontalHeader()->setStretchLastSection(true);
    m_inspector->setFixedHeight(220);
    sideLay->addWidget(m_inspector);

    sideLay->addWidget(new QLabel(tr("RGB 直方图"), this));
    m_hist = new HistogramWidget(this);
    m_hist->setMinimumHeight(140);
    sideLay->addWidget(m_hist, 1);

    // M16.5: per-pane histogram overlay toggle
    m_perPaneHistChk = new QCheckBox(tr("每窗格独立直方图"), this);
    m_perPaneHistChk->setChecked(false);
    connect(m_perPaneHistChk, &QCheckBox::toggled, this, &CompareWorkspace::onPerPaneHistToggled);
    sideLay->addWidget(m_perPaneHistChk);

    // M16.4: quick PSNR/SSIM metrics label
    sideLay->addWidget(new QLabel(tr("差异指标"), this));
    m_metricLabel = new QLabel(tr("PSNR: —  SSIM: —"), this);
    m_metricLabel->setWordWrap(true);
    m_metricLabel->setStyleSheet("color:#888;");
    sideLay->addWidget(m_metricLabel);

    // M16.2: per-cell image edit panel (collapsible section inside side panel)
    buildEditPanel(sideLay);

    m_sidePanel->setVisible(false);

    // M16.6: layout presets save/load + swap panes (sync bar right side)
    m_savePresetBtn = new QPushButton(tr("存储布局"), this);
    m_savePresetBtn->setToolTip(tr("将当前布局存储为预设"));
    connect(m_savePresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onSavePreset);
    syncLayout->addWidget(m_savePresetBtn);

    m_loadPresetBtn = new QPushButton(tr("读取布局"), this);
    m_loadPresetBtn->setToolTip(tr("从预设文件中读取布局"));
    connect(m_loadPresetBtn, &QPushButton::clicked, this, &CompareWorkspace::onLoadPreset);
    syncLayout->addWidget(m_loadPresetBtn);

    m_swapBtn = new QPushButton(tr("交换窗格"), this);
    m_swapBtn->setToolTip(tr("交换选中的两个窗格"));
    m_swapBtn->setEnabled(false);
    connect(m_swapBtn, &QPushButton::clicked, this, &CompareWorkspace::onSwapPanes);
    syncLayout->addWidget(m_swapBtn);

    auto *leftLay = new QVBoxLayout;
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(4);
    leftLay->addWidget(syncBar);
    leftLay->addWidget(scroll, 1);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addLayout(leftLay, 1);
    root->addWidget(m_sidePanel);
}

CompareWorkspace::~CompareWorkspace()
{
    // The EventBus is a process-global singleton. If we don't unsubscribe, a
    // pending "CompareEngine.DiffResult" could fire into this (now destroyed)
    // widget and crash. The subscription id is stored in m_diffSubId.
    if (m_diffSubId != 0)
        EventBus::instance().unsubscribe(m_diffSubId);
}

// A-4.5: continuous compare — walk consecutive pairs from a pool.
void CompareWorkspace::setImagePool(const QStringList &allPaths)
{
    m_imagePool = allPaths;
    m_pairIndex = 0;
    updatePairButtons();
}

bool CompareWorkspace::hasNextPair() const
{
    return m_pairIndex + 3 < m_imagePool.size(); // need at least pairIndex+2 and +3
}

bool CompareWorkspace::hasPrevPair() const
{
    return m_pairIndex >= 2;
}

void CompareWorkspace::nextPair()
{
    if (!hasNextPair())
        return;
    m_pairIndex += 2;
    const QStringList pair{m_imagePool[m_pairIndex], m_imagePool[m_pairIndex + 1]};
    setImages(pair);
    updatePairButtons();
}

void CompareWorkspace::prevPair()
{
    if (!hasPrevPair())
        return;
    m_pairIndex -= 2;
    const QStringList pair{m_imagePool[m_pairIndex], m_imagePool[m_pairIndex + 1]};
    setImages(pair);
    updatePairButtons();
}

void CompareWorkspace::updatePairButtons()
{
    if (m_nextPairBtn)
        m_nextPairBtn->setEnabled(hasNextPair());
    if (m_prevPairBtn)
        m_prevPairBtn->setEnabled(hasPrevPair());
}

void CompareWorkspace::setImages(const QStringList &paths)
{
    std::vector<std::string> stdPaths;
    stdPaths.reserve(paths.size());
    for (const QString &p : paths)
        stdPaths.push_back(p.toStdString());
    m_engine.setImages(stdPaths);
    // A-4: loading a fresh comparison set should not inherit adjustments from
    // the previous session; applySession will repopulate persisted values.
    m_cellAdjusts.clear();
    rebuildCells();
    fitAll();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();

    // P0-4: split / swipe only make sense for exactly two images.
    const bool two = m_engine.imageCount() == 2;
    if (m_splitChk)
    {
        if (!two)
            m_splitChk->setChecked(false);
        m_splitChk->setEnabled(two);
    }
    if (m_swipeChk)
    {
        if (!two)
            m_swipeChk->setChecked(false);
        m_swipeChk->setEnabled(two);
    }
    // A-4.1: overlay mode also only for two images.
    if (m_overlayChk)
    {
        if (!two)
            m_overlayChk->setChecked(false);
        m_overlayChk->setEnabled(two);
    }
    if (m_grid && !two)
        m_grid->setVisible(true);
    setFocus();
}

QStringList CompareWorkspace::comparedImages() const
{
    QStringList out;
    const int n = m_engine.imageCount();
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        if (img)
            out.push_back(QString::fromStdString(img->metadata().filePath));
    }
    return out;
}

bool CompareWorkspace::isSyncEnabled() const
{
    return m_syncZoom && m_syncDrag;
}

void CompareWorkspace::setSyncEnabled(bool on)
{
    m_syncZoom = on;
    m_syncDrag = on;
    m_syncZoomChk->setChecked(on);
    m_syncDragChk->setChecked(on);
    m_engine.setSyncEnabled(on);
}

void CompareWorkspace::rebuildCells()
{
    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)))
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_cellLabels.clear();
    m_cellViews.clear();

    const int n = m_engine.imageCount();
    // Drop a stale focus lock when the image set shrank.
    if (m_focusIndex >= n)
    {
        m_focusIndex = -1;
        if (m_focusBtn)
            m_focusBtn->setChecked(false);
        if (m_focusLabel)
            m_focusLabel->setText(tr("基准: —"));
    }
    const auto &lay = m_engine.layout();
    for (int i = 0; i < n; ++i)
    {
        // Each cell: a RawImageView for the image + a QLabel caption below
        auto *cellWidget = new QWidget(m_grid);
        auto *cellLay = new QVBoxLayout(cellWidget);
        cellLay->setContentsMargins(0, 0, 0, 0);
        cellLay->setSpacing(1);

        auto *view = new RawImageView(cellWidget);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        view->setMinimumSize(64, 64);
        view->setMouseTracking(true);
        view->installEventFilter(this);
        view->setCellIndex(i);
        cellLay->addWidget(view, 1);
        m_cellViews.push_back(view);

        const ImageFrame *img = m_engine.imageAt(i);
        if (img && !img->pixels().isNull())
        {
            QImage q = imageObjectToQImage(img);
            view->setImage(q);
        }

        // Compare mode (2+ images): request an asynchronous difference heatmap
        // (cell i vs base). The compute runs on a worker thread via JobSystem;
        // the result is delivered through the EventBus and painted by
        // refreshDiffOverlay() on the UI thread. The UI thread never blocks here.
        // Skip i==0 (self-diff is all-black and overwrites the useful result).
        if (n > 1 && img && i > 0)
        {
            m_engine.requestDiff(i, diffBaseIndex());
        }

        const QString cellName = img ? QString::fromStdString(img->metadata().fileName) : QString();
        connect(view, &RawImageView::pixelInfo, this,
                [this, cellName](int x, int y, int r, int g, int b, bool valid)
                {
                    if (!valid)
                    {
                        emit pixelInfo(QString());
                        return;
                    }
                    emit pixelInfo(QString("[%1] (%2,%3) RGB(%4,%5,%6)")
                                       .arg(cellName)
                                       .arg(x)
                                       .arg(y)
                                       .arg(r)
                                       .arg(g)
                                       .arg(b));
                    if (m_sidePanel && m_sidePanel->isVisible())
                    {
                        m_lastInspectX = x;
                        m_lastInspectY = y;
                        updateInspector(x, y);
                    }
                });
        connect(view, &RawImageView::selectionChanged, this,
                [this](const mviewer::domain::Selection &sel) { applySelectionToAll(sel); });
        connect(view, &RawImageView::crosshairMoved, this,
                [this, view](const QPointF &p) { onCrosshairMoved(view, p); });
        connect(view, &RawImageView::focusRequested, this, &CompareWorkspace::onFocusRequested);

        // Caption label
        auto *caption = new QLabel(cellWidget);
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("QLabel{background:#222;color:#ccc;padding:2px;}");
        caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        caption->setMinimumHeight(20);
        if (img)
            caption->setText(QString::fromStdString(img->metadata().fileName));
        cellLay->addWidget(caption);
        m_cellLabels.push_back(caption);

        const int row = i / lay.cols;
        const int col = i % lay.cols;
        m_layout->addWidget(cellWidget, row, col);
    }
}

void CompareWorkspace::refreshDiffOverlay()
{
    const DiffResult res = m_engine.lastDiff();
    if (!res.valid || res.index < 0 || res.index >= m_cellViews.size())
        return;
    const ImageData diff = m_engine.lastDiffImage();
    if (diff.isNull())
        return;
    // M15: apply threshold from UI slider
    const ImageData thresholded = DifferenceEngine::applyThreshold(diff, m_thresholdValue);
    const ImageData heat = DifferenceEngine::heatMap(thresholded);
    if (heat.isNull())
        return;
    m_cellViews[res.index]->setOverlay(mvcore::toQImage(heat), 0.5);
    update();
}

void CompareWorkspace::toggleBlink()
{
    m_blinkState = !m_blinkState;
    applyBlink(m_blinkState);
    update();
}

// M15 P0#1: start the blink timer at the given interval (ms). Extracted so the
// persisted blink interval can be restored via applySession().
void CompareWorkspace::startBlink(int intervalMs)
{
    if (!m_blinkTimer)
    {
        m_blinkTimer = new QTimer(this);
        connect(m_blinkTimer, &QTimer::timeout, this, [this]() { this->toggleBlink(); });
    }
    m_blinkTimer->start(intervalMs);
    m_blinkState = false;
    applyBlink(m_blinkState);
}

void CompareWorkspace::stopBlink()
{
    if (m_blinkTimer)
        m_blinkTimer->stop();
    // restore all cells visible
    for (auto *v : m_cellViews)
        if (v)
            v->setVisible(true);
    // Rebuild the grid layout to restore proper cell positions after blink
    // may have repositioned cells.
    rebuildCells();
    fitAll();
    update();
}

void CompareWorkspace::applyBlink(bool state)
{
    const int n = m_cellViews.size();
    if (n == 0)
        return;

    // For exactly two images, blink looks best when the active image fills the
    // entire grid area (rather than staying in its own cell slot). We achieve
    // this by showing only the active cell and stretching it across the grid.
    if (n == 2 && m_grid && m_layout)
    {
        const int activeIdx = state ? 1 : 0;
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            m_cellViews[i]->setVisible(i == activeIdx);
        }
        // Reposition the active cell to span the entire grid area.
        for (int i = 0; i < m_layout->count(); ++i)
        {
            QLayoutItem *item = m_layout->itemAt(i);
            if (item && item->widget())
            {
                m_layout->removeWidget(item->widget());
                --i;
            }
        }
        // Re-add the active cell's parent widget spanning the full grid.
        if (activeIdx < m_cellViews.size() && m_cellViews[activeIdx])
        {
            auto *cellWidget = m_cellViews[activeIdx]->parentWidget();
            if (cellWidget)
                m_layout->addWidget(cellWidget, 0, 0, -1, -1);
        }
    }
    else
    {
        // For 3+ images: toggle visibility of cell 0 vs all others.
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            if (i == 0)
                m_cellViews[i]->setVisible(!state);
            else
                m_cellViews[i]->setVisible(state);
        }
    }
}

void CompareWorkspace::onLayoutChanged()
{
    if (!m_layoutCombo)
        return;
    const int idx = m_layoutCombo->currentIndex();
    int cols = 0;
    switch (idx)
    {
    case 1:
        cols = 1;
        break; // 单列
    case 2:
        cols = 2;
        break; // 2 列
    case 3:
        cols = 3;
        break; // 3 列
    case 4:
        cols = 4;
        break; // 4 列
    case 5:
        cols = m_engine.imageCount();
        break; // 一行
    default:
        cols = 0;
        break; // 自动
    }
    m_engine.setColumns(cols);
    rebuildCells();
    fitAll();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
}

void CompareWorkspace::onSideToggled(bool on)
{
    if (!m_sidePanel)
        return;
    m_sidePanel->setVisible(on);
    if (on)
    {
        refreshHistograms();
        updateMetrics();
    }
    update();
}

void CompareWorkspace::updateInspector(int x, int y)
{
    if (!m_inspector)
        return;
    const auto probe = m_engine.inspectPixel(x, y, diffBaseIndex());
    if (probe.samples.empty() || !probe.samples[0].valid)
    {
        m_inspector->setRowCount(0);
        return;
    }
    const int n = m_engine.imageCount();
    m_inspector->setRowCount(n);
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame *img = m_engine.imageAt(i);
        const QString name = img ? QString::fromStdString(img->metadata().fileName) : QString();
        if (static_cast<size_t>(i) >= probe.samples.size())
            continue;
        const auto &s = probe.samples[static_cast<size_t>(i)];
        const double dist = (static_cast<size_t>(i) < probe.deltas.size())
                                ? probe.deltas[static_cast<size_t>(i)].dist
                                : 0.0;
        m_inspector->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_inspector->setItem(i, 1, new QTableWidgetItem(name));
        m_inspector->setItem(i, 2, new QTableWidgetItem(QString::number(s.r)));
        m_inspector->setItem(i, 3, new QTableWidgetItem(QString::number(s.g)));
        m_inspector->setItem(i, 4, new QTableWidgetItem(QString::number(s.b)));
        m_inspector->setItem(i, 5, new QTableWidgetItem(QString::number(static_cast<int>(dist))));
    }
}

void CompareWorkspace::refreshHistograms()
{
    if (!m_hist)
        return;
    const int n = m_engine.imageCount();

    if (m_perPaneHist && m_editIdx >= 0 && m_editIdx < n)
    {
        // Per-pane: show only the selected cell's histogram
        const ImageFrame *img = m_engine.imageAt(m_editIdx);
        auto h = img ? mviewer::core::computeHistogram(img->pixels()) : mviewer::core::Histogram{};
        m_hist->setHistograms({h});
    }
    else
    {
        // Overlaid: show all
        std::vector<mviewer::core::Histogram> hists;
        hists.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const ImageFrame *img = m_engine.imageAt(i);
            hists.push_back(img ? mviewer::core::computeHistogram(img->pixels())
                                : mviewer::core::Histogram{});
        }
        m_hist->setHistograms(hists);
    }
}

void CompareWorkspace::fitAll()
{
    double sharedScale = 1.0;
    bool first = true;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const ImageFrame *img = m_engine.imageAt(i);
        const QSize qs = m_cellViews[i]->size();
        const CellSize cell{qs.width(), qs.height()};
        QPixmap pm = QPixmap::fromImage(imageObjectToQImage(img));
        if (pm.isNull() || cell.w <= 0 || cell.h <= 0)
            continue;
        CellSize imgSize{pm.width(), pm.height()};
        m_engine.fitCell(i, cell, imgSize);
        if (first || m_engine.cellScale(i) < sharedScale)
            sharedScale = m_engine.cellScale(i);
        first = false;
    }
    if (m_syncZoom)
        m_engine.setScale(sharedScale);
    if (m_syncDrag)
        m_engine.setOffset(0.0, 0.0);
}

void CompareWorkspace::applySession(const mviewer::domain::CompareSession &s)
{
    // Sync mode: All == both zoom+drag synced; Off == neither. (The CompareSession
    // only stores All/Off for the persisted snapshot; per-axis toggles default
    // to the synced state.)
    const bool syncOn = (s.syncMode != mviewer::domain::SyncMode::Off);
    m_syncZoom = syncOn;
    m_syncDrag = syncOn;
    m_syncZoomChk->setChecked(syncOn);
    m_syncDragChk->setChecked(syncOn);
    m_engine.setSyncEnabled(syncOn);

    // Shared transform (used when sync is on).
    m_engine.setScale(s.sharedScale);
    m_engine.setOffset(s.sharedOffsetX, s.sharedOffsetY);

    // Per-cell independent transforms (used when sync is off).
    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        const int idx = static_cast<int>(i);
        if (idx >= m_engine.imageCount())
            break;
        m_engine.setCellScale(idx, s.cells[i].scale);
        m_engine.setCellOffset(idx, s.cells[i].offsetX, s.cells[i].offsetY);
    }

    // ROI / selection (synchronized across cells).
    if (s.selection.w > 0 && s.selection.h > 0)
    {
        applySelectionToAll(
            mviewer::domain::Selection{s.selection.x, s.selection.y, s.selection.w, s.selection.h});
    }

    // M15 P0#1: replay the UI-only state so the reopened view is identical.
    // HeatMap / Diff threshold.
    m_thresholdValue = s.threshold;
    if (m_thresholdSlider)
    {
        m_thresholdSlider->setValue(static_cast<int>(s.threshold));
        if (m_thresholdLabel)
            m_thresholdLabel->setText(QString::number(static_cast<int>(s.threshold)));
    }
    refreshDiffOverlay();

    // Layout combo (0=auto,1=single-col,2=2col,3=3col,4=4col,5=one-row). Setting
    // the index triggers onLayoutChanged which drives the engine's column count.
    if (m_layoutCombo && m_layoutCombo->currentIndex() != s.layoutIndex)
        m_layoutCombo->setCurrentIndex(s.layoutIndex);

    // Side (inspector + histogram) panel visibility.
    if (m_sideChk && m_sideChk->isChecked() != s.sidePanelVisible)
        m_sideChk->setChecked(s.sidePanelVisible);

    // Blink compare: restore interval + on/off state.
    if (m_blinkChk)
    {
        const bool wantBlink = s.isBlinking();
        if (m_blinkChk->isChecked() != wantBlink)
            m_blinkChk->setChecked(wantBlink);
        if (wantBlink)
            startBlink(s.blinkIntervalMs > 0 ? s.blinkIntervalMs : 500);
        else
            stopBlink();
    }

    update();
}

void CompareWorkspace::applySelectionToAll(const mviewer::domain::Selection &sel)
{
    // Engine owns the frames; it mirrors the synchronized ROI to every ImageFrame.
    m_engine.applySelectionToAll(sel);
    m_lastSelection = sel;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        m_cellViews[i]->setSelection(sel);
    }
    update();
}

void CompareWorkspace::onCrosshairMoved(RawImageView *view, const QPointF &pos)
{
    if (!view)
        return;
    // Track the hovered cell so the focus-lock button knows which cell to pin.
    m_hoverIdx = view->cellIndex();

    if (!m_crosshairChk || !m_crosshairChk->isChecked())
        return;

    const bool valid = pos.x() >= 0.0 && pos.y() >= 0.0;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        if (valid)
            m_cellViews[i]->setCrosshair(pos);
        else
            m_cellViews[i]->clearCrosshair();
    }
    // Sample every cell at the synced image-space point.
    if (valid && m_sidePanel && m_sidePanel->isVisible())
    {
        m_lastInspectX = qRound(pos.x());
        m_lastInspectY = qRound(pos.y());
        updateInspector(m_lastInspectX, m_lastInspectY);
    }
}

void CompareWorkspace::onFocusRequested(int cellIndex)
{
    // Toggle the locked reference: re-clicking the focused cell clears it.
    const int newFocus = (cellIndex == m_focusIndex) ? -1 : cellIndex;
    const bool locking = newFocus >= 0;

    m_focusIndex = newFocus;
    if (m_focusBtn)
        m_focusBtn->setChecked(locking);
    if (m_focusLabel)
        m_focusLabel->setText(locking ? tr("基准: %1").arg(newFocus + 1) : tr("基准: —"));

    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        m_cellViews[i]->setFocused(i == m_focusIndex);
    }

    // Re-request diffs against the new base and refresh the inspector deltas.
    if (n > 1)
    {
        for (int i = 0; i < n; ++i)
        {
            if (i != m_focusIndex)
                m_engine.requestDiff(i, diffBaseIndex());
        }
        refreshDiffOverlay();
    }
    if (m_sidePanel && m_sidePanel->isVisible() && m_lastInspectX >= 0)
        updateInspector(m_lastInspectX, m_lastInspectY);
    update();
}

void CompareWorkspace::paintEvent(QPaintEvent *)
{
    // Cells are raw QWidgets (RawImageView) that paint themselves. The workspace
    // only pushes the synchronized transform (scale + offset) so every cell tracks
    // the shared zoom/pan state. Image decode/overlay/draw lives in RawImageView,
    // never in this compare layer (see AGENTS.md: no decode in the QWidget layer).
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const auto &ct = m_engine.cellTransform(i);
        const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
        const QPointF off = m_syncDrag ? QPointF(m_engine.syncTransform().offset.x,
                                                 m_engine.syncTransform().offset.y)
                                       : QPointF(ct.offset.x, ct.offset.y);
        m_cellViews[i]->setTransform(sc, off);
    }

    // P0-4: split / swipe overlay for two images.
    if (n == 2 && (isSplitOrSwipe() || (m_overlayChk && m_overlayChk->isChecked())) &&
        m_cellViews.size() >= 2)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_splitChk && m_splitChk->isChecked())
            drawSplitCompare(p);
        else if (m_swipeChk && m_swipeChk->isChecked())
            drawSwipeCompare(p, int(width() * m_splitPos));
        else if (m_overlayChk && m_overlayChk->isChecked())
            drawOverlayCompare(p);
    }
}

void CompareWorkspace::drawSplitCompare(QPainter &p)
{
    const QRect r = rect();
    const QRect left(r.topLeft(), QSize(r.width() / 2, r.height()));
    const QRect right(left.topRight() + QPoint(1, 0),
                      QSize(r.width() - left.width() - 1, r.height()));
    const QImage &img0 = m_cellViews[0]->image();
    const QImage &img1 = m_cellViews[1]->image();
    drawFitImage(p, img0, left);
    drawFitImage(p, img1, right);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(left.topRight(), left.bottomRight());
}

void CompareWorkspace::drawSwipeCompare(QPainter &p, int x)
{
    const QRect r = rect();
    const QRect left(r.topLeft(), QSize(x, r.height()));
    const QRect right(left.topRight() + QPoint(1, 0), QSize(r.width() - x, r.height()));
    const QImage &img0 = m_cellViews[0]->image();
    const QImage &img1 = m_cellViews[1]->image();
    drawFitImage(p, img0, left);
    drawFitImage(p, img1, right);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPoint(x, 0), QPoint(x, r.height()));
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPoint(x, r.height() / 2), 4, 4);
}

// A-4.1: semi-transparent overlay blend of two images.
void CompareWorkspace::drawOverlayCompare(QPainter &p)
{
    if (m_cellViews.size() < 2)
        return;
    const QRect r = rect();
    const QImage &img0 = m_cellViews[0]->image();
    const QImage &img1 = m_cellViews[1]->image();

    // Draw the base image fully opaque over the entire viewport.
    drawFitImage(p, img0, r);

    // Blend the second image on top with reduced opacity so differences glow
    // and common content stays neutral.
    p.setOpacity(0.45);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    drawFitImage(p, img1, r);
    p.setOpacity(1.0);
}

void CompareWorkspace::drawFitImage(QPainter &p, const QImage &img, const QRect &target)
{
    if (img.isNull() || target.width() <= 0 || target.height() <= 0)
        return;
    const QSizeF src(img.size());
    const QSizeF dst(target.size());
    const double s = qMin(dst.width() / src.width(), dst.height() / src.height());
    const int w = int(src.width() * s);
    const int h = int(src.height() * s);
    const QRect dr(target.x() + (target.width() - w) / 2, target.y() + (target.height() - h) / 2, w,
                   h);
    p.drawImage(dr, img);
}

bool CompareWorkspace::eventFilter(QObject *obj, QEvent *event)
{
    const int idx = m_cellViews.indexOf(static_cast<RawImageView *>(obj));
    if (idx < 0)
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel)
    {
        auto *we = static_cast<QWheelEvent *>(event);
        const double factor = we->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const QPoint pos = we->position().toPoint();
        if (m_syncZoom)
        {
            m_engine.zoomAt(static_cast<double>(pos.x()), static_cast<double>(pos.y()), factor);
        }
        else
        {
            // Zoom only the hovered cell around the cursor.
            m_engine.zoomAt(static_cast<double>(pos.x()), static_cast<double>(pos.y()), factor, idx);
            // Clamp to a sane range to avoid runaway zoom.
            const double s = std::clamp(m_engine.cellTransform(idx).scale, 0.05, 50.0);
            m_engine.setCellScale(idx, s);
        }
        update();
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
        {
            m_dragging = true;
            m_lastMouse = me->pos();
            m_dragStartPos = me->pos();
            m_dragIdx = idx;
        }
        return false;
    }

    if (event->type() == QEvent::MouseMove)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_dragging)
        {
            const QPoint delta = me->pos() - m_lastMouse;
            m_lastMouse = me->pos();
            if (m_syncDrag)
            {
                const Vec2 o = m_engine.syncTransform().offset;
                m_engine.setOffset(o.x + delta.x(), o.y + delta.y());
            }
            else
            {
                const Vec2 oldOff = m_engine.cellTransform(m_dragIdx).offset;
                m_engine.setCellOffset(m_dragIdx, oldOff.x + delta.x(), oldOff.y + delta.y());
            }
            update();
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
        {
            m_dragging = false;
            // Click (no significant drag): select cell for editing & per-pane histogram
            const QPoint delta = me->pos() - m_dragStartPos;
            if (delta.manhattanLength() < 4)
            {
                onEditCellSelected(m_dragIdx);
                refreshHistograms();
            }
        }
        return false;
    }

    return QWidget::eventFilter(obj, event);
}

void CompareWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitAll();
}

// ─── M16.2: Per-cell image adjustments ───────────────────────────────────────

ImageData CompareWorkspace::applyAdjusts(const ImageData &src, const CellAdjust &a)
{
    if (src.isNull() || a.isIdentity())
        return src;

    ImageData cur = src;

    // Order: brightness → contrast → gamma → white balance
    if (a.brightness != 0)
        cur = adjustBrightness(cur, a.brightness);
    if (std::abs(a.contrast - 1.0f) >= 1e-6f)
        cur = adjustContrast(cur, a.contrast);
    if (std::abs(a.gamma - 1.0f) >= 1e-6f)
        cur = adjustGamma(cur, a.gamma);
    if (std::abs(a.rGain - 1.0f) >= 1e-6f || std::abs(a.bGain - 1.0f) >= 1e-6f)
        cur = adjustWhiteBalance(cur, a.rGain, a.bGain);

    // Crop
    if (a.hasCrop && a.cropW > 0 && a.cropH > 0)
    {
        const mviewer::domain::Selection sel{a.cropX, a.cropY, a.cropW, a.cropH};
        cur = cropRegion(cur, sel);
    }

    // Rotation (apply after crop)
    if (a.rotation != 0)
    {
        int rot = a.rotation % 360;
        if (rot < 0)
            rot += 360;
        while (rot > 0)
        {
            cur = rotate90CW(cur);
            rot -= 90;
        }
    }

    return cur;
}

void CompareWorkspace::buildEditPanel(QVBoxLayout *sideLayout)
{
    m_editPanel = new QWidget(m_sidePanel);
    auto *editLay = new QVBoxLayout(m_editPanel);
    editLay->setContentsMargins(0, 4, 0, 0);
    editLay->setSpacing(3);

    m_editLabel = new QLabel(tr("— 选中窗格后可编辑 —"), m_editPanel);
    m_editLabel->setStyleSheet("font-weight:bold;color:#ccc;");
    editLay->addWidget(m_editLabel);

    // Brightness slider [-255, 255]; 0=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("亮度"), m_editPanel));
        m_brightSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_brightSlider->setRange(-255, 255);
        m_brightSlider->setValue(0);
        m_brightVal = new QLabel("0", m_editPanel);
        m_brightVal->setMinimumWidth(30);
        row->addWidget(m_brightSlider);
        row->addWidget(m_brightVal);
        editLay->addLayout(row);
        connect(m_brightSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_brightVal->setText(QString::number(v));
                    onAdjChanged();
                });
    }

    // Contrast slider [0..300] → float [0.0..3.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("对比度"), m_editPanel));
        m_contrastSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_contrastSlider->setRange(0, 300);
        m_contrastSlider->setValue(100);
        m_contrastVal = new QLabel("1.0", m_editPanel);
        m_contrastVal->setMinimumWidth(30);
        row->addWidget(m_contrastSlider);
        row->addWidget(m_contrastVal);
        editLay->addLayout(row);
        connect(m_contrastSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_contrastVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // Gamma slider [5..800] → float [0.05..8.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("伽马"), m_editPanel));
        m_gammaSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_gammaSlider->setRange(5, 800);
        m_gammaSlider->setValue(100);
        m_gammaVal = new QLabel("1.00", m_editPanel);
        m_gammaVal->setMinimumWidth(30);
        row->addWidget(m_gammaSlider);
        row->addWidget(m_gammaVal);
        editLay->addLayout(row);
        connect(m_gammaSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_gammaVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // White balance: R gain [1..500] → float [0.01..5.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("WB R"), m_editPanel));
        m_rGainSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_rGainSlider->setRange(1, 500);
        m_rGainSlider->setValue(100);
        m_rGainVal = new QLabel("1.00", m_editPanel);
        m_rGainVal->setMinimumWidth(30);
        row->addWidget(m_rGainSlider);
        row->addWidget(m_rGainVal);
        editLay->addLayout(row);
        connect(m_rGainSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_rGainVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // White balance: B gain [1..500] → float [0.01..5.0]; 100=identity
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("WB B"), m_editPanel));
        m_bGainSlider = new QSlider(Qt::Horizontal, m_editPanel);
        m_bGainSlider->setRange(1, 500);
        m_bGainSlider->setValue(100);
        m_bGainVal = new QLabel("1.00", m_editPanel);
        m_bGainVal->setMinimumWidth(30);
        row->addWidget(m_bGainSlider);
        row->addWidget(m_bGainVal);
        editLay->addLayout(row);
        connect(m_bGainSlider, &QSlider::valueChanged, this,
                [this](int v)
                {
                    m_bGainVal->setText(QString::number(v / 100.0, 'f', 2));
                    onAdjChanged();
                });
    }

    // Reset button
    m_resetAdjBtn = new QPushButton(tr("重置调整"), m_editPanel);
    connect(m_resetAdjBtn, &QPushButton::clicked, this, &CompareWorkspace::onResetAdj);
    editLay->addWidget(m_resetAdjBtn);

    sideLayout->addWidget(m_editPanel);
}

void CompareWorkspace::onEditCellSelected(int cellIdx)
{
    m_editIdx = cellIdx;

    // Resize adjustment vector if needed
    const int needed = cellIdx + 1;
    if (static_cast<int>(m_cellAdjusts.size()) < needed)
        m_cellAdjusts.resize(static_cast<size_t>(needed));

    const CellAdjust &a = m_cellAdjusts[static_cast<size_t>(cellIdx)];

    // Block signals while setting slider values to avoid triggering onAdjChanged
    {
        QSignalBlocker b(m_brightSlider);
        m_brightSlider->setValue(a.brightness);
    }
    m_brightVal->setText(QString::number(a.brightness));

    {
        QSignalBlocker b(m_contrastSlider);
        m_contrastSlider->setValue(static_cast<int>(a.contrast * 100.0f));
    }
    m_contrastVal->setText(QString::number(a.contrast, 'f', 2));

    {
        QSignalBlocker b(m_gammaSlider);
        m_gammaSlider->setValue(static_cast<int>(a.gamma * 100.0f));
    }
    m_gammaVal->setText(QString::number(a.gamma, 'f', 2));

    {
        QSignalBlocker b(m_rGainSlider);
        m_rGainSlider->setValue(static_cast<int>(a.rGain * 100.0f));
    }
    m_rGainVal->setText(QString::number(a.rGain, 'f', 2));

    {
        QSignalBlocker b(m_bGainSlider);
        m_bGainSlider->setValue(static_cast<int>(a.bGain * 100.0f));
    }
    m_bGainVal->setText(QString::number(a.bGain, 'f', 2));

    const ImageFrame *img = m_engine.imageAt(cellIdx);
    m_editLabel->setText(img ? QString::fromStdString(img->metadata().fileName)
                             : tr("窗格 %1").arg(cellIdx + 1));
}

void CompareWorkspace::onAdjChanged()
{
    if (m_editIdx < 0 || m_editIdx >= static_cast<int>(m_cellAdjusts.size()))
        return;

    CellAdjust &a = m_cellAdjusts[static_cast<size_t>(m_editIdx)];
    a.brightness = m_brightSlider->value();
    a.contrast = m_contrastSlider->value() / 100.0f;
    a.gamma = m_gammaSlider->value() / 100.0f;
    a.rGain = m_rGainSlider->value() / 100.0f;
    a.bGain = m_bGainSlider->value() / 100.0f;

    applyAdjToCell(m_editIdx);
}

void CompareWorkspace::onResetAdj()
{
    if (m_editIdx < 0 || m_editIdx >= static_cast<int>(m_cellAdjusts.size()))
        return;

    m_cellAdjusts[static_cast<size_t>(m_editIdx)] = CellAdjust{};
    onEditCellSelected(m_editIdx); // resync sliders

    // Restore original image
    const ImageFrame *img = m_engine.imageAt(m_editIdx);
    if (img && m_cellViews[m_editIdx])
        m_cellViews[m_editIdx]->setImage(imageObjectToQImage(img));

    update();
}

void CompareWorkspace::applyAdjToCell(int cellIdx)
{
    if (cellIdx < 0 || cellIdx >= static_cast<int>(m_cellViews.size()))
        return;

    const ImageFrame *img = m_engine.imageAt(cellIdx);
    if (!img || img->pixels().isNull())
        return;

    const CellAdjust &a = (cellIdx < static_cast<int>(m_cellAdjusts.size()))
                              ? m_cellAdjusts[static_cast<size_t>(cellIdx)]
                              : CellAdjust{};

    if (a.isIdentity())
    {
        // Just show original
        if (m_cellViews[cellIdx])
            m_cellViews[cellIdx]->setImage(imageObjectToQImage(img));
    }
    else
    {
        ImageData adjusted = applyAdjusts(img->pixels(), a);
        if (adjusted.isNull())
            return;
        QImage qi = mvcore::toQImage(adjusted);
        if (m_cellViews[cellIdx])
            m_cellViews[cellIdx]->setImage(qi);
    }

    update();
}

// ─── M16.4: Quick PSNR/SSIM metrics ─────────────────────────────────────────

void CompareWorkspace::updateMetrics()
{
    if (!m_metricLabel)
        return;

    const int n = m_engine.imageCount();
    if (n < 2)
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        return;
    }

    const int baseIdx = diffBaseIndex();
    // Pick the first non-base cell
    int targetIdx = -1;
    for (int i = 0; i < n; ++i)
    {
        if (i != baseIdx)
        {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0)
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        return;
    }

    const ImageFrame *baseImg = m_engine.imageAt(baseIdx);
    const ImageFrame *tgtImg = m_engine.imageAt(targetIdx);
    if (!baseImg || !tgtImg || baseImg->pixels().isNull() || tgtImg->pixels().isNull())
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —"));
        return;
    }

    const auto baseV = baseImg->pixels().view();
    const auto tgtV = tgtImg->pixels().view();
    if (baseV.width != tgtV.width || baseV.height != tgtV.height)
    {
        m_metricLabel->setText(tr("PSNR: —  SSIM: —\n(图像尺寸不一致)"));
        return;
    }

    const double psnrVal = AnalysisEngine::psnr(baseImg->pixels(), tgtImg->pixels());
    const double ssimVal = AnalysisEngine::ssim(baseImg->pixels(), tgtImg->pixels());

    const QString psnrStr = QString::number(psnrVal, 'f', 2) + " dB";
    const QString ssimStr = QString::number(ssimVal, 'f', 4);

    m_metricLabel->setText(tr("PSNR: %1  SSIM: %2\n(Image #%3 vs #%4)")
                               .arg(psnrStr, ssimStr)
                               .arg(baseIdx + 1)
                               .arg(targetIdx + 1));
}

// ─── M16.5: Per-pane histogram toggle ────────────────────────────────────────

void CompareWorkspace::onPerPaneHistToggled(bool on)
{
    m_perPaneHist = on;
    refreshHistograms();
}

// ─── M16.6: Layout presets save/load ─────────────────────────────────────────

void CompareWorkspace::ensurePresetDir()
{
    if (!m_presetDir.isEmpty())
        return;
    // Store presets under the app's cache/config directory
    m_presetDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/compare_presets";
    QDir().mkpath(m_presetDir);
}

void CompareWorkspace::onSavePreset()
{
    ensurePresetDir();
    const QString fileName = QFileDialog::getSaveFileName(
        this, tr("存储对比布局"), m_presetDir, tr("对比布局文件 (*.mvc)\n所有文件 (*.*)"));
    if (fileName.isEmpty())
        return;

    QJsonObject root;

    // Save per-cell adjustments
    QJsonArray adjArray;
    for (size_t i = 0; i < m_cellAdjusts.size(); ++i)
    {
        const auto &a = m_cellAdjusts[i];
        QJsonObject ao;
        ao["brightness"] = a.brightness;
        ao["contrast"] = static_cast<double>(a.contrast);
        ao["gamma"] = static_cast<double>(a.gamma);
        ao["rGain"] = static_cast<double>(a.rGain);
        ao["bGain"] = static_cast<double>(a.bGain);
        ao["rotation"] = a.rotation;
        ao["hasCrop"] = a.hasCrop;
        if (a.hasCrop)
        {
            ao["cropX"] = a.cropX;
            ao["cropY"] = a.cropY;
            ao["cropW"] = a.cropW;
            ao["cropH"] = a.cropH;
        }
        adjArray.append(ao);
    }
    root["adjustments"] = adjArray;
    root["perPaneHist"] = m_perPaneHist;
    root["layoutIndex"] = m_layoutCombo ? m_layoutCombo->currentIndex() : 0;

    // Save session settings (includes engine state + UI state)
    const mviewer::domain::CompareSession sess = compareSession();
    QJsonObject sessionObj;
    sessionObj["synced"] = (sess.syncMode != mviewer::domain::SyncMode::Off);
    sessionObj["sharedScale"] = sess.sharedScale;
    sessionObj["sharedOffsetX"] = sess.sharedOffsetX;
    sessionObj["sharedOffsetY"] = sess.sharedOffsetY;
    sessionObj["blinkIndex"] = sess.blinkIndex;
    sessionObj["blinkIntervalMs"] = sess.blinkIntervalMs;
    sessionObj["threshold"] = static_cast<int>(sess.threshold);
    sessionObj["sidePanelVisible"] = sess.sidePanelVisible;
    sessionObj["layoutIndex"] = sess.layoutIndex;
    // selection
    QJsonObject selObj;
    selObj["x"] = sess.selection.x;
    selObj["y"] = sess.selection.y;
    selObj["w"] = sess.selection.w;
    selObj["h"] = sess.selection.h;
    selObj["active"] = sess.selection.active;
    selObj["synced"] = sess.selection.synced;
    sessionObj["selection"] = selObj;
    root["session"] = sessionObj;

    // Save image paths
    const QStringList imgPaths = comparedImages();
    QJsonArray pathArray;
    for (const auto &p : imgPaths)
        pathArray.append(p);
    root["paths"] = pathArray;

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("存储失败"), tr("无法写入文件:\n%1").arg(fileName));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
}

void CompareWorkspace::onLoadPreset()
{
    ensurePresetDir();
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("读取对比布局"), m_presetDir, tr("对比布局文件 (*.mvc)\n所有文件 (*.*)"));
    if (fileName.isEmpty())
        return;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("读取失败"), tr("无法打开文件:\n%1").arg(fileName));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();

    // Restore adjustments
    if (root.contains("adjustments"))
    {
        const QJsonArray adjArray = root["adjustments"].toArray();
        m_cellAdjusts.resize(static_cast<size_t>(adjArray.size()));
        for (int i = 0; i < adjArray.size(); ++i)
        {
            const QJsonObject ao = adjArray[i].toObject();
            auto &a = m_cellAdjusts[static_cast<size_t>(i)];
            a.brightness = ao["brightness"].toInt();
            a.contrast = static_cast<float>(ao["contrast"].toDouble(1.0));
            a.gamma = static_cast<float>(ao["gamma"].toDouble(1.0));
            a.rGain = static_cast<float>(ao["rGain"].toDouble(1.0));
            a.bGain = static_cast<float>(ao["bGain"].toDouble(1.0));
            a.rotation = ao["rotation"].toInt();
            a.hasCrop = ao["hasCrop"].toBool();
            if (a.hasCrop)
            {
                a.cropX = ao["cropX"].toInt();
                a.cropY = ao["cropY"].toInt();
                a.cropW = ao["cropW"].toInt();
                a.cropH = ao["cropH"].toInt();
            }
        }
    }

    // per-pane histogram
    if (root.contains("perPaneHist"))
    {
        const bool ph = root["perPaneHist"].toBool();
        m_perPaneHist = ph;
        if (m_perPaneHistChk)
            m_perPaneHistChk->setChecked(ph);
    }

    // Restore session settings inline
    if (root.contains("session") && root["session"].isObject())
    {
        const QJsonObject s = root["session"].toObject();
        mviewer::domain::CompareSession sess;
        sess.syncMode = s["synced"].toBool(false) ? mviewer::domain::SyncMode::All
                                                  : mviewer::domain::SyncMode::Off;
        sess.sharedScale = s["sharedScale"].toDouble(1.0);
        sess.sharedOffsetX = s["sharedOffsetX"].toDouble(0.0);
        sess.sharedOffsetY = s["sharedOffsetY"].toDouble(0.0);
        sess.blinkIndex = s["blinkIndex"].toInt(-1);
        sess.blinkIntervalMs = s["blinkIntervalMs"].toInt(500);
        sess.threshold = static_cast<uint8_t>(s["threshold"].toInt(0));
        sess.sidePanelVisible = s["sidePanelVisible"].toBool(false);
        sess.layoutIndex = s["layoutIndex"].toInt(0);
        if (s.contains("selection") && s["selection"].isObject())
        {
            const QJsonObject sel = s["selection"].toObject();
            sess.selection.x = sel["x"].toInt();
            sess.selection.y = sel["y"].toInt();
            sess.selection.w = sel["w"].toInt();
            sess.selection.h = sel["h"].toInt();
            sess.selection.active = sel["active"].toBool();
            sess.selection.synced = sel["synced"].toBool();
        }
        applySession(sess);
    }

    // Apply adjustments to all cells
    for (size_t i = 0; i < m_cellAdjusts.size(); ++i)
    {
        const int idx = static_cast<int>(i);
        if (idx < m_engine.imageCount())
            applyAdjToCell(idx);
    }

    // Restore layout combo
    if (root.contains("layoutIndex") && m_layoutCombo)
    {
        const int li = root["layoutIndex"].toInt(0);
        if (li >= 0 && li < m_layoutCombo->count())
            m_layoutCombo->setCurrentIndex(li);
    }

    if (m_sidePanel && m_sidePanel->isVisible())
    {
        refreshHistograms();
        updateMetrics();
    }
    update();
}

// ─── M16.6: Swap panes ───────────────────────────────────────────────────────

void CompareWorkspace::onSwapPanes()
{
    const int n = m_cellViews.size();
    if (n < 2)
        return;

    // Swap pane 0 and pane 1 by default; if editIdx is set, swap that with adjacent
    const int a = (m_editIdx >= 0 && m_editIdx < n) ? m_editIdx : 0;
    const int b = (a == 0) ? 1 : 0;

    if (a == b || a >= n || b >= n)
        return;

    // Swap engine frames
    m_engine.swapFrames(a, b);

    // Swap cell adjustments
    if (static_cast<size_t>(std::max(a, b)) < m_cellAdjusts.size())
        std::swap(m_cellAdjusts[static_cast<size_t>(a)], m_cellAdjusts[static_cast<size_t>(b)]);

    rebuildCells();

    // Re-apply adjustments to swapped positions
    applyAdjToCell(a);
    applyAdjToCell(b);

    fitAll();
    update();

    if (m_sidePanel && m_sidePanel->isVisible())
    {
        refreshHistograms();
        updateMetrics();
    }
}

// P0-4: temporary compare — hold Space to blink, release to stop.
void CompareWorkspace::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        if (m_blinkChk && m_blinkChk->isEnabled() && !m_blinkChk->isChecked())
        {
            m_tempBlinking = true;
            m_blinkChk->setChecked(true);
        }
        event->accept();
        return;
    }
    // P1-5: ESC dismisses the Compare dialog (the panel sits inside a QDialog,
    // and key events here don't auto-bubble up to it, so handle it explicitly).
    if (event->key() == Qt::Key_Escape)
    {
        event->accept();
        if (auto *dlg = qobject_cast<QDialog *>(window()))
            dlg->reject();
        return;
    }
    // A-4.5: PageDown / PageUp walk consecutive pairs in continuous compare.
    if (event->key() == Qt::Key_PageDown && !event->modifiers())
    {
        nextPair();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_PageUp && !event->modifiers())
    {
        prevPair();
        event->accept();
        return;
    }
    // B key: toggle blink compare on/off.
    if (event->key() == Qt::Key_B && !event->modifiers() && m_blinkChk &&
        m_blinkChk->isEnabled())
    {
        m_blinkChk->setChecked(!m_blinkChk->isChecked());
        event->accept();
        return;
    }
    // Number keys 1-6 switch the layout preset (parity with the layout combo),
    // so the user can re-tile without reaching for the mouse.
    if (m_layoutCombo && !event->modifiers())
    {
        const int key = event->key();
        int idx = -1;
        if (key == Qt::Key_1)
            idx = 0;
        else if (key == Qt::Key_2)
            idx = 1;
        else if (key == Qt::Key_3)
            idx = 2;
        else if (key == Qt::Key_4)
            idx = 3;
        else if (key == Qt::Key_5)
            idx = 4;
        else if (key == Qt::Key_6)
            idx = 5;
        if (idx >= 0 && idx < m_layoutCombo->count())
        {
            m_layoutCombo->setCurrentIndex(idx);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void CompareWorkspace::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && m_tempBlinking)
    {
        m_tempBlinking = false;
        if (m_blinkChk && m_blinkChk->isChecked())
            m_blinkChk->setChecked(false);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

// P0-4: swipe divider drag. In split mode the divider is fixed; in swipe mode it follows the
// cursor.
void CompareWorkspace::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_swipeChk && m_swipeChk->isChecked())
    {
        const int x = int(width() * m_splitPos);
        if (std::abs(event->pos().x() - x) < 12)
        {
            m_splitDragging = true;
            m_splitPos = std::clamp(event->pos().x() / double(width()), 0.05, 0.95);
            update();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CompareWorkspace::mouseMoveEvent(QMouseEvent *event)
{
    if (m_splitDragging && m_swipeChk && m_swipeChk->isChecked())
    {
        m_splitPos = std::clamp(event->pos().x() / double(width()), 0.05, 0.95);
        update();
        return;
    }
    if (m_swipeChk && m_swipeChk->isChecked())
    {
        const int x = int(width() * m_splitPos);
        setCursor(std::abs(event->pos().x() - x) < 12 ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void CompareWorkspace::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_splitDragging)
    {
        m_splitDragging = false;
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CompareWorkspace::leaveEvent(QEvent *)
{
    m_splitDragging = false;
    setCursor(Qt::ArrowCursor);
}
