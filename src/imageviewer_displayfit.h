#pragma once

// Device-pixel fit shared by the viewer LOD worker and neighbor display warm.
// Not an ImageViewer API. Include only from imageviewer translation units.

#include "core/image/QtConvert.h"
#include "core/render/DisplayResample.h"
#include "core/render/Viewport.h"

#include <QImage>
#include <QRect>

#include <algorithm>
#include <atomic>
#include <cmath>

inline constexpr int kMinLodEdge = 64;
inline constexpr int kMaxLodEdge = 4096;
inline constexpr double kMaxRasterPixels = 8.0 * 1000 * 1000;
// Zoomed-in regions keep this density margin. Zoomed-out Sharp uses paint dest.
inline constexpr double kRasterOverscan = 1.25;

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

// Same dpr treatment as ImageViewer::drawDisplayRaster.
inline double paintDevicePixelRatio(double dpr)
{
    return (dpr > 1.0 && std::isfinite(dpr)) ? dpr : 1.0;
}

// Inputs for planPaintDisplayDest. haveView uses `view` as-is (current pan/zoom);
// otherwise Comfortable/MaximizeClient fit, or a centered lockedScale.
struct DisplayPaintDestRequest
{
    int sourceX = 0;
    int sourceY = 0;
    int sourceW = 0;
    int sourceH = 0;
    int widgetW = 0;
    int widgetH = 0;
    double dpr = 1.0;
    bool maximizeFit = false;
    double lockedScale = 0.0;
    bool haveView = false;
    Viewport view;
};

// Device-pixel dest drawDisplayRaster will blit: Viewport fit (or supplied view),
// imageRectToScreen (snapScreenEdge floor), then lround(sw * dpr).
struct DisplayPaintDest
{
    int width = 0;
    int height = 0;
    double scale = 0.0;
    double density = 0.0;
    bool valid = false;
};

inline DisplayPaintDest planPaintDisplayDest(const DisplayPaintDestRequest &request)
{
    DisplayPaintDest dest;
    if (request.sourceW <= 0 || request.sourceH <= 0)
        return dest;

    Viewport view;
    if (request.haveView)
    {
        view = request.view;
    }
    else
    {
        if (request.widgetW <= 0 || request.widgetH <= 0)
            return dest;
        view.screenW = request.widgetW;
        view.screenH = request.widgetH;
        if (request.lockedScale > 0.0 && std::isfinite(request.lockedScale))
        {
            view.scale = request.lockedScale;
            view.offsetX = (static_cast<double>(view.screenW) -
                            static_cast<double>(request.sourceW) * view.scale) /
                           2.0;
            view.offsetY = (static_cast<double>(view.screenH) -
                            static_cast<double>(request.sourceH) * view.scale) /
                           2.0;
        }
        else
        {
            const FitPolicy policy =
                request.maximizeFit ? FitPolicy::MaximizeClient : FitPolicy::Comfortable;
            view.fit(request.sourceW, request.sourceH, policy);
        }
    }

    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    view.imageRectToScreen(request.sourceX, request.sourceY, request.sourceW, request.sourceH, sx,
                           sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return dest;
    const double ratio = paintDevicePixelRatio(request.dpr);
    dest.width = std::max(1, static_cast<int>(std::lround(static_cast<double>(sw) * ratio)));
    dest.height = std::max(1, static_cast<int>(std::lround(static_cast<double>(sh) * ratio)));
    dest.scale = view.scale;
    dest.density = dest.scale * ratio;
    dest.valid = dest.width >= 1 && dest.height >= 1 && dest.scale > 0.0 &&
                 std::isfinite(dest.scale) && std::isfinite(dest.density);
    return dest;
}

inline DisplayPaintDest paintDestForView(const Viewport &view, const QRect &source, double dpr)
{
    DisplayPaintDestRequest destReq;
    destReq.sourceX = source.x();
    destReq.sourceY = source.y();
    destReq.sourceW = source.width();
    destReq.sourceH = source.height();
    destReq.dpr = dpr;
    destReq.haveView = true;
    destReq.view = view;
    return planPaintDisplayDest(destReq);
}

inline int fallbackLodMaxEdge(int widgetW, int widgetH, double dpr)
{
    const double ratio = paintDevicePixelRatio(dpr);
    const int edge = static_cast<int>(
        std::ceil(std::max(static_cast<double>(widgetW), static_cast<double>(widgetH)) * ratio *
                  kRasterOverscan));
    return std::max(kMinLodEdge, std::min(edge, kMaxLodEdge));
}

inline void applyZoomedOutSharpPlan(const DisplayPaintDestRequest &destReq, int &maxEdge,
                                    bool &sharpFit, int &exactW, int &exactH)
{
    const DisplayPaintDest dest = planPaintDisplayDest(destReq);
    if (!dest.valid || !(dest.density > 0.0) || !std::isfinite(dest.density) ||
        !(dest.density < 1.0))
        return;
    sharpFit = true;
    exactW = dest.width;
    exactH = dest.height;
    maxEdge = std::max(kMinLodEdge, std::min(std::max(dest.width, dest.height), kMaxLodEdge));
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

// Zoomed-out fit matches the settled viewer path: exact paint dest, no overscan.
// A missing widget, or density >= 1, stays a bounded LOD and does not invent a Sharp size.
inline WarmDisplayLodPlan planWarmDisplayLod(const WarmDisplayLodRequest &request)
{
    WarmDisplayLodPlan plan;
    plan.maxEdge = std::max(kMinLodEdge, std::min(request.fallbackEdge, kMaxLodEdge));
    if (request.sourceW <= 0 || request.sourceH <= 0 || request.widgetW <= 0 ||
        request.widgetH <= 0)
        return plan;

    DisplayPaintDestRequest destReq;
    destReq.sourceW = request.sourceW;
    destReq.sourceH = request.sourceH;
    destReq.widgetW = request.widgetW;
    destReq.widgetH = request.widgetH;
    destReq.dpr = request.dpr;
    destReq.maximizeFit = request.maximizeFit;
    destReq.lockedScale = request.lockedScale;
    const DisplayPaintDest dest = planPaintDisplayDest(destReq);
    if (!dest.valid)
        return plan;

    const double density = dest.density;
    if (!(density > 0.0) || !std::isfinite(density) || !(density < 1.0))
    {
        if (std::isfinite(density) && density >= 1.0)
        {
            const int longEdge = std::max(request.widgetW, request.widgetH);
            const double edge = static_cast<double>(longEdge) * paintDevicePixelRatio(request.dpr);
            const double capped = std::min(edge, static_cast<double>(kMaxLodEdge));
            plan.maxEdge = std::max(kMinLodEdge, static_cast<int>(std::ceil(capped)));
        }
        return plan;
    }

    plan.exactW = dest.width;
    plan.exactH = dest.height;
    const int wantEdge = std::max(plan.exactW, plan.exactH);
    plan.maxEdge = std::max(kMinLodEdge, std::min(wantEdge, kMaxLodEdge));
    plan.sharp = plan.exactW >= 1 && plan.exactH >= 1;
    return plan;
}
