#include "rendercommandpainter.h"

#include "core/image/QtConvert.h"
#include "core/trace/Trace.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRect>

#include <algorithm>

namespace mviewer::ui
{
namespace
{
using mviewer::core::RenderCommand;
using mviewer::core::RenderCommandType;
using mviewer::core::RenderEngine;
using mviewer::core::RenderInterp;
using mviewer::core::RenderSize;

void paintDrawImage(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    if (cmd.srcImage.isNull() || !cmd.rect.isValid())
        return;
    RenderSize tgt = cmd.targetSize;
    if (!tgt.isValid())
        tgt = {cmd.rect.width, cmd.rect.height};
    const RenderInterp mode = static_cast<RenderInterp>(std::clamp(cmd.interp, 0, 3));
    ImageData scaled = RenderEngine::instance().scale(cmd.srcImage, tgt, mode);
    QImage q = mvcore::toQImage(scaled);
    if (q.isNull())
        return;
    painter.save();
    painter.setClipRect(viewport);
    painter.drawImage(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height), q);
    painter.restore();
}

void paintDrawOverlay(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    if (cmd.overlayImage.isNull() || !cmd.rect.isValid())
        return;
    RenderSize tgt = cmd.targetSize;
    if (!tgt.isValid())
        tgt = {cmd.rect.width, cmd.rect.height};
    const RenderInterp mode = static_cast<RenderInterp>(std::clamp(cmd.interp, 0, 3));
    ImageData scaled = RenderEngine::instance().scale(cmd.overlayImage, tgt, mode);
    QImage q = mvcore::toQImage(scaled);
    if (q.isNull())
        return;
    painter.save();
    painter.setClipRect(viewport);
    painter.setOpacity(std::clamp(cmd.alpha, 0.0, 1.0));
    painter.drawImage(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height), q);
    painter.restore();
}

void paintDrawSelection(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    if (!cmd.rect.isValid())
        return;
    painter.save();
    painter.setClipRect(viewport);
    QPen pen(QColor(static_cast<QRgb>(cmd.rgba)));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height));
    painter.restore();
}

void paintDrawHistogram(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    const QRect r(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height);
    if (!r.isValid() || cmd.histCount <= 0)
        return;
    painter.save();
    painter.setClipRect(viewport);
    // small black mat
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRect(r);
    int maxV = 1;
    const int n = std::min(cmd.histCount, 256);
    for (int i = 0; i < n; ++i)
        if (cmd.histData[i] > maxV)
            maxV = cmd.histData[i];
    // white translucent polyline
    painter.setPen(QColor(255, 255, 255, 180));
    painter.setBrush(Qt::NoBrush);
    QPointF prev;
    for (int i = 0; i < n; ++i)
    {
        const double px = r.x() + static_cast<double>(i) / 255 * (r.width() - 1);
        const double py =
            r.y() + r.height() - static_cast<double>(cmd.histData[i]) / maxV * (r.height() - 1);
        const QPointF cur(px, py);
        if (i > 0)
            painter.drawLine(prev, cur);
        prev = cur;
    }
    painter.restore();
}

void paintDrawPixelMarker(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    const int x = cmd.targetSize.width;
    const int y = cmd.targetSize.height;
    const unsigned packed = static_cast<unsigned>(cmd.rgba);
    const QColor penColor(static_cast<int>((packed >> 16) & 0xFFu),
                          static_cast<int>((packed >> 8) & 0xFFu),
                          static_cast<int>(packed & 0xFFu));
    painter.save();
    painter.setClipRect(viewport);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(penColor);
    pen.setWidth(1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    constexpr int kArm = 8;
    painter.drawLine(x - kArm, y, x + kArm, y);
    painter.drawLine(x, y - kArm, x, y + kArm);
    painter.restore();
}

void paintDrawHeatmap(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    QImage q = mvcore::toQImage(cmd.srcImage);
    if (q.isNull() || !cmd.rect.isValid())
        return;
    painter.save();
    painter.setClipRect(viewport);
    if (cmd.interp != 0)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(cmd.rect.x, cmd.rect.y, cmd.rect.width, cmd.rect.height), q);
    painter.restore();
}

} // namespace

void paintRenderCommand(QPainter &painter, const RenderCommand &cmd, const QRect &viewport)
{
    MV_TRACE_SCOPED("RenderEngine::executeCommand");
    switch (cmd.type)
    {
    case RenderCommandType::DrawImage:
        paintDrawImage(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawOverlay:
        paintDrawOverlay(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawSelection:
        paintDrawSelection(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawHistogram:
        paintDrawHistogram(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawHeatmap:
        paintDrawHeatmap(painter, cmd, viewport);
        break;
    case RenderCommandType::DrawPixelMarker:
        paintDrawPixelMarker(painter, cmd, viewport);
        break;
    default:
        break;
    }
}
} // namespace mviewer::ui
