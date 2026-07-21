#pragma once

#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// P6: RAW pixel decoder.
//
// Most camera RAW containers (CR2/CR3/NEF/NRW/ARW/DNG/ORF/RW2/PEF/RAF/SRW and
// friends) embed a full or large JPEG preview inside the file. We extract that
// preview and decode it, yielding a real, displayable image WITHOUT pulling in
// a heavy external RAW library (libraw / RawSpeed) — license + build-complexity
// risk avoided per the M14 RFC Phase A ("best-effort display").
//
// Graceful by design: if no usable preview is found, decode*() returns an empty
// ImageData so the registry falls through to the next decoder instead of
// crashing. The full demosaic pipeline (Stage B) is explicitly deferred.
//
// Header is Qt-free; the .cpp may use Qt internally.
class RawDecoder : public IDecoder
{
  public:
    bool canDecode(const std::string &path) const override;
    ImageData decodeFull(const std::string &path) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge) const override;
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override;
    std::vector<std::string> extensions() const override;
    const char *name() const override { return "RawDecoder"; }

  private:
    // Extract the largest embedded JPEG preview from a RAW container and decode
    // it. Returns an empty ImageData when no usable preview is present.
    ImageData extractPreview(const std::string &path, int maxEdge) const;
};
