#pragma once

// UI render adapter: composites a core RenderCommand onto a QPainter. This is
// the Qt half of the RenderCommand pipeline; core/render/RenderEngine.h stays
// Qt-free (ADR-016). Scaling still goes through RenderEngine's active backend.

#include "core/render/RenderEngine.h"

class QPainter;
class QRect;

namespace mviewer::ui
{
// Draws one command onto `painter`, clipped to `viewport` (the cell's local
// rectangle). SmoothPixmapTransform is enabled for heatmaps when
// cmd.interp != 0. Intended for the UI thread (caller owns the painter).
void paintRenderCommand(QPainter &painter, const mviewer::core::RenderCommand &cmd,
                        const QRect &viewport);
} // namespace mviewer::ui
