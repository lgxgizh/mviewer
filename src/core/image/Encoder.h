#pragma once

#include "ImageBuffer.h"

#include <string>
#include <vector>

// 图像编码器：封装 QImageWriter，支持
//  - JPEG（可调质量）
//  - PNG（可调压缩级别）
//  - BMP（无压缩）
//  - WebP（可调质量）
// 接口层只暴露 std 类型，QImage 仅作为内部实现细节。
class Encoder
{
public:
    struct Params {
        int quality = 90;          // JPEG/WebP 质量 (0-100)
        int pngCompression = 6;    // PNG 压缩级别 (0-9)
    };

    // 编码到文件
    static bool encode(const ImageData &img, const std::string &path, const Params &params = {});

    // 编码到内存缓冲区
    static std::vector<uint8_t> encodeToBuffer(const ImageData &img, const std::string &format, const Params &params = {});

    // 根据文件扩展名推断格式
    static std::string formatForExtension(const std::string &ext);

    // 支持的输出格式
    static std::vector<std::string> supportedOutputFormats();
};
