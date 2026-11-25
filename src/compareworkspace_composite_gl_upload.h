#pragma once

#include <QImage>

class QOpenGLFunctions;

namespace mviewer::ui
{

// Normalize RGB888 to Qt-owned, four-byte aligned rows before uploading.
// Preserve GL_UNPACK_ALIGNMENT and GL_UNPACK_ROW_LENGTH exactly. Invalid images
// fail before any GL state change; a valid current desktop GL context is required.
bool uploadCompareRgb888(QOpenGLFunctions *gl, unsigned int tex, const QImage &image);

} // namespace mviewer::ui
