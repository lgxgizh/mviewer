#pragma once
// 内部 core 助手：ImageData <-> QImage 转换。
// 注意：本头文件包含 Qt，仅供 core 的 .cpp 内部包含，
// 绝不可被已去 Qt 化的公共头文件包含。
#include "core/image/ImageBuffer.h"
#include <QImage>

namespace mvcore {

// ImageData -> QImage（格式感知：Grayscale8/RGB24/RGBA32 各自映射）
QImage toQImage(const ImageData &src);

// QImage -> ImageData（格式感知：Grayscale8 保留为灰度，其余转 RGB24）
ImageData fromQImage(const QImage &src);

} // namespace mvcore
