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
// preview and decode it. When the preview is missing or smaller than the
// requested edge, LibRaw (LGPL-2.1 / CDDL-1.0, optional) supplies a half-size
// demosaic, or a full demosaic if a true full decode was asked for.
//
// Graceful by design: if no usable preview is found and LibRaw cannot open the
// file, decode*() returns an empty ImageData so the registry falls through.
//
// Header is Qt-free; the .cpp may use Qt internally.
// Implements ISourceImageCapabilities so SourceImage::decodeLod can classify
// embedded-JPEG preview reads as NativeLod (QImageReader::setScaledSize / DCT)
// and LibRaw half-size as a reduced demosaic. Native region is NOT claimed.
class RawDecoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
    // M42 diagnostic seam retained for compatibility: bytes copied into a
    // second full-file scan buffer by the most recent preview decode. The
    // current streaming implementation never creates that buffer and reports
    // zero.
    static size_t lastPreviewFullFileCopyBytes();

    // M53 diagnostic seam: peak bytes retained by the most recent preview scan
    // or decode. A size probe stays inside the parser window (SOF only). A
    // decode also retains one embedded JPEG at a time, not the container.
    static size_t lastPreviewPeakBufferedBytes();

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
    const char *name() const override
    {
        return "RawDecoder";
    }

    // ── Source-backed capabilities (embedded JPEG preview as native LOD) ────
    bool canProbe(const std::string &path) const override;
    bool probeMetadata(const std::string &path,
                       mviewer::domain::ImageMetadata &meta) const override;
    // True when a reduced path exists (embedded JPEG DCT and/or LibRaw half-size).
    // Full demosaic is not claimed as native LOD: decodeLod returns empty rather
    // than materializing the full sensor when neither reduced path applies.
    bool canNativeLod(const std::string &path) const override;
    // LibRaw half-size is a full-frame LOD, not random-access tiles.
    bool canNativeRegion(const std::string &path) const override;
    // Linked LibRaw (LGPL-2.1 / CDDL-1.0). False in builds configured without it.
    static bool librawAvailable();
    ImageData decodeLod(const std::string &path, int maxEdge,
                        mviewer::domain::ImageMetadata &meta) const override;

  private:
    // Extract the largest embedded JPEG preview from a RAW container and decode
    // it. Returns an empty ImageData when no usable preview is present.
    ImageData extractPreview(const std::string &path, int maxEdge) const;
    // Header-only probe of the largest embedded JPEG (no pixel materialization).
    bool probePreviewSize(const std::string &path, int &outW, int &outH) const;
    struct RawPixelResult
    {
        ImageData image;
        mviewer::domain::RawSource source = mviewer::domain::RawSource::NotRaw;
    };
    // Preview when adequate; LibRaw half-size when the preview is short of the
    // request; full demosaic only when `allowFull` and the display policy (or
    // an explicit full-decode request) asks for it.
    RawPixelResult decodeRawPixels(const std::string &path, int maxEdge, bool allowFull) const;
    RawPixelResult decodeRawLodPixels(const std::string &path, int maxEdge, ImageData preview,
                                      int previewEdge, bool libraw) const;
    RawPixelResult decodeRawFullPixels(const std::string &path, ImageData preview, int previewEdge,
                                       bool libraw) const;
    ImageData demosaicWithLibraw(const std::string &path, bool halfSize, int maxEdge,
                                 mviewer::domain::RawSource *sourceOut) const;
};
