#include "core/image/decoder/JpegTurbo.h"

#include "core/filesystem/Utf8Path.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

#if defined(MVIEWER_HAS_TURBOJPEG)
#include <turbojpeg.h>
#endif

namespace mviewer::core::detail
{
namespace
{
#if defined(MVIEWER_HAS_TURBOJPEG)

namespace
{
// True when the JPEG carries an ICC_PROFILE APP2 segment. The search is over
// the raw compressed bytes: a false positive only costs the fast path (the file
// decodes through Qt), a false negative would silently drop the profile.
bool hasEmbeddedIccProfile(const std::vector<unsigned char> &jpeg)
{
    static const unsigned char kSignature[] = {'I', 'C', 'C', '_', 'P', 'R',
                                               'O', 'F', 'I', 'L', 'E', '\0'};
    const auto begin = jpeg.data();
    const auto last = jpeg.data() + jpeg.size();
    const auto found = std::search(begin, last, kSignature, kSignature + sizeof(kSignature));
    return found != last;
}
} // namespace

// The SMALLEST built-in factor whose output still COVERS `maxEdge`, accepted
// only when it does not overshoot by more than 50%.
//
// Two constraints meet here. The fast path may never hand the viewer a coarser
// raster than it asked for - a zoom that re-materialized to a smaller image
// would visibly degrade (`m47_compare_lod_tests` C3 caught that). But
// libjpeg-turbo can only scale by powers of two, so for a small request on a
// huge source even 1/8 lands far above the target; accepting that would change
// the reported LOD geometry (`m47_source_tests` T2 expects an exact 256x178).
// When neither constraint holds, `ok` is cleared and Qt performs its exact
// rescale - the fast path simply does not apply to that request.
tjscalingfactor pickScaling(int srcW, int srcH, int maxEdge, bool &ok)
{
    ok = true;
    if (maxEdge <= 0)
        return tjscalingfactor{1, 1};
    int factors = 0;
    const tjscalingfactor *list = tjGetScalingFactors(&factors);
    if (list == nullptr || factors <= 0)
        return tjscalingfactor{1, 1};
    tjscalingfactor best = list[0];
    long bestEdge = std::numeric_limits<long>::max();
    for (int i = 0; i < factors; ++i)
    {
        const int w = TJSCALED(srcW, list[i]);
        const int h = TJSCALED(srcH, list[i]);
        if (w <= 0 || h <= 0)
            continue;
        const long edge = std::max(w, h);
        if (edge >= maxEdge && edge < bestEdge)
        {
            bestEdge = edge;
            best = list[i];
        }
    }
    if (bestEdge == std::numeric_limits<long>::max() ||
        bestEdge * 2 > static_cast<long>(maxEdge) * 3)
        ok = false;
    return best;
}
#endif
} // namespace

bool hasTurboJpeg()
{
#if defined(MVIEWER_HAS_TURBOJPEG)
    return true;
#else
    return false;
#endif
}

ImageData decodeJpegTurbo(const std::string &path, int maxEdge)
{
#if !defined(MVIEWER_HAS_TURBOJPEG)
    (void)path;
    (void)maxEdge;
    return {};
#else
    // One sized read. istreambuf_iterator walks the stream a byte at a time
    // through a streambuf; on a 20 MB file that is a measurable share of the
    // decode time.
    std::ifstream in(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!in)
        return {};
    std::vector<unsigned char> jpeg;
    in.seekg(0, std::ios::end);
    const std::streampos end = in.tellg();
    if (end <= 0 || end > std::numeric_limits<size_t>::max())
        return {};
    jpeg.resize(static_cast<size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char *>(jpeg.data()), end))
        return {};
    if (jpeg.size() < 32)
        return {};

    // An embedded ICC profile must be reported to the caller (the colour-managed
    // display path and the metadata centre read it). The decoded pixels carry no
    // colour space, so a profile-bearing JPEG keeps Qt's decode, which attaches
    // it to the QImage. Cost: one memmem-style scan of the compressed bytes.
    if (hasEmbeddedIccProfile(jpeg))
        return {};

    tjhandle handle = tjInitDecompress();
    if (handle == nullptr)
        return {};

    int srcW = 0, srcH = 0, subsample = 0, colorspace = 0;
    if (tjDecompressHeader3(handle, jpeg.data(), static_cast<unsigned long>(jpeg.size()), &srcW,
                            &srcH, &subsample, &colorspace) != 0)
    {
        tjDestroy(handle);
        return {};
    }

    // Only the two colour spaces the pipeline can hand to the display path
    // directly. CMYK / YCCK and anything exotic stay on Qt, which converts them.
    int pixelFormat = TJPF_RGB;
    PixelFormat outFormat = PixelFormat::RGB24;
    if (colorspace == TJCS_GRAY)
    {
        pixelFormat = TJPF_GRAY;
        outFormat = PixelFormat::Grayscale8;
    }
    else if (colorspace != TJCS_RGB && colorspace != TJCS_YCbCr)
    {
        // CMYK / YCCK and anything else stay on Qt, which converts them.
        tjDestroy(handle);
        return {};
    }

    bool scalingOk = true;
    const tjscalingfactor factor = pickScaling(srcW, srcH, maxEdge, scalingOk);
    if (!scalingOk)
    {
        tjDestroy(handle);
        return {};
    }
    const int outW = std::max(1, TJSCALED(srcW, factor));
    const int outH = std::max(1, TJSCALED(srcH, factor));
    const int channels = (pixelFormat == TJPF_GRAY) ? 1 : 3;

    ImageData out = makeImageData(outW, outH, outFormat);
    if (out.isNull())
    {
        tjDestroy(handle);
        return {};
    }
    ImageBuffer view = out.view();
    const int pitch = view.stride() > 0 ? static_cast<int>(view.stride()) : outW * channels;

    // Accurate IDCT, not TJFLAG_FASTDCT: the fast flag trades up to 8 codes of
    // accuracy for speed, which shows up as a visible difference against Qt's
    // own decode on the same file. libjpeg-turbo's SIMD accurate IDCT is still
    // far faster than the IJG scalar one Qt ships.
    const int flags = 0;
    const int rc = tjDecompress2(handle, jpeg.data(), static_cast<unsigned long>(jpeg.size()),
                                 view.data, outW, pitch, outH, pixelFormat, flags);
    tjDestroy(handle);
    if (rc != 0)
        return {};
    return out;
#endif
}

} // namespace mviewer::core::detail
