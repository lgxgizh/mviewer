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
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_RGB32);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        const QRgb *la = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int dr = static_cast<int>(qRed(la[x])) - qRed(lb[x]);
            const int dg = static_cast<int>(qGreen(la[x])) - qGreen(lb[x]);
            const int db = static_cast<int>(qBlue(la[x])) - qBlue(lb[x]);
            sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                     static_cast<int64_t>(db) * db;
        }
    }
}
