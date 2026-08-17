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
// M47: additionally implements the optional source-backed capability
// interface (probe without pixel decode, native-LOD where the backend really
// provides it, bounded-memory region decode via clipRect). Capability claims
// are evidence-based:
//   * canProbe       — QImageReader::size() reads only the container header.
//   * canNativeLod   — JPEG only: the Phase-0 baseline proved setScaledSize
//                      avoids materializing the full raster (a 100 MP JPEG
//                      scales to 256 px while full decode is rejected by Qt's
//                      256 MB allocation limit). Other formats are NOT
//                      claimed (TIFF scaled decode still rasterizes fully).
//   * canNativeRegion— false: Qt offers no true random-access tile decode.
//                      decodeRegion() is the bounded-memory clipRect path,
//                      classified BoundedRasterRegion by the provider.
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
