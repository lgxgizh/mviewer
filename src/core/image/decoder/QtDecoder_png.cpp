// Bounded PNG display LOD (perf review P2-4). Qt's PNG plugin decodes the full
// raster, then scales it. This path streams libpng rows (the libpng Qt already
// ships) and box-averages them into the target QImage. A null image means the
// caller should fall back to QImageReader.
#include <QByteArray>
#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

#if defined(MVIEWER_HAS_PNG_LOD)
#include <png.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr int kPngHeaderBytes = 33;

QString pathToQString(const std::string &path)
{
    return QString::fromUtf8(path.data(), static_cast<int>(path.size()));
}

uint32_t beU32(const unsigned char *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool pngSignature(const unsigned char *b)
{
    static const unsigned char kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return std::memcmp(b, kSig, 8) == 0;
}

bool pngIhdrEligible(const unsigned char *b, int n)
{
    if (n < kPngHeaderBytes)
        return false;
    if (!pngSignature(b))
        return false;
    if (std::memcmp(b + 12, "IHDR", 4) != 0)
        return false;
    if (beU32(b + 8) < 13)
        return false;
    const uint32_t w = beU32(b + 16);
    const uint32_t h = beU32(b + 20);
    if (w < 1 || w > 65535 || h < 1 || h > 65535)
        return false;
    return b[28] == 0;
}

bool readEligible(const std::string &path)
{
    QFile file(pathToQString(path));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    unsigned char buf[kPngHeaderBytes];
    const qint64 n = file.read(reinterpret_cast<char *>(buf), kPngHeaderBytes);
    if (n < kPngHeaderBytes)
        return false;
    return pngIhdrEligible(buf, static_cast<int>(n));
}

#if defined(MVIEWER_HAS_PNG_LOD)

struct PngPod
{
    int width = 0;
    int height = 0;
    int channels = 0;
    int bitDepth = 0;
    png_size_t rowBytes = 0;
    bool reject = false;
    bool hasIccp = false;
    bool hasSrgb = false;
    const png_byte *icc = nullptr;
    png_uint_32 iccLen = 0;
};

struct PngRead
{
    png_structp png = nullptr;
    png_infop info = nullptr;

    ~PngRead()
    {
        if (png != nullptr)
            png_destroy_read_struct(&png, &info, nullptr);
    }
};

struct Phase2In
{
    png_structp png = nullptr;
    png_byte *row = nullptr;
    uint64_t *acc = nullptr;
    uint32_t *count = nullptr;
    const int *colMap = nullptr;
    uint8_t *bits = nullptr;
    int stride = 0;
    int width = 0;
    int height = 0;
    int tw = 0;
    int th = 0;
    int channels = 0;
    int bitDepth = 0;
    bool alpha = false;
    bool rgbx64 = false;
};

struct DecodeBuffers
{
    std::vector<png_byte> row;
    std::vector<int> colMap;
    std::vector<uint64_t> acc;
    std::vector<uint32_t> count;
    QByteArray icc;
    QImage image;
};

void PNGAPI pngErrorLongjmp(png_structp png, png_const_charp)
{
    png_longjmp(png, 1);
}

void PNGAPI pngWarnIgnore(png_structp, png_const_charp)
{
}

void PNGAPI pngReadFromFile(png_structp png, png_bytep data, png_size_t length)
{
    auto *file = static_cast<QFile *>(png_get_io_ptr(png));
    if (file == nullptr)
    {
        png_error(png, "read");
        return;
    }
    const qint64 n = file->read(reinterpret_cast<char *>(data), static_cast<qint64>(length));
    if (n != static_cast<qint64>(length))
        png_error(png, "short read");
}

bool colorMgmtReject(png_structp png, png_infop info)
{
    if (png_get_interlace_type(png, info) != PNG_INTERLACE_NONE)
        return true;
    const bool gama = png_get_valid(png, info, PNG_INFO_gAMA) != 0;
    const bool chrm = png_get_valid(png, info, PNG_INFO_cHRM) != 0;
    const bool srgb = png_get_valid(png, info, PNG_INFO_sRGB) != 0;
    const bool iccp = png_get_valid(png, info, PNG_INFO_iCCP) != 0;
    return (gama || chrm) && !srgb && !iccp;
}

void applyTransforms(png_structp png, png_infop info)
{
    const png_byte color = png_get_color_type(png, info);
    const png_byte depth = png_get_bit_depth(png, info);
    if (color == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0)
        png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    // PNG 16-bit samples are big-endian. Swap for a little-endian host.
    if (depth == 16)
        png_set_swap(png);
}

void fillPodChannels(png_structp png, png_infop info, PngPod *pod)
{
    pod->width = static_cast<int>(png_get_image_width(png, info));
    pod->height = static_cast<int>(png_get_image_height(png, info));
    pod->channels = static_cast<int>(png_get_channels(png, info));
    pod->bitDepth = static_cast<int>(png_get_bit_depth(png, info));
    pod->rowBytes = png_get_rowbytes(png, info);
    pod->hasSrgb = png_get_valid(png, info, PNG_INFO_sRGB) != 0;
    png_charp name = nullptr;
    int compression = 0;
    png_bytep profile = nullptr;
    png_uint_32 length = 0;
    if (png_get_iCCP(png, info, &name, &compression, &profile, &length) == 0)
        return;
    if (profile == nullptr || length == 0)
        return;
    pod->hasIccp = true;
    pod->icc = profile;
    pod->iccLen = length;
}

// Every libpng call that can longjmp lives in this function. Locals are pointers
// and integers only; the caller owns the png_struct and the pod.
bool phase1Body(png_structp png, png_infop *infoOut, QFile *file, PngPod *pod)
{
    if (setjmp(png_jmpbuf(png))) // NOLINT(cert-err52-cpp)
        return false;
    png_set_read_fn(png, file, pngReadFromFile);
    png_infop info = png_create_info_struct(png);
    if (info == nullptr)
        return false;
    *infoOut = info;
    png_read_info(png, info);
    if (colorMgmtReject(png, info))
    {
        pod->reject = true;
        return true;
    }
    applyTransforms(png, info);
    png_read_update_info(png, info);
    fillPodChannels(png, info, pod);
    return pod->width > 0 && pod->height > 0;
}

bool openPngRead(PngRead *lib)
{
    lib->png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, pngErrorLongjmp, pngWarnIgnore);
    return lib->png != nullptr;
}

bool runPhase1(PngRead *lib, QFile *file, PngPod *pod)
{
    if (!phase1Body(lib->png, &lib->info, file, pod))
        return false;
    return !pod->reject;
}

QImage::Format pngOutFormat(int channels, int bitDepth, bool *alpha, bool *rgbx64)
{
    *alpha = false;
    *rgbx64 = false;
    const bool deep = bitDepth == 16;
    if (bitDepth != 8 && !deep)
        return QImage::Format_Invalid;
    if (channels == 1)
        return deep ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8;
    if (channels == 3)
    {
        *rgbx64 = deep;
        return deep ? QImage::Format_RGBX64 : QImage::Format_RGB888;
    }
    if (channels == 2 || channels == 4)
    {
        *alpha = true;
        return deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888;
    }
    return QImage::Format_Invalid;
}

bool podLayoutOk(const PngPod &pod)
{
    if (pod.width < 1 || pod.width > 65535)
        return false;
    if (pod.height < 1 || pod.height > 65535)
        return false;
    if (pod.bitDepth != 8 && pod.bitDepth != 16)
        return false;
    if (pod.channels < 1 || pod.channels > 4)
        return false;
    const int sampleBytes = pod.bitDepth == 16 ? 2 : 1;
    const uint64_t need = static_cast<uint64_t>(pod.width) * static_cast<uint64_t>(pod.channels) *
                          static_cast<uint64_t>(sampleBytes);
    if (static_cast<uint64_t>(pod.rowBytes) < need)
        return false;
    return pod.rowBytes <= static_cast<png_size_t>(8u * 1024u * 1024u);
}

void makeColMap(int w, int tw, std::vector<int> &colMap)
{
    colMap.assign(static_cast<size_t>(w), 0);
    for (int ox = 0; ox < tw; ++ox)
    {
        const int64_t x0 = static_cast<int64_t>(ox) * w / tw;
        const int64_t next = static_cast<int64_t>(ox + 1) * w / tw;
        const int64_t x1 = x0 + 1 > next ? x0 + 1 : next;
        for (int64_t x = x0; x < x1 && x < w; ++x)
            colMap[static_cast<size_t>(x)] = ox;
    }
}

void copyIcc(const PngPod &pod, QByteArray &out)
{
    out.clear();
    if (!pod.hasIccp || pod.icc == nullptr || pod.iccLen == 0)
        return;
    if (pod.iccLen > 32u * 1024u * 1024u)
        return;
    out = QByteArray(reinterpret_cast<const char *>(pod.icc), static_cast<int>(pod.iccLen));
}

int accChannels(bool alpha, int channels)
{
    return alpha ? 4 : channels;
}

bool prepareBuffers(const PngPod &pod, const QSize &target, DecodeBuffers &buffers, Phase2In &in)
{
    bool alpha = false;
    bool rgbx64 = false;
    const QImage::Format format = pngOutFormat(pod.channels, pod.bitDepth, &alpha, &rgbx64);
    if (format == QImage::Format_Invalid)
        return false;
    const int tw = target.width();
    const int th = target.height();
    buffers.row.resize(pod.rowBytes);
    buffers.colMap.resize(static_cast<size_t>(pod.width));
    makeColMap(pod.width, tw, buffers.colMap);
    const int accCh = accChannels(alpha, pod.channels);
    buffers.acc.assign(static_cast<size_t>(tw) * static_cast<size_t>(accCh), 0);
    buffers.count.assign(static_cast<size_t>(tw), 0);
    buffers.image = QImage(tw, th, format);
    if (buffers.image.isNull() || buffers.row.empty())
        return false;
    copyIcc(pod, buffers.icc);
    in.row = buffers.row.data();
    in.acc = buffers.acc.data();
    in.count = buffers.count.data();
    in.colMap = buffers.colMap.data();
    in.bits = reinterpret_cast<uint8_t *>(buffers.image.bits());
    in.stride = static_cast<int>(buffers.image.bytesPerLine());
    in.width = pod.width;
    in.height = pod.height;
    in.tw = tw;
    in.th = th;
    in.channels = pod.channels;
    in.bitDepth = pod.bitDepth;
    in.alpha = alpha;
    in.rgbx64 = rgbx64;
    return in.bits != nullptr && in.stride > 0;
}

uint32_t pngSample(const png_byte *row, int index, int bitDepth)
{
    if (bitDepth == 16)
    {
        const int o = index * 2;
        return static_cast<uint32_t>(row[o]) | (static_cast<uint32_t>(row[o + 1]) << 8);
    }
    return row[index];
}

void pngStore(uint8_t *dst, int index, int bitDepth, uint32_t value)
{
    if (bitDepth == 16)
    {
        const int o = index * 2;
        dst[o] = static_cast<uint8_t>(value & 0xffu);
        dst[o + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        return;
    }
    dst[index] = static_cast<uint8_t>(value);
}

uint32_t divRound(uint64_t sum, uint64_t n)
{
    if (n == 0)
        return 0;
    return static_cast<uint32_t>((sum + (n / 2)) / n);
}

void zeroAcc(uint64_t *acc, uint32_t *count, int n, int accCh)
{
    const size_t m = static_cast<size_t>(n) * static_cast<size_t>(accCh);
    for (size_t i = 0; i < m; ++i)
        acc[i] = 0;
    for (int i = 0; i < n; ++i)
        count[i] = 0;
}

void addStraight(const png_byte *row, Phase2In *in)
{
    const int ch = in->channels;
    for (int x = 0; x < in->width; ++x)
    {
        const int ox = in->colMap[x];
        uint64_t *dst = in->acc + static_cast<size_t>(ox) * static_cast<size_t>(ch);
        const int base = x * ch;
        for (int c = 0; c < ch; ++c)
            dst[c] += pngSample(row, base + c, in->bitDepth);
        in->count[ox] += 1;
    }
}

void addAlpha(const png_byte *row, Phase2In *in)
{
    const int colorCh = in->channels - 1;
    const bool gray = colorCh == 1;
    for (int x = 0; x < in->width; ++x)
    {
        const int ox = in->colMap[x];
        const int base = x * in->channels;
        const uint32_t a = pngSample(row, base + colorCh, in->bitDepth);
        uint64_t *dst = in->acc + static_cast<size_t>(ox) * 4u;
        if (gray)
        {
            const uint64_t g = static_cast<uint64_t>(pngSample(row, base, in->bitDepth)) * a;
            dst[0] += g;
            dst[1] += g;
            dst[2] += g;
        }
        else
        {
            for (int c = 0; c < 3; ++c)
            {
                const uint32_t s = pngSample(row, base + c, in->bitDepth);
                dst[c] += static_cast<uint64_t>(s) * a;
            }
        }
        dst[3] += a;
        in->count[ox] += 1;
    }
}

void storeStraight(uint8_t *dst, int tw, int channels, int bitDepth, const uint64_t *acc,
                   const uint32_t *count)
{
    for (int ox = 0; ox < tw; ++ox)
    {
        const uint64_t n = count[ox];
        const size_t base = static_cast<size_t>(ox) * static_cast<size_t>(channels);
        for (int c = 0; c < channels; ++c)
        {
            const uint32_t v = divRound(acc[base + static_cast<size_t>(c)], n);
            pngStore(dst, ox * channels + c, bitDepth, v);
        }
    }
}

void storeRgbx64(uint8_t *dst, int tw, const uint64_t *acc, const uint32_t *count)
{
    for (int ox = 0; ox < tw; ++ox)
    {
        const uint64_t n = count[ox];
        const size_t base = static_cast<size_t>(ox) * 3u;
        for (int c = 0; c < 3; ++c)
        {
            const uint32_t v = divRound(acc[base + static_cast<size_t>(c)], n);
            pngStore(dst, ox * 4 + c, 16, v);
        }
        pngStore(dst, ox * 4 + 3, 16, 65535u);
    }
}

void storeAlpha(uint8_t *dst, int tw, int bitDepth, const uint64_t *acc, const uint32_t *count)
{
    for (int ox = 0; ox < tw; ++ox)
    {
        const uint64_t n = count[ox];
        const size_t base = static_cast<size_t>(ox) * 4u;
        const uint64_t sumA = acc[base + 3u];
        for (int c = 0; c < 3; ++c)
        {
            const uint32_t v = divRound(acc[base + static_cast<size_t>(c)], sumA);
            pngStore(dst, ox * 4 + c, bitDepth, v);
        }
        pngStore(dst, ox * 4 + 3, bitDepth, divRound(sumA, n));
    }
}

void storeOutputRow(const Phase2In *in, uint8_t *dst)
{
    if (in->rgbx64)
        storeRgbx64(dst, in->tw, in->acc, in->count);
    else if (in->alpha)
        storeAlpha(dst, in->tw, in->bitDepth, in->acc, in->count);
    else
        storeStraight(dst, in->tw, in->channels, in->bitDepth, in->acc, in->count);
}

void accumSourceRow(Phase2In *in)
{
    if (in->alpha)
        addAlpha(in->row, in);
    else
        addStraight(in->row, in);
}

void boxOneRow(Phase2In *in, int oy, int accCh)
{
    const int64_t y0 = static_cast<int64_t>(oy) * in->height / in->th;
    const int64_t next = static_cast<int64_t>(oy + 1) * in->height / in->th;
    const int64_t y1 = y0 + 1 > next ? y0 + 1 : next;
    zeroAcc(in->acc, in->count, in->tw, accCh);
    for (int64_t y = y0; y < y1; ++y)
    {
        png_read_row(in->png, in->row, nullptr);
        accumSourceRow(in);
    }
    uint8_t *dst = in->bits + static_cast<size_t>(oy) * static_cast<size_t>(in->stride);
    storeOutputRow(in, dst);
}

// Second setjmp scope: rows are consumed strictly in order. Pointers and counts
// are owned by the caller, so a longjmp does not skip a C++ destructor.
bool readAndBox(Phase2In *in)
{
    if (setjmp(png_jmpbuf(in->png))) // NOLINT(cert-err52-cpp)
        return false;
    const int accCh = accChannels(in->alpha, in->channels);
    for (int oy = 0; oy < in->th; ++oy)
        boxOneRow(in, oy, accCh);
    return true;
}

void applyOutputColor(QImage &image, const QByteArray &icc, bool hasSrgb)
{
    if (!icc.isEmpty())
    {
        image.setColorSpace(QColorSpace::fromIccProfile(icc));
        return;
    }
    if (hasSrgb)
        image.setColorSpace(QColorSpace::SRgb);
}

bool targetFits(const QSize &target, const PngPod &pod)
{
    if (!target.isValid() || target.isEmpty())
        return false;
    return target.width() <= pod.width && target.height() <= pod.height;
}

QImage decodePngBoxLodUnchecked(const std::string &path, const QSize &target)
{
    if (!target.isValid() || target.isEmpty())
        return QImage();
    QFile file(pathToQString(path));
    if (!file.open(QIODevice::ReadOnly))
        return QImage();
    PngRead lib;
    if (!openPngRead(&lib))
        return QImage();
    PngPod pod;
    if (!runPhase1(&lib, &file, &pod) || !podLayoutOk(pod))
        return QImage();
    if (!targetFits(target, pod))
        return QImage();
    DecodeBuffers buffers;
    Phase2In in;
    if (!prepareBuffers(pod, target, buffers, in))
        return QImage();
    in.png = lib.png;
    if (!readAndBox(&in))
        return QImage();
    applyOutputColor(buffers.image, buffers.icc, pod.hasSrgb);
    return buffers.image;
}

#endif // MVIEWER_HAS_PNG_LOD

} // namespace

namespace mviewer::core::detail
{

// Declared in QtDecoder.cpp next to the BMP adapter (core headers stay Qt-free).
bool pngBoxLodEligible(const std::string &path);
QImage decodePngBoxLod(const std::string &path, const QSize &target);

bool pngBoxLodEligible(const std::string &path)
{
    try
    {
        return readEligible(path);
    }
    catch (...)
    {
        return false;
    }
}

QImage decodePngBoxLod(const std::string &path, const QSize &target)
{
    try
    {
#if defined(MVIEWER_HAS_PNG_LOD)
        return decodePngBoxLodUnchecked(path, target);
#else
        (void)path;
        (void)target;
        return QImage();
#endif
    }
    catch (...)
    {
        return QImage();
    }
}

} // namespace mviewer::core::detail
