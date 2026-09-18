#pragma once

#include <QImage>
#include <QSize>

namespace mviewer::core::render_detail
{

// Bilinear scaler (scalar + optional SSE2 4-wide path). Used by RenderEngine::scaleQ.
QImage bilinearQ(const QImage &src, const QSize &target);

} // namespace mviewer::core::render_detail
