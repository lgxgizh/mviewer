#pragma once

#include "core/image/decoder/IDecoder.h"

#include <memory>
#include <string>
#include <vector>

// DecoderRegistry: owns the ordered list of decoders and dispatches a file to
// the first decoder whose canDecode() returns true (specific decoders first,
// fallback last). Header is Qt-free; decoders may use Qt internally.
// RAW status (M24-verified): RawDecoder ships and serves the embedded-JPEG
// preview for CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF etc., with graceful fallback
// to QtFallbackDecoder when no preview exists. Full demosaic (libraw) is
// deferred (M18) — see docs/roadmap.md.
class DecoderRegistry
{
  public:
    static DecoderRegistry &instance();

    // Register a decoder. The FIRST registered decoder is the highest priority.
    // Decoders are tried in registration order; the fallback should be
    // registered last.
    void registerDecoder(std::shared_ptr<IDecoder> decoder);
    void unregister(const std::string &id);

    // Lookup / listing for loader and tests.
    std::shared_ptr<IDecoder> get(const std::string &id) const;
    std::vector<std::string> available() const;

    // Decode via the first claiming decoder. Returns empty ImageData if no
    // decoder can handle the file (graceful — no crash).
    ImageData decodeFull(const std::string &path) const;
    ImageData decodeScaled(const std::string &path, int maxEdge) const;
    ImageData decodeScaled(const std::string &path, int maxEdge,
                           mviewer::domain::ImageMetadata &outMeta) const;

    // Decode and populate metadata in a single pass.
    ImageData decodeFull(const std::string &path, mviewer::domain::ImageMetadata &outMeta) const;

    // Union of all registered decoders' extensions (fallback contributes none).
    std::vector<std::string> supportedExtensions() const;

    // Reset to the default registry (QtDecoder + QtFallbackDecoder).
    void resetToDefaults();

  private:
    DecoderRegistry();
    ~DecoderRegistry() = default;
    DecoderRegistry(const DecoderRegistry &) = delete;
    DecoderRegistry &operator=(const DecoderRegistry &) = delete;

    std::vector<std::shared_ptr<IDecoder>> m_decoders;
};
