// libFuzzer target: hand-written JPEG/EXIF scrapers in MetadataReader
// (EXIF IFD1 thumbnail, APP2 ICC profile, GPS IFD via readHeaders()).
#include "core/image/MetadataReader.h"
#include "fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > (1u << 20))
        return 0;
    mviewer::fuzz::ensureApp();
    const std::string jpg = mviewer::fuzz::writeTempInput(data, size, ".jpg");
    if (jpg.empty())
        return 0;
    (void)mviewer::core::MetadataReader::extractExifThumbnail(jpg);
    (void)mviewer::core::MetadataReader::extractJpegIccProfile(jpg);
    (void)mviewer::core::MetadataReader::readHeaders(jpg);
    return 0;
}
