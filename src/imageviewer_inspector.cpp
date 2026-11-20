// Pixel Inspector hover readout. Split out of imageviewer.cpp so the viewer's
// core TU stays inside its 800-line budget (ADR-014).
#include "imageviewer.h"

#include <cmath>

void ImageViewer::updatePixelSampleAt(const QPoint &pos)
{
    // Pixel Inspector reads the full frame when one exists. Above the analysis
    // budget the bounded display raster supplies hover, ROI, and color samples.
    int ix = -1, iy = -1;
    if (hasDisplayImage() && m_view.scale > 0.0)
    {
        m_view.screenW = width();
        m_view.screenH = height();
        ix = static_cast<int>(std::floor((pos.x() - m_view.offsetX) / m_view.scale));
        iy = static_cast<int>(std::floor((pos.y() - m_view.offsetY) / m_view.scale));
    }
    if (ix == m_lastHoverX && iy == m_lastHoverY)
        return;
    m_lastHoverX = ix;
    m_lastHoverY = iy;

    const PixelRGBA px = sampleAnalysisPixel(ix, iy);
    m_lastHoverPixel = px;
    int r16 = 0, g16 = 0, b16 = 0, rawKind = 0;
    if (m_frame && px.valid)
    {
        const auto &meta = m_frame->metadata();
        if (meta.format == "RAW")
            rawKind = 1;
        else if (m_frame->hasRaw16())
        {
            uint16_t vr = 0, vg = 0, vb = 0;
            if (m_frame->raw16At(ix, iy, vr, vg, vb))
            {
                r16 = vr;
                g16 = vg;
                b16 = vb;
                rawKind = 2;
            }
        }
    }
    emit pixelInfo(ix, iy, px.r, px.g, px.b, px.valid ? px.a : 255, r16, g16, b16, rawKind,
                   px.valid);
}
