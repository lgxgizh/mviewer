#include "compareworkspace.h"

#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

// UI 边界转换：核心层 ImageFrame -> Qt QImage，复用 mvcore::toQImage。
QImage imageObjectToQImage(const ImageFrame *img) {
  if (!img)
    return QImage();
  return mvcore::toQImage(img->pixels());
}

} // namespace

CompareWorkspace::CompareWorkspace(QWidget *parent) : QWidget(parent) {
  m_engine = CompareEngine();

  m_syncZoomChk = new QCheckBox("同步缩放(&Z)", this);
  m_syncZoomChk->setChecked(true);
  m_syncDragChk = new QCheckBox("同步拖动(&D)", this);
  m_syncDragChk->setChecked(true);

  auto applySync = [this](bool) {
    if (!m_syncZoom || !m_syncDrag) {
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

  auto *syncBar = new QWidget(this);
  auto *syncLayout = new QHBoxLayout(syncBar);
  syncLayout->setContentsMargins(0, 0, 0, 0);
  syncLayout->setSpacing(12);
  syncLayout->addWidget(m_syncZoomChk);
  syncLayout->addWidget(m_syncDragChk);
  syncLayout->addStretch(1);

  m_grid = new QWidget(this);
  m_layout = new QGridLayout(m_grid);
  m_layout->setSpacing(2);
  m_layout->setContentsMargins(0, 0, 0, 0);

  QVBoxLayout *root = new QVBoxLayout(this);
  root->setContentsMargins(4, 4, 4, 4);
  root->setSpacing(4);
  root->addWidget(syncBar);
  root->addWidget(m_grid, 1);
}

void CompareWorkspace::setImages(const QStringList &paths) {
  std::vector<std::string> stdPaths;
  stdPaths.reserve(paths.size());
  for (const QString &p : paths)
    stdPaths.push_back(p.toStdString());
  m_engine.setImages(stdPaths);
  rebuildCells();
  fitAll();
  update();
}

bool CompareWorkspace::isSyncEnabled() const {
  return m_syncZoom && m_syncDrag;
}

void CompareWorkspace::setSyncEnabled(bool on) {
  m_syncZoom = on;
  m_syncDrag = on;
  m_syncZoomChk->setChecked(on);
  m_syncDragChk->setChecked(on);
  m_engine.setSyncEnabled(on);
}

void CompareWorkspace::rebuildCells() {
  QLayoutItem *item;
  while ((item = m_layout->takeAt(0))) {
    if (item->widget())
      delete item->widget();
    delete item;
  }
  m_cells.clear();
  m_stats.clear();

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

    const ImageFrame *img = m_engine.imageAt(i);
    if (img && !img->pixels().isNull()) {
      QImage q = imageObjectToQImage(img);
      m_stats.insert(i, AnalysisEngine::computeStats(mvcore::fromQImage(q)));
    }
  }
}

void CompareWorkspace::fitAll() {
  double sharedScale = 1.0;
  bool first = true;
  const int n = m_engine.imageCount();
  for (int i = 0; i < n; ++i) {
    const ImageFrame *img = m_engine.imageAt(i);
    const QSize qs = m_cells[i]->size();
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

void CompareWorkspace::paintEvent(QPaintEvent *) {
  const int n = m_engine.imageCount();
  for (int i = 0; i < n; ++i) {
    if (!m_cells[i])
      continue;
    const ImageFrame *img = m_engine.imageAt(i);
    if (!img)
      continue;
    QPixmap pm = QPixmap::fromImage(imageObjectToQImage(img));
    if (pm.isNull()) {
      m_cells[i]->setPixmap(QPixmap());
      continue;
    }
    const auto &ct = m_engine.cellTransform(i);
    double sc = m_syncZoom ? m_engine.syncTransform().scale : ct.scale;
    QPointF off = m_syncDrag ? QPointF(m_engine.syncTransform().offset.x,
                                       m_engine.syncTransform().offset.y)
                             : QPointF(ct.offset.x, ct.offset.y);
    const QSize cell = m_cells[i]->size();
    if (cell.width() <= 0 || cell.height() <= 0)
      continue;

    QPixmap scaled =
        pm.scaled(cell * sc, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap canvas(cell);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.drawPixmap((cell.width() - scaled.width()) / 2 + off.x(),
                 (cell.height() - scaled.height()) / 2 + off.y(), scaled);

    // 亮度直方图半透明叠加在右下角
    auto it = m_stats.find(i);
    if (it != m_stats.end())
      drawCellHistogram(p, cell, i);

    m_cells[i]->setPixmap(canvas);
  }
}

void CompareWorkspace::drawCellHistogram(QPainter &p, const QSize &cell,
                                         int index) {
  auto it = m_stats.find(index);
  if (it == m_stats.end())
    return;

  const int w = cell.width() / 4;
  const int h = cell.height() / 4;
  if (w <= 4 || h <= 4)
    return;
  const int margin = 6;
  const int x = cell.width() - w - margin;
  const int y = cell.height() - h - margin;

  // 小黑底
  p.save();
  p.setBrush(QColor(0, 0, 0, 150));
  p.setPen(Qt::NoPen);
  p.drawRect(x, y, w, h);

  const int *hist = it->histLum;
  int maxV = 1;
  for (int i = 0; i < 256; ++i)
    if (hist[i] > maxV)
      maxV = hist[i];

  // 白色半透明折线
  p.setPen(QColor(255, 255, 255, 180));
  p.setBrush(Qt::NoBrush);
  QPointF prev;
  for (int i = 0; i < 256; ++i) {
    const double px = x + static_cast<double>(i) / 255 * (w - 1);
    const double py = y + h - static_cast<double>(hist[i]) / maxV * (h - 1);
    const QPointF cur(px, py);
    if (i > 0)
      p.drawLine(prev, cur);
    prev = cur;
  }
  p.restore();
}

bool CompareWorkspace::eventFilter(QObject *obj, QEvent *event) {
  const int idx = m_cells.indexOf(static_cast<QLabel *>(obj));
  if (idx < 0)
    return QWidget::eventFilter(obj, event);

  if (event->type() == QEvent::Wheel) {
    auto *we = static_cast<QWheelEvent *>(event);
    const double factor = we->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    if (m_syncZoom) {
      m_engine.zoomAt(0.0, 0.0, factor);
    } else {
      const double oldScale = m_engine.cellTransform(idx).scale;
      m_engine.setCellScale(idx, std::clamp(oldScale * factor, 0.05, 50.0));
    }
    update();
    return true;
  }

  if (event->type() == QEvent::MouseButtonPress) {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::LeftButton) {
      m_dragging = true;
      m_lastMouse = me->pos();
      m_dragIdx = idx;
    }
    return false;
  }

  if (event->type() == QEvent::MouseMove) {
    auto *me = static_cast<QMouseEvent *>(event);
    if (m_dragging) {
      const QPoint delta = me->pos() - m_lastMouse;
      m_lastMouse = me->pos();
      if (m_syncDrag) {
        const Vec2 o = m_engine.syncTransform().offset;
        m_engine.setOffset(o.x + delta.x(), o.y + delta.y());
      } else {
        const Vec2 oldOff = m_engine.cellTransform(m_dragIdx).offset;
        m_engine.setCellOffset(m_dragIdx, oldOff.x + delta.x(),
                               oldOff.y + delta.y());
      }
      update();
    }
    return false;
  }

  if (event->type() == QEvent::MouseButtonRelease) {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::LeftButton)
      m_dragging = false;
    return false;
  }

  return QWidget::eventFilter(obj, event);
}

void CompareWorkspace::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  fitAll();
}
