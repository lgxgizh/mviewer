// libFuzzer target: Y4M stream header, FRAME header and frame indexer
// (core/image/yuv/Y4mHeader.cpp).
#include "core/image/yuv/Y4mHeader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    using namespace mviewer::core::yuv;
    std::string why;
    const auto info = parseY4mHeader(data, size, &why);
    (void)parseY4mFrameHeader(data, size);
    if (!info)
        return 0;
    const ReadAtFn read = [data, size](int64_t offset, uint8_t *buf, size_t n) -> size_t
    {
        if (offset < 0 || static_cast<uint64_t>(offset) >= size)
            return 0;
        const size_t avail = size - static_cast<size_t>(offset);
        const size_t count = std::min(n, avail);
        std::memcpy(buf, data + offset, count);
        return count;
    };
    (void)indexY4mFrames(*info, static_cast<int64_t>(size), read, 64);
    return 0;
}
