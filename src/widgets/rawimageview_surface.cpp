// Viewport surface cache for RawImageView. Wheel notches (and the compare
// path that turns them into setTransform) must not rebuild a full offscreen
// surface on every event. A burst reuses the previous surface, scaled to the
// new transform, and rasterizes once after the burst settles.

#include "widgets/rawimageview.h"

#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{

constexpr qint64 kMaxCacheDim = 16384;
constexpr qint64 kMaxSurfacePixels = qint64(64) * 1024 * 1024; // 64M px
constexpr qint64 kCoalesceWindowMs = 16;
constexpr int kCoalesceSettleMs = 40;

qint64 surfaceNowMs()
{
    using namespace std::chrono;
    static const steady_clock::time_point kStart = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - kStart).count();
}

} // namespace

bool RawImageView::paintBaseSurface(QPainter &p)
{
    ensureBaseSurface();
    if (!m_baseSurfaceValid)
        return false;
    if (m_deferSurfaceRebuild)
        drawCoalescedSurface(p);
    else
        p.drawImage(rect(), m_baseSurface);
    return true;
}

void RawImageView::drawCoalescedSurface(QPainter &p) const
{
    const double cached = m_cachedScale;
    const double presented = presentedScale();
    if (!(cached > 0.0) || !std::isfinite(presented) || !std::isfinite(cached))
    {
        p.drawImage(rect(), m_baseSurface);
        return;
    }
    const double factor = presented / cached;
    const QPointF center(width() / 2.0, height() / 2.0);
    p.save();
    p.translate(center + m_offset - factor * m_cachedOffset);
    p.scale(factor, factor);
    p.translate(-center);
    p.drawImage(rect(), m_baseSurface);
    p.restore();
}

void RawImageView::scheduleSurfaceRebuild()
{
    if (!m_surfaceRebuildTimer)
    {
        m_surfaceRebuildTimer = new QTimer(this);
        m_surfaceRebuildTimer->setSingleShot(true);
        connect(m_surfaceRebuildTimer, &QTimer::timeout, this,
                [this]()
                {
                    m_deferSurfaceRebuild = false;
                    update();
                });
    }
    m_surfaceRebuildTimer->start(kCoalesceSettleMs);
}

void RawImageView::ensureBaseSurface()
{
    const qreal dpr = devicePixelRatioF();
    const QSize viewport = size();
    const QImage &image = presentationImage();
    const QRect sourceRect = renderSourceRect();
    const qint64 imageKey = image.isNull() ? -1 : image.cacheKey();
    const qint64 overlayKey = m_overlay.isNull() ? -1 : m_overlay.cacheKey();
    const double presented = presentedScale();
    const bool samePixels = imageKey == m_cachedImageKey && overlayKey == m_cachedOverlayKey &&
                            m_overlayAlpha == m_cachedOverlayAlpha &&
                            viewport == m_cachedViewport && dpr == m_cachedDpr &&
                            sourceRect == m_cachedSourceRect;
    const bool sameTransform = presented == m_cachedScale && m_offset == m_cachedOffset;
    if (m_baseSurfaceValid && samePixels && sameTransform)
    {
        m_deferSurfaceRebuild = false;
        return;
    }

    // Rapid zoom/pan notches: keep the previous allocation and approximate the
    // new transform by scaling that surface. One trailing rebuild catches up.
    const qint64 now = surfaceNowMs();
    if (m_baseSurfaceValid && samePixels && !sameTransform &&
        (now - m_lastSurfaceRebuildMs) < kCoalesceWindowMs)
    {
        m_deferSurfaceRebuild = true;
        scheduleSurfaceRebuild();
        return;
    }

    const int w = static_cast<int>(std::ceil(width() * static_cast<double>(dpr)));
    const int h = static_cast<int>(std::ceil(height() * static_cast<double>(dpr)));
    const qint64 pixels = static_cast<qint64>(w) * h;
    if (w <= 0 || h <= 0 || w > kMaxCacheDim || h > kMaxCacheDim || pixels > kMaxSurfacePixels)
    {
        releaseBaseSurface();
        return;
    }

    if (m_baseSurface.isNull() || m_baseSurface.width() != w || m_baseSurface.height() != h ||
        m_baseSurface.format() != QImage::Format_ARGB32_Premultiplied ||
        m_baseSurface.devicePixelRatio() != dpr)
    {
        m_baseSurface = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        m_baseSurface.setDevicePixelRatio(dpr);
        if (m_baseSurface.isNull())
        {
            releaseBaseSurface();
            return;
        }
    }

    m_baseSurface.fill(Qt::transparent);
    QPainter painter(&m_baseSurface);
    drawBaseLayer(painter);
    painter.end();

    m_baseSurfaceValid = true;
    m_deferSurfaceRebuild = false;
    m_cachedImageKey = imageKey;
    m_cachedOverlayKey = overlayKey;
    m_cachedOverlayAlpha = m_overlayAlpha;
    m_cachedScale = presented;
    m_cachedOffset = m_offset;
    m_cachedViewport = viewport;
    m_cachedDpr = dpr;
    m_cachedSourceRect = sourceRect;
    m_lastSurfaceRebuildMs = now;
    if (m_surfaceRebuildTimer)
        m_surfaceRebuildTimer->stop();

    ++m_baseSurfaceRenderCount;
    setProperty("baseSurfaceRenderCount",
                QVariant::fromValue<qulonglong>(m_baseSurfaceRenderCount));
}

void RawImageView::releaseBaseSurface()
{
    m_baseSurface = QImage();
    m_baseSurfaceValid = false;
    m_deferSurfaceRebuild = false;
    m_cachedImageKey = -1;
    m_cachedOverlayKey = -1;
    m_cachedOverlayAlpha = -1.0;
    m_cachedScale = 0.0;
    m_cachedOffset = {};
    m_cachedViewport = {};
    m_cachedDpr = 0.0;
    m_cachedSourceRect = {};
    if (m_surfaceRebuildTimer)
        m_surfaceRebuildTimer->stop();
}
