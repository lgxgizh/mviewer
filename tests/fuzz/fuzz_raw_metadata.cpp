// libFuzzer target: TIFF/RAW IFD walker (parseRawMetadata) and the JPEG APP1
// Exif path (parseCaptureMetadata) in core/image/RawMetadata.cpp.
#include "core/image/RawMetadata.h"
#include "fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > (1u << 20))
        return 0;
    const std::string raw = mviewer::fuzz::writeTempInput(data, size, ".dng");
    if (!raw.empty())
        (void)mviewer::core::parseRawMetadata(raw);
    const std::string jpg = mviewer::fuzz::writeTempInput(data, size, ".jpg");
    if (!jpg.empty())
        (void)mviewer::core::parseCaptureMetadata(jpg);
    return 0;
}
