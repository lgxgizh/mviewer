#pragma once

// HDR raster support (report feature 4): OpenEXR and PFM.
//
// Both formats store scene-referred linear RGB as 16-bit half or 32-bit float.
// The core pixel buffer is 8-bit, so the decoder keeps a float working image
// only inside this TU and tone-maps it to RGB24 on the way out:
//
//   linear = value * exposure
//   mapped = linear / (1 + linear)          (Reinhard, per channel)
//   srgb   = pow(mapped, 1/2.2) * 255
//
// Metadata records the format and that the source is HDR so the viewer can
// label it and re-decode with a different exposure.

#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

class HdrDecoder : public IDecoder
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
        return "HdrDecoder";
    }

    struct ToneMap
    {
        double exposure = 1.0;
        double gamma = 2.2;
    };

    // Exposed for the viewer's exposure / gamma controls and for tests.
    static ImageData toneMapToRgb24(const std::vector<float> &rgb, int width, int height,
                                    const ToneMap &tone);

  private:
    bool decodeFile(const std::string &path, std::vector<float> &rgb, int &width, int &height,
                    bool &isFloat, std::string &error) const;
    bool readPfm(const std::string &path, std::vector<float> &rgb, int &width, int &height,
                 std::string &error) const;
    bool readExr(const std::string &path, std::vector<float> &rgb, int &width, int &height,
                 std::string &error) const;
};
