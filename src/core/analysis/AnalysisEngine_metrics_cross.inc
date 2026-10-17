// Included by AnalysisEngine_metrics.cpp to handle cross-format SSD accumulation
// and fallback paths without exceeding single-file line caps.

bool accumulateCrossFormatSsd(const ImageData &aData, const ImageData &bData, int w, int h,
                              int64_t &sumSq)
{
    const bool isCrossRgbBgr24 =
        (aData.format == PixelFormat::RGB24 && bData.format == PixelFormat::BGR24) ||
        (aData.format == PixelFormat::BGR24 && bData.format == PixelFormat::RGB24);
    if (isCrossRgbBgr24)
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            for (int x = 0; x < w; ++x)
            {
                const int dr = static_cast<int>(la[x * 3 + 0]) - static_cast<int>(lb[x * 3 + 2]);
                const int dg = static_cast<int>(la[x * 3 + 1]) - static_cast<int>(lb[x * 3 + 1]);
                const int db = static_cast<int>(la[x * 3 + 2]) - static_cast<int>(lb[x * 3 + 0]);
                sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                         static_cast<int64_t>(db) * db;
            }
        }
        return true;
    }

    const bool isCrossRgbaBgra32 =
        (aData.format == PixelFormat::RGBA32 && bData.format == PixelFormat::BGRA32) ||
        (aData.format == PixelFormat::BGRA32 && bData.format == PixelFormat::RGBA32);
    if (isCrossRgbaBgra32)
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            for (int x = 0; x < w; ++x)
            {
                const int dr = static_cast<int>(la[x * 4 + 0]) - static_cast<int>(lb[x * 4 + 2]);
                const int dg = static_cast<int>(la[x * 4 + 1]) - static_cast<int>(lb[x * 4 + 1]);
                const int db = static_cast<int>(la[x * 4 + 2]) - static_cast<int>(lb[x * 4 + 0]);
                sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                         static_cast<int64_t>(db) * db;
            }
        }
        return true;
    }

    return false;
}

void accumulateGenericSsd(const ImageData &aData, const ImageData &bData, int w, int h,
                          int64_t &sumSq)
{
    const ImageBuffer va = aData.view();
    const ImageBuffer vb = bData.view();
    const int cppa = aData.channelsPerPixel();
    const int cppb = bData.channelsPerPixel();
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
        const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
        for (int x = 0; x < w; ++x)
        {
            uint8_t ra = 0, ga = 0, ba = 0;
            uint8_t rb = 0, gb = 0, bb = 0;
            getPixelRGB(la + static_cast<size_t>(x) * cppa, aData.format, ra, ga, ba);
            getPixelRGB(lb + static_cast<size_t>(x) * cppb, bData.format, rb, gb, bb);
            const int dr = static_cast<int>(ra) - static_cast<int>(rb);
            const int dg = static_cast<int>(ga) - static_cast<int>(gb);
            const int db = static_cast<int>(ba) - static_cast<int>(bb);
            sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                     static_cast<int64_t>(db) * db;
        }
    }
}
