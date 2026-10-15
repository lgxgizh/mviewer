#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include "domain/DecodeError.h"
#include "domain/Result.h"

#include <string>
#include <vector>

// Thin delegating shim over DecoderRegistry. Kept so existing callers keep
// compiling after the single Decoder was split into per-format decoders.
// All real decode logic now lives in core/image/decoder/*.
class Decoder
{
  public:
    // 全图解码
    static ImageData decodeFull(const std::string &path);

    // Modern Result-based decode returning rich error context when decode fails.
    static mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
    decodeFullResult(const std::string &path);
    static mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
    decodeFullResult(const std::string &path, mviewer::domain::ImageMetadata &outMeta);

    // 缩放到 maxEdge 以内的解码（保持比例）
    static ImageData decodeScaled(const std::string &path, int maxEdge);
    static ImageData decodeScaled(const std::string &path, int maxEdge,
                                  mviewer::domain::ImageMetadata &outMeta);

    // 全图解码并填充元数据（M6 扩展）
    static ImageData decodeFull(const std::string &path, mviewer::domain::ImageMetadata &outMeta);

    // 支持的图像后缀（与 UI 过滤保持一致）
    static std::vector<std::string> supportedExtensions();
};
