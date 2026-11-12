#pragma once

// Device-pixel fit shared by the viewer LOD worker and neighbor display warm.
// Not an ImageViewer API. Include only from imageviewer translation units.

#include "core/image/QtConvert.h"
#include "core/render/DisplayResample.h"
#include "core/render/Viewport.h"

#include <QImage>

#include <algorithm>
#include <atomic>
#include <cmath>

inline constexpr int kMinLodEdge = 64;
inline constexpr int kMaxLodEdge = 4096;
inline constexpr double kMaxRasterPixels = 8.0 * 1000 * 1000;

// Viewport LOD when a full frame is worth avoiding. 8-bit (unknown depth that
// is not RAW counts as 8-bit) at or below 4 MP stays a full-frame fast path.
// 16-bit and RAW stay on that path only at or below 1 MP.
inline bool shouldUseDisplayLod(const mviewer::domain::ImageMetadata &meta)
{
    if (meta.width <= 0 || meta.height <= 0)
        return false;
    const qint64 pixels = static_cast<qint64>(meta.width) * static_cast<qint64>(meta.height);
    const bool raw = meta.rawSource != mviewer::domain::RawSource::NotRaw || meta.format == "RAW";
    constexpr qint64 kHeavyFastPathPixels = 1 * 1000 * 1000;
    constexpr qint64 kSmallFastPathPixels = 4 * 1000 * 1000;
    if (meta.bitDepth > 8 || raw)
        return pixels > kHeavyFastPathPixels;
    return pixels > kSmallFastPathPixels;
}

// Device px of a source rect at `density` (device px / source px), LOD-capped.
inline void devicePixelsForSource(int sourceW, int sourceH, double density, int &tw, int &th)
{
    tw = 1;
    th = 1;
    if (sourceW <= 0 || sourceH <= 0 || !(density > 0.0) || !std::isfinite(density))
        return;
    double w = static_cast<double>(sourceW) * density;
    double h = static_cast<double>(sourceH) * density;
    const double edge = std::max(w, h);
    if (edge > static_cast<double>(kMaxLodEdge))
    {
        const double scale = static_cast<double>(kMaxLodEdge) / edge;
        w *= scale;
        h *= scale;
    }
    if (w * h > kMaxRasterPixels)
    {
        const double scale = std::sqrt(kMaxRasterPixels / (w * h));
        w *= scale;
        h *= scale;
    }
    tw = std::max(1, static_cast<int>(std::lround(w)));
    th = std::max(1, static_cast<int>(std::lround(h)));
}

// Color-manage first. Sharp's sRGB EOTF must see a display-referred raster.
inline QImage sharpFitDisplayImage(const ImageData &pixels,
                                   const mviewer::domain::ImageMetadata &meta,
                                   const mviewer::core::DisplayColorContext &target, int tw, int th,
                                   const std::atomic<bool> *cancel)
{
    const ImageData display = mvcore::toDisplayImageData(pixels, meta, target);
    if (display.isNull())
        return QImage();
    ImageData fitted = display;
    if (tw >= 1 && th >= 1 && (display.width != tw || display.height != th))
    {
        thread_local mviewer::core::DisplayResampleScratch scratch;
        mviewer::core::DisplayResampleRequest resample;
        resample.source = {0, 0, display.width, display.height};
        resample.targetWidth = tw;
        resample.targetHeight = th;
        resample.quality = mviewer::core::DisplayResampleQuality::Sharp;
        fitted = mviewer::core::resampleDisplay(display, resample, &scratch, cancel);
        if (fitted.isNull())
            return QImage();
    }
    QImage out = mvcore::toQImage(fitted);
    mvcore::tagDisplayQImage(out, meta, target);
    return out;
}

// UI-thread snapshot of the view the neighbor will open into.
// lockedScale > 0 keeps that scale; otherwise the neighbor is fit to the widget.
struct WarmDisplayLodRequest
{
    int sourceW = 0;
    int sourceH = 0;
    int widgetW = 0;
    int widgetH = 0;
    double dpr = 1.0;
    double lockedScale = 0.0;
    bool maximizeFit = false;
    int fallbackEdge = 1024;
};

// sharp targets exact device pixels. Otherwise maxEdge is a bounded LOD.
struct WarmDisplayLodPlan
{
    int maxEdge = 1024;
    int exactW = 0;
    int exactH = 0;
    bool sharp = false;
};

// Zoomed-out fit matches the settled viewer path: exact device pixels, no overscan.
// A missing widget, or density >= 1, stays a bounded LOD and does not invent a Sharp size.
inline WarmDisplayLodPlan planWarmDisplayLod(const WarmDisplayLodRequest &request)
{
    WarmDisplayLodPlan plan;
    plan.maxEdge = std::max(kMinLodEdge, std::min(request.fallbackEdge, kMaxLodEdge));
    if (request.sourceW <= 0 || request.sourceH <= 0 || request.widgetW <= 0 ||
        request.widgetH <= 0)
        return plan;

    double scale = request.lockedScale;
    if (!(scale > 0.0) || !std::isfinite(scale))
    {
        Viewport fitted;
        fitted.screenW = request.widgetW;
        fitted.screenH = request.widgetH;
        FitPolicy policy = FitPolicy::Comfortable;
        if (request.maximizeFit)
            policy = FitPolicy::MaximizeClient;
        fitted.fit(request.sourceW, request.sourceH, policy);
        scale = fitted.scale;
    }

    const double ratio = std::isfinite(request.dpr) ? std::max(1.0, request.dpr) : 1.0;
    const double density = scale * ratio;
    if (!(density > 0.0) || !std::isfinite(density) || !(density < 1.0))
    {
        if (std::isfinite(density) && density >= 1.0)
        {
            const int longEdge = std::max(request.widgetW, request.widgetH);
            const double edge = static_cast<double>(longEdge) * ratio;
            const double capped = std::min(edge, static_cast<double>(kMaxLodEdge));
            plan.maxEdge = std::max(kMinLodEdge, static_cast<int>(std::ceil(capped)));
        }
        return plan;
    }

    devicePixelsForSource(request.sourceW, request.sourceH, density, plan.exactW, plan.exactH);
    const int wantEdge = std::max(plan.exactW, plan.exactH);
    plan.maxEdge = std::max(kMinLodEdge, std::min(wantEdge, kMaxLodEdge));
    plan.sharp = plan.exactW >= 1 && plan.exactH >= 1;
    return plan;
}
