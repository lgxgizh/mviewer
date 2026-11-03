#pragma once

#include "core/image/ISourceImageCapabilities.h"
#include "core/image/decoder/IDecoder.h"
#include "core/image/yuv/YuvSource.h"

#include <string>
#include <vector>

// Raw .yuv / .y4m decoder (ADR 032). Returns no pixels when the frame
// parameters cannot be resolved. Header is Qt-free.
class YuvDecoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
    // Key for YuvParamStore. UI uses the same function.
    static std::string paramKey(const std::string &path);

    bool canDecode(const std::string &path) const override;
    ImageData decodeFull(const std::string &path) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge,
                           mviewer::domain::ImageMetadata &outMeta) const override;
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override;
    mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
    decodeFullResult(const std::string &path,
                     mviewer::domain::ImageMetadata &outMeta) const override;
    std::vector<std::string> extensions() const override;
    const char *name() const override;

    bool canProbe(const std::string &path) const override;
    bool probeMetadata(const std::string &path,
                       mviewer::domain::ImageMetadata &meta) const override;
    bool canNativeLod(const std::string &path) const override;
    bool canNativeRegion(const std::string &path) const override;
    ImageData decodeLod(const std::string &path, int maxEdge,
                        mviewer::domain::ImageMetadata &meta) const override;
};
