#pragma once
// 内部 core 助手：ImageData <-> QImage 转换。
// 本头文件是 core 中**被点名允许包含 Qt 的适配头**之一（另一个是
// QtMetadataSemantics.h，见 docs/adr/011 与 architecture_gate.ps1 的 R5）。
// 它可以被 core 的 .cpp 与 UI 的私有头（*_p.h）包含；除此之外的 core/domain
// 头文件必须保持不含 Qt include。
#include "core/image/DisplayColorContext.h"
#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <QImage>

namespace mvcore
{

// ImageData -> QImage（格式感知：Grayscale8/RGB24/RGBA32 各自映射）
QImage toQImage(const ImageData &src);

// M28 P1-03: NON-OWNING QImage view over the ImageData buffer where the byte
// order matches a Qt format (RGB24->RGB888, BGR24->BGR888, BGRA32->ARGB32,
// Grayscale8->Grayscale8). Zero copy — the caller must keep `src` alive while
// the returned QImage is used (Qt never frees the referenced data). Returns a
// null QImage for formats whose byte order does not map (RGBA32); callers
// must fall back to toQImage().
QImage toQImageRef(const ImageData &src);

// Materialize a display-only sRGB copy from analysis-domain pixels. Embedded
// ICC is applied to the copy; the source ImageData bytes are never modified.
QImage toDisplayQImage(const ImageData &src, const mviewer::domain::ImageMetadata &meta);

// Materialize a display copy for an explicit presentation target.  The source
// profile is converted directly to the target (never source -> sRGB -> target)
// and the source ImageData remains untouched.
QImage toDisplayQImage(const ImageData &src, const mviewer::domain::ImageMetadata &meta,
                       const mviewer::core::DisplayColorContext &target);

// Materialize the same display transform into an ImageData tile. This keeps
// CPU QPainter and GPU upload paths byte-equivalent while preserving the
// source ImageFrame as analysis-domain data.
ImageData toDisplayImageData(const ImageData &src, const mviewer::domain::ImageMetadata &meta);

ImageData toDisplayImageData(const ImageData &src, const mviewer::domain::ImageMetadata &meta,
                             const mviewer::core::DisplayColorContext &target);

// QImage -> ImageData（格式感知：Grayscale8 保留为灰度，其余转 RGB24）
ImageData fromQImage(const QImage &src);

} // namespace mvcore
