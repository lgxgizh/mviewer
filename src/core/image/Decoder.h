#pragma once

#include "ImageBuffer.h"

#include <string>
#include <vector>

// 解码器：封装 QImageReader，支持
//  - 全分辨率解码
//  - 按目标尺寸缩小解码（缩略图/预览，省内存）
//  - 区域解码（后续做差异图/局部分析时用）
// 所有解码都在后台线程跑，这里不碰 UI。
// 注意：接口层只暴露 std 类型（ImageData），QImage 仅作为内部实现细节。
class Decoder
{
public:
    // 全图解码
    static ImageData decodeFull(const std::string& path);

    // 缩放到 maxEdge 以内的解码（保持比例）
    static ImageData decodeScaled(const std::string& path, int maxEdge);

    // 支持的图像后缀（与 UI 过滤保持一致）
    static std::vector<std::string> supportedExtensions();
};
