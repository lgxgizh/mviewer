#pragma once

#include "core/image/ISourceImageCapabilities.h"
#include "core/image/decoder/IDecoder.h"

#include <cstddef>
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
// Implements ISourceImageCapabilities so SourceImage::decodeLod can classify
// embedded-JPEG preview reads as NativeLod (QImageReader::setScaledSize / DCT).
// We do NOT claim libraw half-size demosaic — that remains deferred.
class RawDecoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
    // M42 diagnostic seam retained for compatibility: bytes copied into a
    // second full-file scan buffer by the most recent preview decode. The
    // current streaming implementation never creates that buffer and reports
    // zero.
    static size_t lastPreviewFullFileCopyBytes();

    // M53 diagnostic seam: peak bytes retained by the preview scanner while it
    // searches the RAW container. This must stay bounded by the parser window
    // plus the selected preview, rather than growing with the container.
    static size_t lastPreviewPeakBufferedBytes();

    bool canDecode(const std::string &path) const override;
    ImageData decodeFull(const std::string &path) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge,
                           mviewer::domain::ImageMetadata &outMeta) const override;
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override;
    std::vector<std::string> extensions() const override;
    const char *name() const override
    {
        return "RawDecoder";
    }

    // ── Source-backed capabilities (embedded JPEG preview as native LOD) ────
    bool canProbe(const std::string &path) const override;
    bool probeMetadata(const std::string &path,
                       mviewer::domain::ImageMetadata &meta) const override;
    // True for RAW extensions we own: embedded JPEG + setScaledSize (not libraw half).
    bool canNativeLod(const std::string &path) const override;
    bool canNativeRegion(const std::string &path) const override;
    ImageData decodeLod(const std::string &path, int maxEdge,
                        mviewer::domain::ImageMetadata &meta) const override;

  private:
    // Extract the largest embedded JPEG preview from a RAW container and decode
    // it. Returns an empty ImageData when no usable preview is present.
    ImageData extractPreview(const std::string &path, int maxEdge) const;
    // Header-only probe of the largest embedded JPEG (no pixel materialization).
    bool probePreviewSize(const std::string &path, int &outW, int &outH) const;
};
