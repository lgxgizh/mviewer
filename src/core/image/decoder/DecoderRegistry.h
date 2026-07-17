#pragma once

#include "core/image/decoder/IDecoder.h"

#include <memory>
#include <string>
#include <vector>

// DecoderRegistry: owns the ordered list of decoders and dispatches a file to
// the first decoder whose canDecode() returns true (specific decoders first,
// fallback last). Header is Qt-free; decoders may use Qt internally.
// RAW (libraw) is intentionally NOT supported yet — see TODO(M7): RAW.
class DecoderRegistry
{
  public:
    static DecoderRegistry &instance();

    // Register a decoder. The FIRST registered decoder is the highest priority.
    // Decoders are tried in registration order; the fallback should be
    // registered last.
    void registerDecoder(std::shared_ptr<IDecoder> decoder);

    // Decode via the first claiming decoder. Returns empty ImageData if no
    // decoder can handle the file (graceful — no crash). TODO(M7): RAW support.
    ImageData decodeFull(const std::string &path) const;
    ImageData decodeScaled(const std::string &path, int maxEdge) const;

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
