#include "core/analysis/YuvMetrics.h"

#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvSource.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mviewer::core::yuv
{
namespace
{

constexpr int kWindow = 8;
constexpr int kStep = 4;
constexpr double kK1 = 0.01;
constexpr double kK2 = 0.03;
constexpr double kInfStandIn = 100.0;

const char *subsamplingText(Subsampling s)
{
    switch (s)
    {
    case Subsampling::S444:
        return "4:4:4";
    case Subsampling::S422:
        return "4:2:2";
    case Subsampling::S420:
        return "4:2:0";
    case Subsampling::S400:
        return "4:0:0";
    }
    return "unknown";
}
int64_t codePeak(int bitDepth)
{
    if (bitDepth <= 0 || bitDepth > 16)
        return 255;
    return static_cast<int64_t>((std::uint64_t{1} << static_cast<unsigned>(bitDepth)) - 1U);
}
std::vector<int> axisStarts(int size, int window)
{
    std::vector<int> starts;
    if (size <= 0)
        return starts;
    const int win = size < window ? size : window;
    if (size <= win)
    {
        starts.push_back(0);
        return starts;
    }
    for (int pos = 0; pos + win <= size; pos += kStep)
        starts.push_back(pos);
    const int last = size - win;
    if (starts.empty() || starts.back() != last)
        starts.push_back(last);
    return starts;
}
double windowSsim(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int width,
                  int originX, int originY, int winW, int winH, double c1, double c2)
{
    const double n = static_cast<double>(winW) * static_cast<double>(winH);
    double sumA = 0.0;
    double sumB = 0.0;
    double varA = 0.0;
    double varB = 0.0;
    double cov = 0.0;
    for (int pass = 0; pass < 2; ++pass)
    {
        const double muA = sumA / n;
        const double muB = sumB / n;
        if (pass == 1)
        {
            varA = 0.0;
            varB = 0.0;
            cov = 0.0;
        }
        for (int y = 0; y < winH; ++y)
        {
            const std::size_t row =
                static_cast<std::size_t>(originY + y) * static_cast<std::size_t>(width);
            for (int x = 0; x < winW; ++x)
            {
                const std::size_t index = row + static_cast<std::size_t>(originX + x);
                const double sa = static_cast<double>(a[index]);
                const double sb = static_cast<double>(b[index]);
                if (pass == 0)
                {
                    sumA += sa;
                    sumB += sb;
                }
                else
                {
                    const double da = sa - muA;
                    const double db = sb - muB;
                    varA += da * da;
                    varB += db * db;
                    cov += da * db;
                }
            }
        }
    }
    varA /= n;
    varB /= n;
    cov /= n;
    const double muA = sumA / n;
    const double muB = sumB / n;
    const double num = (2.0 * muA * muB + c1) * (2.0 * cov + c2);
    const double den = (muA * muA + muB * muB + c1) * (varA + varB + c2);
    if (den == 0.0)
        return 1.0;
    return num / den;
}
double planeSsim(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int width,
                 int height, double c1, double c2)
{
    const int winW = width < kWindow ? width : kWindow;
    const int winH = height < kWindow ? height : kWindow;
    const std::vector<int> xs = axisStarts(width, kWindow);
    const std::vector<int> ys = axisStarts(height, kWindow);
    if (xs.empty() || ys.empty() || winW <= 0 || winH <= 0)
        return 1.0;
    double sum = 0.0;
    for (int oy : ys)
    {
        for (int ox : xs)
            sum += windowSsim(a, b, width, ox, oy, winW, winH, c1, c2);
    }
    const double count = static_cast<double>(xs.size()) * static_cast<double>(ys.size());
    return sum / count;
}
void setAverages(YuvFrameMetrics &m)
{
    const double yP = m.planes[0].psnr;
    const double yS = m.planes[0].ssim;
    if (m.planeCount < 3)
    {
        m.psnr611 = yP;
        m.psnr111 = yP;
        m.ssim611 = yS;
        m.ssim111 = yS;
        return;
    }
    const double uP = m.planes[1].psnr;
    const double vP = m.planes[2].psnr;
    const double uS = m.planes[1].ssim;
    const double vS = m.planes[2].ssim;
    m.psnr611 = (6.0 * yP + uP + vP) / 8.0;
    m.psnr111 = (yP + uP + vP) / 3.0;
    m.ssim611 = (6.0 * yS + uS + vS) / 8.0;
    m.ssim111 = (yS + uS + vS) / 3.0;
}
bool planeLongEnough(const std::vector<uint16_t> &plane, int width, int height, std::string &why)
{
    if (width <= 0 || height <= 0)
    {
        why = "plane size is empty";
        return false;
    }
    const std::size_t need = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (plane.size() < need)
    {
        why = "plane shorter than width*height";
        return false;
    }
    return true;
}
double meanPsnr(double sum, int finiteCount, int total)
{
    if (total <= 0)
        return 0.0;
    if (finiteCount == 0)
        return std::numeric_limits<double>::infinity();
    return sum / static_cast<double>(total);
}
void accumulatePsnr(double value, double &sum, int &finiteCount)
{
    if (std::isinf(value))
        sum += kInfStandIn;
    else
    {
        sum += value;
        ++finiteCount;
    }
}
YuvFrameMetrics averageFrames(const std::vector<YuvFrameMetrics> &frames)
{
    YuvFrameMetrics mean;
    if (frames.empty())
        return mean;
    mean.valid = true;
    mean.planeCount = frames.front().planeCount;
    mean.bitDepth = frames.front().bitDepth;
    const int planes = mean.planeCount >= 3 ? 3 : 1;
    double mse[3] = {};
    double psnr[3] = {};
    double ssim[3] = {};
    int finitePsnr[3] = {};
    double p611 = 0.0;
    double p111 = 0.0;
    double s611 = 0.0;
    double s111 = 0.0;
    int finite611 = 0;
    int finite111 = 0;
    for (const YuvFrameMetrics &frame : frames)
    {
        for (int p = 0; p < planes; ++p)
        {
            const PlaneMetrics &plane = frame.planes[static_cast<std::size_t>(p)];
            mse[p] += plane.mse;
            ssim[p] += plane.ssim;
            accumulatePsnr(plane.psnr, psnr[p], finitePsnr[p]);
        }
        accumulatePsnr(frame.psnr611, p611, finite611);
        accumulatePsnr(frame.psnr111, p111, finite111);
        s611 += frame.ssim611;
        s111 += frame.ssim111;
    }
    const double n = static_cast<double>(frames.size());
    for (int p = 0; p < planes; ++p)
    {
        mean.planes[static_cast<std::size_t>(p)].mse = mse[p] / n;
        mean.planes[static_cast<std::size_t>(p)].ssim = ssim[p] / n;
        mean.planes[static_cast<std::size_t>(p)].psnr =
            meanPsnr(psnr[p], finitePsnr[p], static_cast<int>(frames.size()));
    }
    mean.psnr611 = meanPsnr(p611, finite611, static_cast<int>(frames.size()));
    mean.psnr111 = meanPsnr(p111, finite111, static_cast<int>(frames.size()));
    mean.ssim611 = s611 / n;
    mean.ssim111 = s111 / n;
    return mean;
}
bool openKnown(const std::string &path, const char *tag, OpenedYuvFile &out, std::string &why)
{
    if (!openYuvFile(path, out))
    {
        why = std::string(tag) + ": cannot open file";
        return false;
    }
    if (!out.info.known)
    {
        why = std::string(tag) + ": " + out.info.why;
        return false;
    }
    return true;
}
std::string fixedText(double value, int precision, bool infAsWord)
{
    if (infAsWord && std::isinf(value))
        return "inf";
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(precision);
    stream << value;
    return stream.str();
}
std::string sizePair(int aw, int ah, int bw, int bh)
{
    return std::to_string(aw) + "x" + std::to_string(ah) + " vs " + std::to_string(bw) + "x" +
           std::to_string(bh);
}

bool framesMatch(const YuvPlanes &a, const YuvPlanes &b, bool chroma, std::string &why)
{
    const bool otherChroma = !b.u.empty() || !b.v.empty();
    if (a.width != b.width || a.height != b.height)
        why = "size differs: " + sizePair(a.width, a.height, b.width, b.height);
    else if (a.bitDepth != b.bitDepth)
        why = "bit depth differs: " + std::to_string(a.bitDepth) + " vs " +
              std::to_string(b.bitDepth);
    else if (chroma != otherChroma)
        why = "plane presence differs";
    else if (chroma && (a.chromaWidth != b.chromaWidth || a.chromaHeight != b.chromaHeight))
        why = "chroma size differs: " +
              sizePair(a.chromaWidth, a.chromaHeight, b.chromaWidth, b.chromaHeight);
    if (!why.empty())
        return false;
    if (!planeLongEnough(a.y, a.width, a.height, why) ||
        !planeLongEnough(b.y, b.width, b.height, why))
        return false;
    if (!chroma)
        return true;
    return planeLongEnough(a.u, a.chromaWidth, a.chromaHeight, why) &&
           planeLongEnough(a.v, a.chromaWidth, a.chromaHeight, why) &&
           planeLongEnough(b.u, b.chromaWidth, b.chromaHeight, why) &&
           planeLongEnough(b.v, b.chromaWidth, b.chromaHeight, why);
}

} // namespace

bool yuvComparable(const Format &a, const Format &b, std::string *why)
{
    if (a.width != b.width || a.height != b.height)
    {
        if (why != nullptr)
            *why = "size differs: " + sizePair(a.width, a.height, b.width, b.height);
        return false;
    }
    if (a.subsampling != b.subsampling)
    {
        if (why != nullptr)
        {
            *why = std::string("subsampling differs: ") + subsamplingText(a.subsampling) + " vs " +
                   subsamplingText(b.subsampling);
        }
        return false;
    }
    if (a.bitDepth != b.bitDepth)
    {
        if (why != nullptr)
        {
            *why = "bit depth differs: " + std::to_string(a.bitDepth) + " vs " +
                   std::to_string(b.bitDepth);
        }
        return false;
    }
    return true;
}
PlaneMetrics comparePlane(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int width,
                          int height, int bitDepth)
{
    PlaneMetrics metrics;
    if (width <= 0 || height <= 0)
    {
        metrics.psnr = std::numeric_limits<double>::infinity();
        metrics.ssim = 1.0;
        return metrics;
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    uint64_t total = 0;
    for (int y = 0; y < height; ++y)
    {
        uint64_t row = 0;
        const std::size_t base = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x)
        {
            const int64_t delta = static_cast<int64_t>(a[base + static_cast<std::size_t>(x)]) -
                                  static_cast<int64_t>(b[base + static_cast<std::size_t>(x)]);
            row += static_cast<uint64_t>(delta * delta);
        }
        total += row;
    }
    metrics.mse = total == 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(count);
    const double peak = static_cast<double>(codePeak(bitDepth));
    metrics.psnr = metrics.mse == 0.0 ? std::numeric_limits<double>::infinity()
                                      : 10.0 * std::log10((peak * peak) / metrics.mse);
    const double c1 = (kK1 * peak) * (kK1 * peak);
    const double c2 = (kK2 * peak) * (kK2 * peak);
    metrics.ssim = planeSsim(a, b, width, height, c1, c2);
    return metrics;
}
YuvFrameMetrics compareYuvFrames(const YuvPlanes &a, const YuvPlanes &b)
{
    YuvFrameMetrics metrics;
    const bool chroma = !a.u.empty() || !a.v.empty();
    if (!framesMatch(a, b, chroma, metrics.why))
        return metrics;
    metrics.bitDepth = a.bitDepth;
    metrics.planeCount = chroma ? 3 : 1;
    metrics.planes[0] = comparePlane(a.y, b.y, a.width, a.height, a.bitDepth);
    if (chroma)
    {
        metrics.planes[1] = comparePlane(a.u, b.u, a.chromaWidth, a.chromaHeight, a.bitDepth);
        metrics.planes[2] = comparePlane(a.v, b.v, a.chromaWidth, a.chromaHeight, a.bitDepth);
    }
    setAverages(metrics);
    metrics.valid = true;
    return metrics;
}
YuvSequenceMetrics compareYuvFiles(const std::string &pathA, const std::string &pathB,
                                   int64_t maxFrames, const YuvProgressFn &progress)
{
    YuvSequenceMetrics result;
    OpenedYuvFile fileA;
    OpenedYuvFile fileB;
    if (!openKnown(pathA, "A", fileA, result.why) || !openKnown(pathB, "B", fileB, result.why))
        return result;
    result.framesA = fileA.info.frameCount;
    result.framesB = fileB.info.frameCount;
    if (!yuvComparable(fileA.info.format, fileB.info.format, &result.why))
        return result;
    result.why.clear();
    int64_t total = result.framesA < result.framesB ? result.framesA : result.framesB;
    if (maxFrames > 0 && maxFrames < total)
        total = maxFrames;
    for (int64_t index = 0; index < total; ++index)
    {
        if (progress && !progress(index, total))
        {
            result.why = "cancelled";
            return result;
        }
        YuvPlanes planesA;
        YuvPlanes planesB;
        std::string readWhy;
        if (!readYuvFrame(fileA.info, index, fileA.read, planesA, &readWhy))
        {
            result.why = "A: frame " + std::to_string(index) + ": " + readWhy;
            return result;
        }
        if (!readYuvFrame(fileB.info, index, fileB.read, planesB, &readWhy))
        {
            result.why = "B: frame " + std::to_string(index) + ": " + readWhy;
            return result;
        }
        YuvFrameMetrics frame = compareYuvFrames(planesA, planesB);
        if (!frame.valid)
        {
            result.why = "frame " + std::to_string(index) + ": " + frame.why;
            return result;
        }
        result.frames.push_back(std::move(frame));
        result.framesCompared = index + 1;
    }
    if (progress && total > 0 && !progress(total, total))
    {
        result.why = "cancelled";
        return result;
    }
    result.mean = averageFrames(result.frames);
    result.valid = true;
    return result;
}
std::string formatYuvFrameMetrics(const YuvFrameMetrics &m)
{
    if (!m.valid)
        return m.why;
    std::string text = "Y " + fixedText(m.planes[0].psnr, 2, true) + " dB";
    if (m.planeCount >= 3)
    {
        text += "  U " + fixedText(m.planes[1].psnr, 2, true) + " dB";
        text += "  V " + fixedText(m.planes[2].psnr, 2, true) + " dB";
        text += "  6:1:1 " + fixedText(m.psnr611, 2, true) + " dB";
    }
    text += "  |  SSIM Y " + fixedText(m.planes[0].ssim, 4, false);
    if (m.planeCount >= 3)
    {
        text += " U " + fixedText(m.planes[1].ssim, 4, false);
        text += " V " + fixedText(m.planes[2].ssim, 4, false);
        text += " 6:1:1 " + fixedText(m.ssim611, 4, false);
    }
    return text;
}

} // namespace mviewer::core::yuv
