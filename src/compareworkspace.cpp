#include "compareworkspace.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include "core/EventBus.h"
#include "core/compare/DifferenceEngine.h"
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
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
    m_diffSubId = EventBus::instance().subscribe(
        "CompareEngine.DiffResult",
        [this](void *ctx)
        {
            if (ctx != static_cast<void *>(&m_engine))
                return;
            // Repaint on the UI thread; refreshDiffOverlay() reads lastDiffImage().
            QMetaObject::invokeMethod(this, "refreshDiffOverlay", Qt::QueuedConnection);
        });

    auto *syncBar = new QWidget(this);
    auto *syncLayout = new QHBoxLayout(syncBar);
    syncLayout->setContentsMargins(0, 0, 0, 0);
    syncLayout->setSpacing(12);
    syncLayout->addWidget(m_syncZoomChk);
    syncLayout->addWidget(m_syncDragChk);

    // M14-3: blink (flicker) compare — 500ms toggle between base and target.
    m_blinkChk = new QCheckBox("闪烁对比(&B)", this);
    connect(m_blinkChk, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
        {
            startBlink(500);
        }
        else
        {
            stopBlink();
        }
    });
    syncLayout->addWidget(m_blinkChk);

    // M15: threshold slider for difference heatmap (0-255).
    auto *thresholdLabel = new QLabel("阈值:", this);
    syncLayout->addWidget(thresholdLabel);
    m_thresholdSlider = new QSlider(Qt::Horizontal, this);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(0);
    m_thresholdSlider->setMaximumWidth(120);
    m_thresholdSlider->setToolTip("差异阈值: 低于此值的像素将被隐藏");
    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this](int value) {
        m_thresholdValue = static_cast<uint8_t>(value);
        refreshDiffOverlay();
    });
    syncLayout->addWidget(m_thresholdSlider);
    m_thresholdLabel = new QLabel("0", this);
    m_thresholdLabel->setMinimumWidth(24);
    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this](int value) {
        m_thresholdLabel->setText(QString::number(value));
    });
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
    m_sidePanel->setVisible(false);

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

void CompareWorkspace::setImages(const QStringList &paths)
{
    std::vector<std::string> stdPaths;
    stdPaths.reserve(paths.size());
    for (const QString &p : paths)
        stdPaths.push_back(p.toStdString());
    m_engine.setImages(stdPaths);
    rebuildCells();
    fitAll();
    update();
    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
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
            m_engine.requestDiff(i, 0);
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
                        updateInspector(x, y);
                });
        connect(view, &RawImageView::selectionChanged, this,
                [this](const mviewer::domain::Selection &sel) { applySelectionToAll(sel); });

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
        if (v) v->setVisible(true);
}

void CompareWorkspace::applyBlink(bool state)
{
    const int n = m_cellViews.size();
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

void CompareWorkspace::onLayoutChanged()
{
    if (!m_layoutCombo)
        return;
    const int idx = m_layoutCombo->currentIndex();
    int cols = 0;
    switch (idx)
    {
        case 1: cols = 1; break;   // 单列
        case 2: cols = 2; break;   // 2 列
        case 3: cols = 3; break;   // 3 列
        case 4: cols = 4; break;   // 4 列
        case 5: cols = m_engine.imageCount(); break;  // 一行
        default: cols = 0; break;  // 自动
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
        refreshHistograms();
    update();
}

void CompareWorkspace::updateInspector(int x, int y)
{
    if (!m_inspector)
        return;
    const auto probe = m_engine.inspectPixel(x, y);
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
        const QString name =
            img ? QString::fromStdString(img->metadata().fileName) : QString();
        const auto &s = probe.samples[static_cast<size_t>(i)];
        const double dist = (static_cast<size_t>(i) < probe.deltas.size())
                                ? probe.deltas[static_cast<size_t>(i)].dist
                                : 0.0;
        m_inspector->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_inspector->setItem(i, 1, new QTableWidgetItem(name));
        m_inspector->setItem(i, 2, new QTableWidgetItem(QString::number(s.r)));
        m_inspector->setItem(i, 3, new QTableWidgetItem(QString::number(s.g)));
        m_inspector->setItem(i, 4, new QTableWidgetItem(QString::number(s.b)));
        m_inspector->setItem(
            i, 5, new QTableWidgetItem(QString::number(static_cast<int>(dist))));
    }
}

void CompareWorkspace::refreshHistograms()
{
    if (!m_hist)
        return;
    const int n = m_engine.imageCount();
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

void CompareWorkspace::fitAll()
{
    double sharedScale = 1.0;
    bool first = true;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
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
        if (m_cellViews[i])
            m_cellViews[i]->setSelection(sel);
    }
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
        if (!m_cellViews[i])
            continue;
        const auto &ct = m_engine.cellTransform(i);
        const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
        const QPointF off = m_syncDrag ? QPointF(m_engine.syncTransform().offset.x,
                                                 m_engine.syncTransform().offset.y)
                                       : QPointF(ct.offset.x, ct.offset.y);
        m_cellViews[i]->setTransform(sc, off);
    }
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
        if (m_syncZoom)
        {
            m_engine.zoomAt(0.0, 0.0, factor);
        }
        else
        {
            const double oldScale = m_engine.cellTransform(idx).scale;
            m_engine.setCellScale(idx, std::clamp(oldScale * factor, 0.05, 50.0));
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
            m_dragging = false;
        return false;
    }

    return QWidget::eventFilter(obj, event);
}

void CompareWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitAll();
}
