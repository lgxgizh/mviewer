// libFuzzer target: raw YUV parameter inference from file / folder names
// (core/image/yuv/YuvParamInference.cpp). Input layout:
// [8 bytes little-endian file size][file name]\n[folder name]
#include "core/image/yuv/YuvParamInference.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    using namespace mviewer::core::yuv;
    if (size < 8 || size > 4096)
        return 0;
    int64_t fileSize = 0;
    for (int i = 7; i >= 0; --i)
        fileSize = (fileSize << 8) | data[i];
    const std::string_view text(reinterpret_cast<const char *>(data + 8), size - 8);
    const size_t split = text.find('\n');
    const std::string_view name = text.substr(0, split);
    const std::string_view folder =
        split == std::string_view::npos ? std::string_view() : text.substr(split + 1);
    (void)inferYuvParams(name, folder, fileSize);
    int w = 0;
    int h = 0;
    (void)parseSizeToken(text, w, h);
    (void)tokenizeName(name);
    return 0;
}
