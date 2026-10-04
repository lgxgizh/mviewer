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

qint64 surfaceImageKey(const QImage &image)
{
    return image.isNull() ? -1 : image.cacheKey();
}

struct SurfacePixelKey
{
    qint64 imageKey = -1;
    qint64 overlayKey = -1;
    double overlayAlpha = 0.0;
    QSize viewport;
    qreal dpr = 0.0;
    QRect sourceRect;
};

bool sameSurfacePixels(const SurfacePixelKey &current, const SurfacePixelKey &cached)
{
    return current.imageKey == cached.imageKey && current.overlayKey == cached.overlayKey &&
           current.overlayAlpha == cached.overlayAlpha && current.viewport == cached.viewport &&
           current.dpr == cached.dpr && current.sourceRect == cached.sourceRect;
}

bool sameSurfaceTransform(double presented, double cachedScale, const QPointF &offset,
                          const QPointF &cachedOffset)
{
    return presented == cachedScale && offset == cachedOffset;
}

bool surfaceExceedsBudget(int width, int height, qint64 pixels)
{
    return width <= 0 || height <= 0 || width > kMaxCacheDim || height > kMaxCacheDim ||
           pixels > kMaxSurfacePixels;
}

bool surfaceNeedsAllocation(const QImage &surface, int width, int height, qreal dpr)
{
    return surface.isNull() || surface.width() != width || surface.height() != height ||
           surface.format() != QImage::Format_ARGB32_Premultiplied ||
           surface.devicePixelRatio() != dpr;
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
    const qint64 imageKey = surfaceImageKey(image);
    const qint64 overlayKey = surfaceImageKey(m_overlay);
    const double presented = presentedScale();
    const SurfacePixelKey pixels{imageKey, overlayKey, m_overlayAlpha, viewport, dpr, sourceRect};
    const SurfacePixelKey cached{m_cachedImageKey, m_cachedOverlayKey, m_cachedOverlayAlpha,
                                 m_cachedViewport, m_cachedDpr,        m_cachedSourceRect};
    const bool samePixels = sameSurfacePixels(pixels, cached);
    const bool sameTransform =
        sameSurfaceTransform(presented, m_cachedScale, m_offset, m_cachedOffset);
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
    if (surfaceExceedsBudget(w, h, pixels))
    {
        releaseBaseSurface();
        return;
    }

    if (surfaceNeedsAllocation(m_baseSurface, w, h, dpr))
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
