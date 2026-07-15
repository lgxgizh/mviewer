#include "compareworkspace.h"
#include "widgets/rawimageview.h"

#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace
{

// UI 边界转换：核心层 ImageFrame -> Qt QImage，复用 mvcore::toQImage。
QImage imageObjectToQImage(const ImageFrame* img)
{
    if (!img)
        return QImage();
    return mvcore::toQImage(img->pixels());
}

} // namespace

CompareWorkspace::CompareWorkspace(QWidget* parent)
    : QWidget(parent)
{
    m_engine = CompareEngine();

    m_syncZoomChk = new QCheckBox("同步缩放(&Z)", this);
    m_syncZoomChk->setChecked(true);
    m_syncDragChk = new QCheckBox("同步拖动(&D)", this);
    m_syncDragChk->setChecked(true);

    auto applySync = [this](bool) {
        if (!m_syncZoom || !m_syncDrag)
        {
            // 关闭任一同步时,用当前 fit 结果初始化每张图的独立变换
            fitAll();
        }
        update();
    };
    connect(m_syncZoomChk, &QCheckBox::toggled, this, [this, applySync](bool on) {
        m_syncZoom = on;
        m_engine.setSyncEnabled(m_syncZoom && m_syncDrag);
        applySync(on);
    });
    connect(m_syncDragChk, &QCheckBox::toggled, this, [this, applySync](bool on) {
        m_syncDrag = on;
        m_engine.setSyncEnabled(m_syncZoom && m_syncDrag);
        applySync(on);
    });

    auto* syncBar = new QWidget(this);
    auto* syncLayout = new QHBoxLayout(syncBar);
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
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(false);
    scroll->setWidget(m_grid);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addWidget(syncBar);
    root->addWidget(scroll, 1);
}

void CompareWorkspace::setImages(const QStringList& paths)
{
    std::vector<std::string> stdPaths;
    stdPaths.reserve(paths.size());
    for (const QString& p : paths)
        stdPaths.push_back(p.toStdString());
    m_engine.setImages(stdPaths);
    rebuildCells();
    fitAll();
    update();
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
    QLayoutItem* item;
    while ((item = m_layout->takeAt(0)))
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_cellLabels.clear();
    m_cellViews.clear();
    m_stats.clear();

    const int n = m_engine.imageCount();
    const auto& lay = m_engine.layout();
    for (int i = 0; i < n; ++i)
    {
        // Each cell: a RawImageView for the image + a QLabel caption below
        auto* cellWidget = new QWidget(m_grid);
        auto* cellLay = new QVBoxLayout(cellWidget);
        cellLay->setContentsMargins(0, 0, 0, 0);
        cellLay->setSpacing(1);

        auto* view = new RawImageView(cellWidget);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        view->setMinimumSize(64, 64);
        view->setMouseTracking(true);
        view->installEventFilter(this);
        view->setCellIndex(i);
        cellLay->addWidget(view, 1);
        m_cellViews.push_back(view);

        const ImageFrame* img = m_engine.imageAt(i);
        if (img && !img->pixels().isNull())
        {
            QImage q = imageObjectToQImage(img);
            view->setImage(q);
            m_stats.insert(i, AnalysisEngine::computeStats(mvcore::fromQImage(q)));
        }

        const QString cellName = img ? QString::fromStdString(img->metadata().fileName) : QString();
        connect(view, &RawImageView::pixelInfo, this,
                [this, cellName](int x, int y, int r, int g, int b, bool valid) {
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
                [this](const mviewer::domain::Selection& sel) {
                    applySelectionToAll(sel);
                });

        // Caption label
        auto* caption = new QLabel(cellWidget);
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

void CompareWorkspace::fitAll()
{
    double sharedScale = 1.0;
    bool first = true;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        const ImageFrame* img = m_engine.imageAt(i);
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

void CompareWorkspace::applySelectionToAll(const mviewer::domain::Selection& sel)
{
    // Engine owns the frames; it mirrors the synchronized ROI to every ImageFrame.
    m_engine.applySelectionToAll(sel);
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (m_cellViews[i])
            m_cellViews[i]->setSelection(sel);
    }
    update();
}

void CompareWorkspace::paintEvent(QPaintEvent*)
{
    const int n = m_engine.imageCount();
    const bool multi = n > 1;
    for (int i = 0; i < n; ++i)
    {
        if (!m_cellViews[i])
            continue;
        const ImageFrame* img = m_engine.imageAt(i);
        if (!img)
            continue;
        QPixmap pm = QPixmap::fromImage(imageObjectToQImage(img));
        if (pm.isNull())
        {
            m_cellViews[i]->setImage(QImage());
            continue;
        }
        const QSize cell = m_cellViews[i]->size();
        if (cell.width() <= 0 || cell.height() <= 0)
            continue;

        const auto& ct = m_engine.cellTransform(i);
        const double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
        const QPointF off = m_syncDrag
            ? QPointF(m_engine.syncTransform().offset.x, m_engine.syncTransform().offset.y)
            : QPointF(ct.offset.x, ct.offset.y);

        const QSize target = pm.size().scaled(cell * sc, Qt::KeepAspectRatio);
        const int dx = (cell.width() - target.width()) / 2 + static_cast<int>(off.x());
        const int dy = (cell.height() - target.height()) / 2 + static_cast<int>(off.y());
        const RenderRect dest{dx, dy, target.width(), target.height()};
        const QRect viewport(0, 0, cell.width(), cell.height());

        QPixmap canvas(cell);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);

        // 1) base image
        RenderCommand imgCmd = RenderCommand::drawImage(
            img->pixels(), RenderSize{target.width(), target.height()}, RenderInterp::Bilinear);
        imgCmd.rect = dest;
        imgCmd.interp = static_cast<int>(RenderInterp::Bilinear);
        RenderEngine::instance().executeCommand(p, imgCmd, viewport);

        // 2) difference overlay (compare mode)
        if (multi)
        {
            ImageData diff = m_engine.differenceMap(i);
            if (!diff.isNull())
            {
                RenderCommand ovCmd = RenderCommand::drawOverlay(diff, 0.5);
                ovCmd.rect = dest;
                ovCmd.interp = static_cast<int>(RenderInterp::Bilinear);
                RenderEngine::instance().executeCommand(p, ovCmd, viewport);
            }
        }

        // 3) ROI selection (image coords -> viewport coords)
        const auto& sel = img->selection();
        if (!sel.isEmpty())
        {
            const double sx = target.width() / static_cast<double>(pm.width());
            const double sy = target.height() / static_cast<double>(pm.height());
            const RenderRect srect{
                dest.x + static_cast<int>(sel.x * sx),
                dest.y + static_cast<int>(sel.y * sy),
                static_cast<int>(sel.width * sx),
                static_cast<int>(sel.height * sy)};
            RenderCommand selCmd = RenderCommand::drawSelection(srect, 0xFFFF0000);
            RenderEngine::instance().executeCommand(p, selCmd, viewport);
        }

        // 4) luminance histogram (bottom-right overlay)
        auto it = m_stats.find(i);
        if (it != m_stats.end())
        {
            const int w = cell.width() / 4;
            const int h = cell.height() / 4;
            if (w > 4 && h > 4)
            {
                const int margin = 6;
                const int x = cell.width() - w - margin;
                const int y = cell.height() - h - margin;
                RenderCommand histCmd =
                    RenderCommand::drawHistogram(it->histLum, 256, RenderRect{x, y, w, h});
                RenderEngine::instance().executeCommand(p, histCmd, viewport);
            }
        }

        // We don't setPixmap on RawImageView (it paints itself); instead
        // we just update the view's transform to reflect sync state.
        m_cellViews[i]->setTransform(sc, off);
    }
}

bool CompareWorkspace::eventFilter(QObject* obj, QEvent* event)
{
    const int idx = m_cellViews.indexOf(static_cast<RawImageView*>(obj));
    if (idx < 0)
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel)
    {
        auto* we = static_cast<QWheelEvent*>(event);
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
        auto* me = static_cast<QMouseEvent*>(event);
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
        auto* me = static_cast<QMouseEvent*>(event);
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
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton)
            m_dragging = false;
        return false;
    }

    return QWidget::eventFilter(obj, event);
}

void CompareWorkspace::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    fitAll();
}
