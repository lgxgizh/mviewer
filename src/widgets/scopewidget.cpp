#include "widgets/scopewidget.h"

#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace mviewer::ui
{

namespace
{
// A 4K frame would otherwise walk 12.5 M pixels on every repaint; scopes only
// need a representative sample of the distribution.
constexpr int kMaxWaveformRows = 240;
constexpr int kMaxSamples = 256;
constexpr int kScopeMargin = 6;

QColor scopeInk()
{
    return QColor(235, 235, 240);
}

int lumaOf(QRgb px)
{
    return (qRed(px) * 299 + qGreen(px) * 587 + qBlue(px) * 114) / 1000;
}

struct ColumnRange
{
    int lo = 255;
    int hi = 0;
};

// Per-column channel range over a bounded row sample. channel: 0=R 1=G 2=B 3=luma.
QVector<ColumnRange> columnRanges(const QImage &img, int channel)
{
    QVector<ColumnRange> out(img.width());
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return out;
    const int step = std::max(1, img.height() / kMaxWaveformRows);
    const QImage rgb = img.convertToFormat(QImage::Format_RGB32);
    for (int x = 0; x < rgb.width(); ++x)
    {
        ColumnRange range;
        for (int y = 0; y < rgb.height(); y += step)
        {
            const QRgb px = rgb.constScanLine(y)[x];
            int v = 0;
            if (channel == 0)
                v = qRed(px);
            else if (channel == 1)
                v = qGreen(px);
            else if (channel == 2)
                v = qBlue(px);
            else
                v = lumaOf(px);
            range.lo = std::min(range.lo, v);
            range.hi = std::max(range.hi, v);
        }
        out[x] = range;
    }
    return out;
}

// Grid: quarter lines plus the mid axis, drawn in a low-alpha ink.
void drawScopeGrid(QPainter &p, const QRectF &target, int divisions)
{
    p.save();
    QColor ink = scopeInk();
    ink.setAlpha(26);
    QPen grid(ink);
    grid.setCosmetic(true);
    p.setPen(grid);
    for (int i = 1; i < divisions; ++i)
    {
        const double x = target.left() + target.width() * static_cast<double>(i) / divisions;
        p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
    }
    p.drawLine(QPointF(target.left(), target.center().y()),
               QPointF(target.right(), target.center().y()));
    p.restore();
}
} // namespace

ScopeWidget::ScopeWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(120, 96);
}

QString ScopeWidget::modeLabel(Mode mode)
{
    switch (mode)
    {
    case Mode::LineProfile:
        return tr("Line Profile");
    case Mode::LumaWaveform:
        return tr("Luma Waveform");
    case Mode::RgbParade:
        return tr("RGB Parade");
    case Mode::Vectorscope:
        return tr("Vectorscope");
    }
    return {};
}

void ScopeWidget::setImage(const QImage &image)
{
    if (image.isNull())
    {
        m_image = QImage();
        update();
        return;
    }
    // Format_RGB32 keeps the per-pixel reads in the paint path branch-free.
    m_image = image.convertToFormat(QImage::Format_RGB32);
    update();
}

void ScopeWidget::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    update();
}

void ScopeWidget::setProfileLine(const QLineF &line)
{
    m_line = line;
    m_hasLine = true;
    update();
}

void ScopeWidget::clearProfileLine()
{
    m_hasLine = false;
    update();
}

QVector<QPoint> ScopeWidget::sampleLinePoints(int maxSamples) const
{
    QVector<QPoint> points;
    if (m_image.isNull() || !m_hasLine)
        return points;
    const double dx = m_line.x2() - m_line.x1();
    const double dy = m_line.y2() - m_line.y1();
    const double length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0.0))
        return points;
    const int count = std::clamp(static_cast<int>(std::ceil(length)), 2, maxSamples);
    points.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / (count - 1);
        const int x = static_cast<int>(std::lround(m_line.x1() + dx * t));
        const int y = static_cast<int>(std::lround(m_line.y1() + dy * t));
        if (x < 0 || y < 0 || x >= m_image.width() || y >= m_image.height())
            continue;
        points.append(QPoint(x, y));
    }
    return points;
}

void ScopeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 150));

    const QRectF target(kScopeMargin, kScopeMargin, std::max(1.0, width() - kScopeMargin * 2.0),
                        std::max(1.0, height() - kScopeMargin * 2.0));

    switch (m_mode)
    {
    case Mode::LineProfile:
        paintLineProfile(p, target);
        break;
    case Mode::LumaWaveform:
        paintWaveform(p, target, false);
        break;
    case Mode::RgbParade:
        paintWaveform(p, target, true);
        break;
    case Mode::Vectorscope:
        paintVectorscope(p, target);
        break;
    }
}

void ScopeWidget::paintLineProfile(QPainter &p, const QRectF &target) const
{
    drawScopeGrid(p, target, 4);
    const QVector<QPoint> points = sampleLinePoints(kMaxSamples);
    if (points.size() < 2)
    {
        p.setPen(scopeInk());
        p.drawText(target, Qt::AlignCenter, tr("Drag a line on the image"));
        return;
    }

    static const QColor kChans[4] = {QColor(255, 84, 84), QColor(84, 214, 96), QColor(84, 140, 255),
                                     QColor(225, 225, 225)};
    const int channels = 4;
    for (int c = 0; c < channels; ++c)
    {
        QPolygonF poly;
        poly.reserve(points.size());
        for (int i = 0; i < points.size(); ++i)
        {
            const QRgb px = m_image.constScanLine(points.at(i).y())[points.at(i).x()];
            int v = 0;
            if (c == 0)
                v = qRed(px);
            else if (c == 1)
                v = qGreen(px);
            else if (c == 2)
                v = qBlue(px);
            else
                v = lumaOf(px);
            const double x = target.left() + target.width() * static_cast<double>(i) /
                                                 static_cast<double>(points.size() - 1);
            const double y = target.bottom() - target.height() * (v / 255.0);
            poly.append(QPointF(x, y));
        }
        QPen pen(kChans[c]);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
    }

    int sum = 0;
    for (const QPoint &pt : points)
        sum += lumaOf(m_image.constScanLine(pt.y())[pt.x()]);
    p.setPen(scopeInk());
    p.drawText(target.left(), target.bottom() + 12,
               tr("%1 samples · mean luma %2").arg(points.size()).arg(sum / points.size()));
}

void ScopeWidget::paintWaveform(QPainter &p, const QRectF &target, bool parade) const
{
    const int lanes = parade ? 3 : 1;
    static const QColor kLane[3] = {QColor(255, 84, 84), QColor(84, 214, 96), QColor(84, 140, 255)};
    const double laneW = target.width() / lanes;
    for (int lane = 0; lane < lanes; ++lane)
    {
        const QRectF box(target.left() + laneW * lane, target.top(), laneW, target.height());
        drawScopeGrid(p, box, 4);
        const QVector<ColumnRange> ranges = columnRanges(m_image, parade ? lane : 3);
        if (ranges.isEmpty())
            continue;
        const double dx = box.width() / std::max(1, static_cast<int>(ranges.size()) - 1);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(parade ? kLane[lane] : scopeInk());
        for (int x = 0; x < ranges.size(); ++x)
        {
            const ColumnRange &range = ranges.at(x);
            const double y0 = box.bottom() - box.height() * (range.hi / 255.0);
            const double y1 = box.bottom() - box.height() * (range.lo / 255.0);
            p.fillRect(QRectF(box.left() + x * dx, y0, std::max(1.0, dx), std::max(1.0, y1 - y0)),
                       p.brush());
        }
        p.restore();
    }
}

void ScopeWidget::paintVectorscope(QPainter &p, const QRectF &target) const
{
    const double radius = std::min(target.width(), target.height()) * 0.42;
    const QPointF center = target.center();
    p.save();
    p.setPen(QPen(scopeInk(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, radius, radius);
    p.drawEllipse(center, radius * 0.5, radius * 0.5);
    p.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    p.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));
    p.restore();

    if (m_image.isNull())
        return;
    const QImage rgb = m_image;
    const int stepX = std::max(1, rgb.width() / 320);
    const int stepY = std::max(1, rgb.height() / 320);
    const double k = radius / 128.0;
    p.save();
    p.setPen(QPen(QColor(120, 200, 255), 1));
    for (int y = 0; y < rgb.height(); y += stepY)
    {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += stepX)
        {
            const QRgb px = line[x];
            const double u = -0.147 * qRed(px) - 0.289 * qGreen(px) + 0.436 * qBlue(px);
            const double v = 0.615 * qRed(px) - 0.515 * qGreen(px) - 0.100 * qBlue(px);
            p.drawPoint(QPointF(center.x() + u * k, center.y() - v * k));
        }
    }
    p.restore();
}

} // namespace mviewer::ui
