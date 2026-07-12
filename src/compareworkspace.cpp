#include "compareworkspace.h"

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGridLayout>
#include <QLabel>
#include <QCheckBox>
#include <QVBoxLayout>

CompareWorkspace::CompareWorkspace(QWidget *parent)
    : QWidget(parent)
{
    m_engine = CompareEngine();
    m_syncChk = new QCheckBox("同步缩放/平移", this);
    m_syncChk->setChecked(true);
    connect(m_syncChk, &QCheckBox::toggled, this, [this](bool on) {
        m_engine.setSyncEnabled(on);
        if (!on) fitAll();
        emit syncToggled(on);
        update();
    });

    m_grid = new QWidget(this);
    m_layout = new QGridLayout(m_grid);
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    root->addWidget(m_syncChk);
    root->addWidget(m_grid, 1);
}

void CompareWorkspace::setImages(const QStringList &paths)
{
    m_engine.setImages(paths);
    rebuildCells();
    fitAll();
    update();
}

bool CompareWorkspace::isSyncEnabled() const
{
    return m_syncChk->isChecked();
}

void CompareWorkspace::setSyncEnabled(bool on)
{
    m_syncChk->setChecked(on);
}

void CompareWorkspace::rebuildCells()
{
    QLayoutItem *item;
    while ((item = m_layout->takeAt(0))) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_cells.clear();

    const int n = m_engine.imageCount();
    const auto &lay = m_engine.layout();
    for (int i = 0; i < n; ++i) {
        auto *lbl = new QLabel(m_grid);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("background-color: #222; border: 1px solid #555;");
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        lbl->setMouseTracking(true);
        lbl->installEventFilter(this);
        m_layout->addWidget(lbl, i / lay.cols, i % lay.cols);
        m_cells.push_back(lbl);
    }
}

void CompareWorkspace::fitAll()
{
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i) {
        const ImageObject *img = m_engine.imageAt(i);
        if (!img || !m_cells[i]) continue;
        QPixmap pm = QPixmap::fromImage(img->image());
        if (pm.isNull()) continue;
        const QSize cell = m_cells[i]->size();
        if (cell.width() <= 0 || cell.height() <= 0) continue;
        m_imageScale[i] = std::min(
            static_cast<double>(cell.width()) / pm.width(),
            static_cast<double>(cell.height()) / pm.height()) * 0.95;
        m_imageOffset[i] = QPointF(
            (cell.width() - pm.width() * m_imageScale[i]) / 2.0,
            (cell.height() - pm.height() * m_imageScale[i]) / 2.0);
    }
}

void CompareWorkspace::paintEvent(QPaintEvent *)
{
    const int n = m_engine.imageCount();
    const bool sync = m_engine.syncEnabled();
    for (int i = 0; i < n; ++i) {
        if (!m_cells[i]) continue;
        const ImageObject *img = m_engine.imageAt(i);
        if (!img) continue;
        QPixmap pm = QPixmap::fromImage(img->image());
        if (pm.isNull()) {
            m_cells[i]->setPixmap(QPixmap());
            continue;
        }
        double sc = sync ? m_engine.syncTransform().scale : m_imageScale.value(i, 1.0);
        QPointF off = sync ? m_engine.syncTransform().offset : m_imageOffset.value(i, QPointF());
        const QSize cell = m_cells[i]->size();
        if (cell.width() <= 0 || cell.height() <= 0) continue;

        QPixmap scaled = pm.scaled(cell * sc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap canvas(cell);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        p.drawPixmap((cell.width() - scaled.width()) / 2 + off.x(),
                     (cell.height() - scaled.height()) / 2 + off.y(), scaled);
        m_cells[i]->setPixmap(canvas);
    }
}

bool CompareWorkspace::eventFilter(QObject *obj, QEvent *event)
{
    const int idx = m_cells.indexOf(static_cast<QLabel*>(obj));
    if (idx < 0) return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent*>(event);
        const double factor = we->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        if (m_engine.syncEnabled()) {
            m_engine.zoomAt(QPointF(), factor);
        } else {
            double &sc = m_imageScale[idx];
            sc = std::clamp(sc * factor, 0.05, 50.0);
        }
        update();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void CompareWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitAll();
}
