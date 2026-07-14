#include "CompareImagesUseCase.h"

#include "core/compare/CompareEngine.h"
#include "core/image/Decoder.h"

CompareImagesUseCase::Result CompareImagesUseCase::execute(
    const std::vector<std::string>& imagePaths)
{
    Result r;
    if (imagePaths.size() < 2)
    {
        r.error = "At least 2 images required for comparison";
        return r;
    }
    // Validation: check all paths are decodeable
    for (const auto& p : imagePaths)
    {
        ImageData img = Decoder::decodeScaled(p, 64); // tiny decode to validate
        if (img.isNull())
        {
            r.error = "Failed to decode: " + p;
            return r;
        }
    }
    r.success = true;
    return r;
}
