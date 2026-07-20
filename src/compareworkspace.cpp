#include "compareworkspace.h"
#include "widgets/rawimageview.h"

#include "core/EventBus.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWheelEvent>

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

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addWidget(syncBar);
    root->addWidget(scroll, 1);
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
    const ImageData heat = DifferenceEngine::heatMap(diff);
    if (heat.isNull())
        return;
    m_cellViews[res.index]->setOverlay(mvcore::toQImage(heat), 0.5);
    update();
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
