#pragma once

#include "core/image/ISourceImageCapabilities.h"
#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// ADR 033 / ADR 034: headerless .raw sensor dumps and .gray/.grey grayscale.
// Returns no pixels when the parameters cannot be resolved (thumbnail
// placeholder + dialog hint).
// Probe does not read samples. Native LOD and region are false; decodeRegion
// still returns pixels (one cached full frame, then crop and nearest scale)
// because SourceImage calls it whenever this interface is present.
class PlainRawDecoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
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

    bool canProbe(const std::string &path) const override;
    bool probeMetadata(const std::string &path,
                       mviewer::domain::ImageMetadata &meta) const override;
    bool canNativeLod(const std::string &path) const override;
    bool canNativeRegion(const std::string &path) const override;
    ImageData decodeLod(const std::string &path, int maxEdge,
                        mviewer::domain::ImageMetadata &meta) const override;
    ImageData decodeRegion(const std::string &path, int x, int y, int w, int h, int targetW,
                           int targetH, mviewer::domain::ImageMetadata &meta) const override;
};
