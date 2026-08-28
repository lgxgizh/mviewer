#pragma once

#include "core/image/ISourceImageCapabilities.h"
#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// Qt-based decoder for raster formats supported by QImageReader
// (JPEG/PNG/BMP/TIFF). This is the primary decoder; DecoderRegistry tries it
// before the fallback. It respects EXIF orientation (auto-transform) and
// outputs RGB24 ImageData, identical to the legacy Decoder output.
//
// M47/M53: additionally implements the optional source-backed capability
// interface (probe without pixel decode, native-LOD where the backend really
// provides it, bounded-memory region decode). Capability claims are
// evidence-based:
//   * canProbe       — QImageReader::size() reads only the container header.
//   * canNativeLod   — JPEG uses reduced DCT scaling; Windows TIFF uses the
//                      measured WIC frame/clip/scale adapter. Other formats
//                      are NOT claimed without equivalent proof.
//   * canNativeRegion— false: decodeRegion() uses the bounded WIC clip path
//                      for TIFF and is classified BoundedRasterRegion by the
//                      provider; Qt offers no true random-access tile claim.
class QtDecoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
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
        return "QtDecoder";
    }

    // ── M47 source-backed capabilities ───────────────────────────────────────
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
