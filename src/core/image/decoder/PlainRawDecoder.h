#pragma once

#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// ADR 033: headerless .raw sensor dumps. Returns no pixels when the
// parameters cannot be resolved (thumbnail placeholder + dialog hint).
class PlainRawDecoder : public IDecoder
{
  public:
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
};
