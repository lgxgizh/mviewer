// Installs the display-resample thread pool as the YUV -> RGB row runner, so
// full-frame conversions of large frames run in row bands on several cores.
// The yuv module itself stays std-only (ADR 032); this file is the Qt-side glue.

#include "core/image/yuv/YuvToRgb.h"
#include "core/render/DisplayResampleDetail.h"

#include <functional>

namespace
{

// Bands of fewer rows are not worth a pool hop.
constexpr int kMinRowsPerCall = 64;

thread_local bool t_inBand = false;

void runYuvRows(int rowCount, const std::function<void(int, int)> &fn)
{
    // A band that converts again (nested) must not wait on the same pool.
    if (t_inBand)
    {
        fn(0, rowCount);
        return;
    }
    mviewer::core::resample_detail::parallelForRows(rowCount, kMinRowsPerCall, nullptr,
                                                    [&fn](int y0, int y1)
                                                    {
                                                        t_inBand = true;
                                                        fn(y0, y1);
                                                        t_inBand = false;
                                                    });
}

const bool g_installed = []
{
    mviewer::core::yuv::setYuvRowsRunner(&runYuvRows);
    return true;
}();

} // namespace
