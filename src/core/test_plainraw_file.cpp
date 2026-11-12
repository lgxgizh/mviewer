// NOLINTBEGIN
#include "core/FileMetadataRelocation.h"
#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/rawplain/PlainRawUnpack.h"
#ifndef PLAINRAW_NO_REGISTRY
#include "core/image/ImageFormats.h"
#include "core/image/SourceImage.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/PlainRawDecoder.h"
#include "core/image/decoder/YuvDecoder.h"
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

namespace
{

using mviewer::core::rawplain::Cfa;
using mviewer::core::rawplain::Packing;
using mviewer::core::rawplain::Params;
using mviewer::core::rawplain::PlainRawFileInfo;
using mviewer::core::rawplain::plainRawParamKey;
using mviewer::core::rawplain::PlainRawParamStore;
using mviewer::core::rawplain::SizeCheck;
using mviewer::domain::ImageMetadata;

std::vector<std::uint16_t> patternSamples(int w, int h)
{
    std::vector<std::uint16_t> s(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int64_t i =
                static_cast<int64_t>(y) * static_cast<int64_t>(w) + static_cast<int64_t>(x);
            s[static_cast<std::size_t>(i)] =
                static_cast<std::uint16_t>(static_cast<unsigned>(i & 1023));
        }
    }
    return s;
}

std::vector<std::uint8_t> packLsb16(const std::vector<std::uint16_t> &samples, int w, int stride)
{
    const int h = static_cast<int>(samples.size() / static_cast<std::size_t>(w));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int64_t i =
                static_cast<int64_t>(y) * static_cast<int64_t>(w) + static_cast<int64_t>(x);
            const auto v = samples[static_cast<std::size_t>(i)];
            const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                                   static_cast<std::size_t>(x) * 2U;
            bytes[at] = static_cast<std::uint8_t>(v & 0xFFU);
            bytes[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
        }
    }
    return bytes;
}

void writeBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

bool contains(const std::string &text, const std::string &needle)
{
    return text.find(needle) != std::string::npos;
}

Params sized(int w, int h)
{
    Params p;
    p.width = w;
    p.height = h;
    p.bitDepth = 10;
    p.packing = Packing::Lsb16;
    p.cfa = Cfa::RGGB;
    return p;
}

void testOverrideFollowsRename()
{
    auto &store = PlainRawParamStore::instance();
    store.clearAll();
    const Params params = sized(64, 32);
    const std::string from = "/tmp/x/a_64x32.raw";
    const std::string to = "/tmp/x/b_64x32.raw";
    const std::string keyFrom = plainRawParamKey(from);
    const std::string keyTo = plainRawParamKey(to);
    store.setOverride(keyFrom, params);

    mviewer::core::relocateFileMetadata(from, to);
    CHECK(store.override(keyTo).has_value());
    CHECK(store.override(keyTo)->width == 64);
    CHECK(!store.override(keyFrom).has_value());

    mviewer::core::relocateFileMetadata(to, from);
    CHECK(store.override(keyFrom).has_value());
    CHECK(!store.override(keyTo).has_value());

    store.copyOverride("missing-plain-raw-key", "nowhere");
    CHECK(!store.override("nowhere").has_value());

    store.copyOverride(keyFrom, keyTo);
    CHECK(store.override(keyFrom).has_value());
    CHECK(store.override(keyTo).has_value());
    CHECK(store.override(keyFrom)->width == store.override(keyTo)->width);

    const uint64_t revision = store.revision();
    store.moveOverride("missing-plain-raw-key", keyTo);
    CHECK(store.revision() == revision);
    CHECK(store.override(keyFrom).has_value());
    store.clearAll();
}

void waitUntilSampleIdle()
{
    using mviewer::core::rawplain::plainRawSampleFillIdle;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!plainRawSampleFillIdle())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

std::string rawRedText(int value)
{
    return "RAW R=" + std::to_string(value) + " (10-bit)";
}

void writeFlatRaw(const std::filesystem::path &path, std::uint16_t value)
{
    auto samples = patternSamples(64, 32);
    samples[0] = value;
    writeBytes(path, packLsb16(samples, 64, 128));
}

void testSampleMtime(const std::filesystem::path &dir)
{
    using mviewer::core::rawplain::plainRawSampleText;
    const auto mtimeFile = dir / "mtime_64x32.raw";
    writeFlatRaw(mtimeFile, 10);
    const std::string path = mtimeFile.string();
    const std::string before = plainRawSampleText(path, 0, 0);
    CHECK(before == rawRedText(10));
    const auto stamp = std::filesystem::last_write_time(mtimeFile);
    writeFlatRaw(mtimeFile, 400);
    std::filesystem::last_write_time(mtimeFile, stamp + std::chrono::seconds(2));
    const std::string after = plainRawSampleText(path, 0, 0);
    CHECK(after == rawRedText(400));
}

void testSampleAsync(const std::filesystem::path &dir)
{
    using mviewer::core::rawplain::plainRawSampleFillIdle;
    using mviewer::core::rawplain::plainRawSampleText;
    using mviewer::core::rawplain::SampleRead;
    const auto asyncFile = dir / "async_64x32.raw";
    writeFlatRaw(asyncFile, 7);
    const std::string path = asyncFile.string();
    CHECK(plainRawSampleText(path, 0, 0, SampleRead::CachedOrAsync).empty());
    waitUntilSampleIdle();
    CHECK(plainRawSampleFillIdle());
    const std::string cached = plainRawSampleText(path, 0, 0, SampleRead::CachedOrAsync);
    CHECK(cached == rawRedText(7));
    CHECK(cached == plainRawSampleText(path, 0, 0));
}

void testSampleTwoFiles(const std::filesystem::path &dir)
{
    using mviewer::core::rawplain::plainRawSampleFillIdle;
    using mviewer::core::rawplain::plainRawSampleText;
    using mviewer::core::rawplain::SampleRead;
    const auto fileA = dir / "alt_a_64x32.raw";
    const auto fileB = dir / "alt_b_64x32.raw";
    writeFlatRaw(fileA, 11);
    writeFlatRaw(fileB, 22);
    const std::string pathA = fileA.string();
    const std::string pathB = fileB.string();
    CHECK(plainRawSampleText(pathA, 0, 0, SampleRead::CachedOrAsync).empty());
    waitUntilSampleIdle();
    CHECK(plainRawSampleText(pathB, 0, 0, SampleRead::CachedOrAsync).empty());
    waitUntilSampleIdle();
    const std::string textA = plainRawSampleText(pathA, 0, 0, SampleRead::CachedOrAsync);
    const std::string textB = plainRawSampleText(pathB, 0, 0, SampleRead::CachedOrAsync);
    CHECK(textA == rawRedText(11));
    CHECK(textB == rawRedText(22));
    CHECK(plainRawSampleFillIdle());
    CHECK(plainRawSampleText(pathA, 0, 0, SampleRead::CachedOrAsync) == textA);
    CHECK(plainRawSampleText(pathB, 0, 0, SampleRead::CachedOrAsync) == textB);
}

void testSampleReadCache(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    testSampleMtime(dir);
    testSampleAsync(dir);
    testSampleTwoFiles(dir);
    waitUntilSampleIdle();
}

#ifndef PLAINRAW_NO_REGISTRY
const uint8_t *imageRow(const ConstImageView &view, int y)
{
    const auto stride = static_cast<std::size_t>(view.stride());
    return view.data + static_cast<std::size_t>(y) * stride;
}

bool topLeftEquals(const ImageData &full, const ImageData &part, int w, int h)
{
    if (full.isNull() || part.isNull() || part.width != w || part.height != h)
        return false;
    if (full.width < w || full.height < h || full.format != part.format)
        return false;
    const ConstImageView fullView = full.constView();
    const ConstImageView partView = part.constView();
    if (fullView.data == nullptr || partView.data == nullptr)
        return false;
    const int cpp = full.channelsPerPixel();
    const auto bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(cpp);
    for (int y = 0; y < h; ++y)
    {
        if (std::memcmp(imageRow(fullView, y), imageRow(partView, y), bytes) != 0)
            return false;
    }
    return true;
}

void testPlainRawCapabilities(const std::string &scenePath, const std::string &unknownPath)
{
    PlainRawDecoder decoder;
    CHECK(decoder.canProbe(scenePath) && !decoder.canProbe("photo.cr2"));
    CHECK(!decoder.canNativeLod(scenePath) && !decoder.canNativeRegion(scenePath));
    ImageMetadata probed;
    CHECK(decoder.probeMetadata(scenePath, probed));
    CHECK(probed.width == 64 && probed.height == 32);
    ImageMetadata unknownMeta;
    CHECK(!decoder.probeMetadata(unknownPath, unknownMeta));

    DecoderRegistry::instance().resetToDefaults();
    const auto src = mviewer::core::SourceImage::open(scenePath);
    CHECK(src != nullptr && src->metadata().width == 64 && src->metadata().height == 32);
    CHECK(src != nullptr && src->rawWidth() == 64 && src->rawHeight() == 32);

    ImageMetadata fullMeta;
    const ImageData full = decoder.decodeFull(scenePath, fullMeta);
    ImageMetadata regionMeta;
    const ImageData region = decoder.decodeRegion(scenePath, 0, 0, 32, 16, 32, 16, regionMeta);
    CHECK(region.width == 32 && region.height == 16);
    CHECK(topLeftEquals(full, region, 32, 16));
    CHECK(regionMeta.width == 64 && regionMeta.height == 32);
    ImageMetadata againMeta;
    const ImageData again = decoder.decodeRegion(scenePath, 0, 0, 32, 16, 32, 16, againMeta);
    CHECK(topLeftEquals(full, again, 32, 16));
    if (src)
    {
        const auto opened = src->decodeRegion({0, 0, 32, 16}, 32, 16);
        CHECK(opened.ok && topLeftEquals(full, opened.pixels, 32, 16));
    }
    ImageMetadata lodMeta;
    const ImageData lod = decoder.decodeLod(scenePath, 16, lodMeta);
    CHECK(lod.width == 16 && lod.height == 8);
}
#endif

bool grayRowPixel(const ImageData &image, int x, int value)
{
    if (image.isNull() || image.format != PixelFormat::RGB24)
        return false;
    const ConstImageView view = image.constView();
    if (view.data == nullptr || x < 0 || x >= image.width || image.height < 1)
        return false;
    const std::size_t at = static_cast<std::size_t>(x) * 3U;
    const auto expect = static_cast<uint8_t>(value);
    return view.data[at] == expect && view.data[at + 1] == expect && view.data[at + 2] == expect;
}

void testGrayDump(const std::filesystem::path &dir)
{
    using namespace mviewer::core::rawplain;
    CHECK(isPlainRawPath("scan.gray") && isGrayRawPath("scan.gray"));
    CHECK(isPlainRawPath("SCAN_64x32.GREY") && isGrayRawPath("SCAN_64x32.GREY"));
    CHECK(isGrayRawPath("dir/SCAN_64x32.GREY"));
    CHECK(!isGrayRawPath("scene.RAW"));
    CHECK(!isGrayRawPath("photo.png"));
    CHECK(dumpKindForPath("scan.GREY") == DumpKind::Gray);
    CHECK(dumpKindForPath("scene.RAW") == DumpKind::Sensor);
    CHECK(dumpKindForPath("photo.jpg") == DumpKind::Sensor);

    auto &store = PlainRawParamStore::instance();
    store.clearAll();
    const int w = 64;
    const int h = 32;
    std::vector<std::uint8_t> ramp(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        ramp[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i & 255);
    const auto grayFile = dir / "scan_64x32.gray";
    writeBytes(grayFile, ramp);
    const std::string grayPath = grayFile.string();

    PlainRawFileInfo info = resolvePlainRawFile(grayPath);
    CHECK(info.resolved.known);
    CHECK(info.resolved.params.packing == Packing::Bits8);
    CHECK(info.resolved.params.bitDepth == 8);
    CHECK(info.resolved.params.cfa == Cfa::Mono);
    CHECK(info.resolved.params.wb == WhiteBalance::None);
    CHECK(info.resolved.params.srgbGamma == false);
    CHECK(info.resolved.params.width == w && info.resolved.params.height == h);

    std::string why;
    const ImageData image = decodePlainRawFile(grayPath, 0, nullptr, &why);
    CHECK(!image.isNull());
    CHECK(image.width == w && image.height == h);
    CHECK(grayRowPixel(image, 0, ramp[0]));
    CHECK(grayRowPixel(image, 1, ramp[1]));
    CHECK(grayRowPixel(image, 17, ramp[17]));
    CHECK(grayRowPixel(image, 63, ramp[63]));

    const unsigned first = ramp[0];
    const unsigned at17 = ramp[17];
    const std::string sample = plainRawSampleText(grayPath, 0, 0);
    CHECK(sample.rfind("GRAY=", 0) == 0);
    CHECK(sample == "GRAY=" + std::to_string(first) + " (8-bit)");
    CHECK(plainRawSampleText(grayPath, 17, 0) == "GRAY=" + std::to_string(at17) + " (8-bit)");

    ImageMetadata meta;
    fillPlainRawMetadata(info, grayPath, meta);
    CHECK(meta.format == "Gray (headerless)");
    CHECK(meta.colorSpace == "Gray 8-bit");
    CHECK(meta.channels == 1);
    CHECK(contains(plainRawCacheKeySuffix(grayPath), toString(store.defaults(DumpKind::Gray))));

    // Name tokens need two digits and both sides >= 16, so 16x2 is not a size.
    // 16x16 1-bit is 2 bytes/row * 16 rows = 32 bytes and is the packing guess.
    const auto bitsFile = dir / "bw_16x16.gray";
    writeBytes(bitsFile, std::vector<std::uint8_t>(32, 0xA5));
    const PlainRawFileInfo bits = resolvePlainRawFile(bitsFile.string());
    CHECK(bits.resolved.known);
    CHECK(bits.resolved.guessed);
    CHECK(bits.resolved.params.packing == Packing::Bits1);
    CHECK(bits.resolved.params.bitDepth == 1);

    const Params sensorBefore = store.defaults();
    Params gray16 = grayDefaultParams();
    gray16.bitDepth = 16;
    gray16.packing = Packing::Lsb16;
    gray16.cfa = Cfa::RGGB;
    store.setDefaults(DumpKind::Gray, gray16);
    CHECK(store.defaults() == sensorBefore);
    CHECK(store.defaults(DumpKind::Gray).bitDepth == 16);
    CHECK(store.defaults(DumpKind::Gray).packing == Packing::Lsb16);
    CHECK(store.defaults(DumpKind::Gray).cfa == Cfa::Mono);
    CHECK(store.defaults(DumpKind::Gray).srgbGamma == false);
    store.clearAll();
    CHECK(store.defaults(DumpKind::Gray) == grayDefaultParams());

    Params forced;
    forced.width = w;
    forced.height = h;
    forced.bitDepth = 8;
    forced.packing = Packing::Bits8;
    forced.cfa = Cfa::RGGB;
    forced.wb = WhiteBalance::GrayWorld;
    forced.wbGainR = 2.0;
    forced.wbGainB = 0.5;
    forced.srgbGamma = false;
    store.setOverride(plainRawParamKey(grayPath), forced);
    info = resolvePlainRawFile(grayPath);
    CHECK(info.fromOverride && info.resolved.known);
    CHECK(info.resolved.params.cfa == Cfa::Mono);
    CHECK(info.resolved.params.wb == WhiteBalance::None);
    CHECK(info.resolved.params.wbGainR == 1.0);
    CHECK(info.resolved.params.wbGainB == 1.0);
    CHECK(plainRawSampleText(grayPath, 0, 0).rfind("GRAY=", 0) == 0);
    store.clearAll();

    const std::string rawPath = "/tmp/mv-gray-reloc/a.raw";
    const std::string grayMove = "/tmp/mv-gray-reloc/a.gray";
    const std::string rawKey = plainRawParamKey(rawPath);
    const std::string grayKey = plainRawParamKey(grayMove);
    store.setOverride(rawKey, sized(8, 8));
    mviewer::core::relocateFileMetadata(rawPath, grayMove);
    CHECK(!store.override(rawKey).has_value());
    CHECK(!store.override(grayKey).has_value());
    const std::string fromGray = "/tmp/mv-gray-reloc/s.gray";
    const std::string toGrey = "/tmp/mv-gray-reloc/s.grey";
    store.setOverride(plainRawParamKey(fromGray), sized(8, 8));
    mviewer::core::relocateFileMetadata(fromGray, toGrey);
    CHECK(store.override(plainRawParamKey(toGrey)).has_value());
    CHECK(!store.override(plainRawParamKey(fromGray)).has_value());
    store.clearAll();

#ifndef PLAINRAW_NO_REGISTRY
    DecoderRegistry::instance().resetToDefaults();
    YuvDecoder yuv;
    CHECK(!yuv.canDecode(grayPath));
    CHECK(!yuv.canDecode("SCAN_64x32.GREY"));
    PlainRawDecoder decoder;
    CHECK(decoder.canDecode(grayPath) && decoder.canDecode("SCAN_64x32.GREY"));
    CHECK(decoder.canProbe(grayPath));
    const auto exts = decoder.extensions();
    CHECK(exts.size() == 3 && exts[0] == "raw" && exts[1] == "gray" && exts[2] == "grey");
    ImageMetadata decodedMeta;
    const ImageData decoded = DecoderRegistry::instance().decodeFull(grayPath, decodedMeta);
    CHECK(!decoded.isNull());
    CHECK(decoded.width == w && decoded.height == h);
    CHECK(decodedMeta.format == "Gray (headerless)");
    ImageMetadata probed;
    CHECK(decoder.probeMetadata(grayPath, probed));
    CHECK(probed.format == "Gray (headerless)");
    ImageMetadata regionMeta;
    const ImageData region = decoder.decodeRegion(grayPath, 0, 0, 8, 1, 8, 1, regionMeta);
    CHECK(!region.isNull() && regionMeta.format == "Gray (headerless)");
    CHECK(mviewer::core::ImageFormats::isSupportedSuffix("gray"));
    CHECK(mviewer::core::ImageFormats::isSupportedSuffix("grey"));
#endif
}

} // namespace

int main()
{
    using namespace mviewer::core::rawplain;
    auto &store = PlainRawParamStore::instance();
    store.clearAll();

    CHECK(isPlainRawPath("scene.RAW"));
    CHECK(isPlainRawPath("a.b.Raw"));
    CHECK(isPlainRawPath(".raw"));
    CHECK(!isPlainRawPath("raw"));
    CHECK(!isPlainRawPath("x.raw/"));
    CHECK(!isPlainRawPath("x.raw\\"));
    CHECK(!isPlainRawPath("foo.raw.jpg"));
    CHECK(!isPlainRawPath("dir/photo.png"));

    const uint64_t rev0 = store.revision();
    const Params def = store.defaults();
    CHECK(def.width == 0);
    CHECK(def.bitDepth == 10);
    CHECK(def.packing == Packing::Lsb16);
    Params custom = def;
    custom.bitDepth = 12;
    custom.blackLevel = 16;
    store.setDefaults(custom);
    CHECK(store.revision() == rev0 + 1U);
    CHECK(store.defaults().bitDepth == 12);
    CHECK(store.defaults().blackLevel == 16);
    Params over = sized(8, 8);
    store.setOverride("k", over);
    CHECK(store.revision() == rev0 + 2U);
    CHECK(store.override("k").has_value());
    CHECK(store.override("k")->width == 8);
    CHECK(!store.override("missing").has_value());
    store.clearOverride("k");
    CHECK(store.revision() == rev0 + 3U);
    CHECK(!store.override("k").has_value());
    store.clearAll();
    CHECK(store.defaults().bitDepth == 10);
    CHECK(store.revision() == rev0 + 4U);

    testOverrideFollowsRename();

    std::mt19937 rng{std::random_device{}()};
    const auto dir =
        std::filesystem::temp_directory_path() / ("mviewer_plainraw_test_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    const auto scene = dir / "scene_64x32.raw";
    const auto samples = patternSamples(64, 32);
    writeBytes(scene, packLsb16(samples, 64, 128));
    const std::string scenePath = scene.string();

    store.clearAll();
    PlainRawFileInfo info = resolvePlainRawFile(scenePath);
    CHECK(info.resolved.known);
    CHECK(info.resolved.sizeFromName);
    CHECK(info.resolved.check == SizeCheck::Exact);
    CHECK(info.resolved.params.width == 64);
    CHECK(info.resolved.params.height == 32);
    CHECK(info.fileSize == 64 * 32 * 2);
    std::vector<std::uint16_t> got;
    std::string why;
    CHECK(readPlainRawSamples(scenePath, info, got, &why));
    CHECK(got == samples);

    ImageData full = decodePlainRawFile(scenePath, 0, &info, &why);
    CHECK(!full.isNull());
    CHECK(full.width == 64);
    CHECK(full.height == 32);
    CHECK(full.format == PixelFormat::RGB24);
    CHECK(full.buffer && full.buffer->size() == static_cast<std::size_t>(64 * 32 * 3));
    ImageData preview = decodePlainRawFile(scenePath, 16, nullptr, &why);
    CHECK(preview.width == 16);
    CHECK(preview.height == 8);
    CHECK(preview.format == PixelFormat::RGB24);

    ImageMetadata meta;
    fillPlainRawMetadata(info, scenePath, meta);
    CHECK(meta.width == 64);
    CHECK(meta.height == 32);
    CHECK(meta.fileSize == info.fileSize);
    CHECK(meta.fileName == "scene_64x32.raw");
    CHECK(meta.filePath == scenePath);
    CHECK(meta.bitDepth == 10);
    CHECK(meta.channels == 3);
    CHECK(meta.format == "RAW (plain)");
    CHECK(meta.colorSpace == "RGGB 10-bit");
    CHECK(meta.frameCount == 1);
    CHECK(meta.currentFrame == 0);
    CHECK(meta.sequenceKind == "static");
    CHECK(meta.animated == false);

    store.clearAll();
    const auto nosize = dir / "scene.raw";
    writeBytes(nosize, packLsb16(samples, 64, 128));
    const std::string nosizePath = nosize.string();
    info = resolvePlainRawFile(nosizePath);
    CHECK(!info.resolved.known);
    CHECK(contains(info.resolved.why, "image size unknown"));
    Params forced = sized(64, 32);
    store.setOverride(plainRawParamKey(nosizePath), forced);
    info = resolvePlainRawFile(nosizePath);
    CHECK(info.resolved.known);
    CHECK(info.fromOverride);
    CHECK(info.resolved.params.width == 64);
    CHECK(info.resolved.params.height == 32);
    store.clearOverride(plainRawParamKey(nosizePath));
    info = resolvePlainRawFile(nosizePath);
    CHECK(!info.resolved.known);
    CHECK(contains(info.resolved.why, "image size unknown"));

    store.clearAll();
    const auto shortPathFs = dir / "short_64x32.raw";
    writeBytes(shortPathFs, std::vector<std::uint8_t>(100, 0));
    info = resolvePlainRawFile(shortPathFs.string());
    CHECK(!info.resolved.known);
    CHECK(contains(info.resolved.why, "smaller"));

    store.clearAll();
    const auto mipi = dir / "m_64x32.raw";
    const std::size_t mipiBytes = static_cast<std::size_t>(64 * 32 * 5 / 4);
    writeBytes(mipi, std::vector<std::uint8_t>(mipiBytes, 1));
    info = resolvePlainRawFile(mipi.string());
    CHECK(info.resolved.known);
    CHECK(info.resolved.guessed);
    CHECK(info.resolved.params.packing == Packing::Mipi10);
    CHECK(info.resolved.params.bitDepth == 10);

    store.clearAll();
    const auto strideFile = dir / "pad.raw";
    std::vector<std::uint8_t> padded(static_cast<std::size_t>(16 + 160 * 32), 0);
    const auto rowBytes = packLsb16(samples, 64, 160);
    for (std::size_t i = 0; i < rowBytes.size(); ++i)
        padded[16U + i] = rowBytes[i];
    writeBytes(strideFile, padded);
    const std::string stridePath = strideFile.string();
    Params pad = sized(64, 32);
    pad.headerOffset = 16;
    pad.rowStride = 160;
    store.setOverride(plainRawParamKey(stridePath), pad);
    info = resolvePlainRawFile(stridePath);
    CHECK(info.resolved.known);
    CHECK(info.fromOverride);
    CHECK(info.resolved.params.headerOffset == 16);
    CHECK(info.resolved.params.rowStride == 160);
    got.clear();
    CHECK(readPlainRawSamples(stridePath, info, got, &why));
    CHECK(got == samples);

    store.clearAll();
    CHECK(plainRawCacheKeySuffix("a.jpg").empty());
    const std::string suf0 = plainRawCacheKeySuffix(scenePath);
    CHECK(contains(suf0, "|praw="));
    CHECK(contains(suf0, toString(store.defaults())));
    Params next = store.defaults();
    next.gain = 2.0;
    store.setDefaults(next);
    const std::string suf1 = plainRawCacheKeySuffix(scenePath);
    CHECK(suf1 != suf0);
    CHECK(contains(suf1, toString(store.defaults())));
    Params ov = sized(64, 32);
    ov.gain = 4.0;
    store.setOverride(plainRawParamKey(scenePath), ov);
    const std::string suf2 = plainRawCacheKeySuffix(scenePath);
    CHECK(suf2 != suf1);
    CHECK(contains(suf2, toString(ov)));
    CHECK(plainRawCacheKeyAgrees("plain", ""));
    CHECK(!plainRawCacheKeyAgrees("x|praw=abc", ""));
    CHECK(plainRawCacheKeyAgrees("pre" + suf2, suf2));
    CHECK(plainRawCacheKeyAgrees(suf2, suf2));
    CHECK(!plainRawCacheKeyAgrees("pre" + suf2, suf1));
    CHECK(!plainRawCacheKeyAgrees("nope", suf2));

    store.clearAll();
    const int r = static_cast<int>(samples[0]);
    const int g = static_cast<int>(samples[1]);
    const int b = static_cast<int>(samples[static_cast<std::size_t>(64 + 1)]);
    const std::string red = plainRawSampleText(scenePath, 0, 0);
    const std::string green = plainRawSampleText(scenePath, 1, 0);
    const std::string blue = plainRawSampleText(scenePath, 1, 1);
    CHECK(red == "RAW R=" + std::to_string(r) + " (10-bit)");
    CHECK(green == "RAW G=" + std::to_string(g) + " (10-bit)");
    CHECK(blue == "RAW B=" + std::to_string(b) + " (10-bit)");
    CHECK(plainRawSampleText(scenePath, -1, 0).empty());
    CHECK(plainRawSampleText(scenePath, 64, 0).empty());
    CHECK(plainRawSampleText(scenePath, 0, 32).empty());
    CHECK(plainRawSampleText("a.jpg", 0, 0).empty());
    Params mono = sized(64, 32);
    mono.cfa = Cfa::Mono;
    store.setOverride(plainRawParamKey(scenePath), mono);
    const std::string yText = plainRawSampleText(scenePath, 0, 0);
    CHECK(yText == "RAW Y=" + std::to_string(r) + " (10-bit)");
    ImageMetadata monoMeta;
    info = resolvePlainRawFile(scenePath);
    fillPlainRawMetadata(info, scenePath, monoMeta);
    CHECK(monoMeta.channels == 1);
    CHECK(monoMeta.colorSpace == "Mono 10-bit");

    store.clearAll();
    info = resolvePlainRawFile((dir / "missing.raw").string());
    CHECK(!info.resolved.known);
    CHECK(info.fileSize == -1);
    CHECK(contains(info.resolved.why, "cannot open file"));
    CHECK(plainRawSampleText((dir / "missing.raw").string(), 0, 0).empty());

    store.clearAll();
    const auto depthFile = dir / "depth_64x32.raw";
    std::vector<std::uint16_t> ramp(static_cast<std::size_t>(64 * 32));
    const int rampLast = static_cast<int>(ramp.size()) - 1;
    for (int i = 0; i <= rampLast; ++i)
    {
        const int value = static_cast<int>((static_cast<int64_t>(i) * 4000) / rampLast);
        ramp[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(value);
    }
    writeBytes(depthFile, packLsb16(ramp, 64, 128));
    const std::string depthPath = depthFile.string();
    PlainRawFileInfo depthInfo;
    ImageData depthImage = decodePlainRawFile(depthPath, 0, &depthInfo, &why);
    CHECK(!depthImage.isNull());
    CHECK(depthInfo.resolved.params.bitDepth == 12);
    CHECK(!depthInfo.depthNote.empty());
    CHECK(contains(depthInfo.depthNote, "read as"));
    ImageMetadata depthMeta;
    fillPlainRawMetadata(depthInfo, depthPath, depthMeta);
    CHECK(depthMeta.bitDepth == 12);
    CHECK(contains(depthMeta.colorSpace, depthInfo.depthNote));
    CHECK(depthMeta.textKeys["MViewer.PlainRaw.Note"] == depthInfo.depthNote);

    Params depthOver = sized(64, 32);
    store.setOverride(plainRawParamKey(depthPath), depthOver);
    depthInfo = resolvePlainRawFile(depthPath);
    CHECK(depthInfo.fromOverride);
    std::vector<std::uint16_t> clipped;
    CHECK(readPlainRawSamples(depthPath, depthInfo, clipped, &why));
    std::uint16_t peak = 0;
    for (std::uint16_t sample : clipped)
    {
        if (sample > peak)
            peak = sample;
    }
    CHECK(peak == 1023);
    CHECK(depthInfo.resolved.params.bitDepth == 10);
    CHECK(contains(depthInfo.depthNote, "clipped"));
    store.clearAll();

#ifndef PLAINRAW_NO_REGISTRY
    {
        // ADR 033: the default registry sends .raw to PlainRawDecoder, never LibRaw.
        store.clearAll();
        DecoderRegistry::instance().resetToDefaults();
        ImageMetadata decodedMeta;
        const auto decoded = DecoderRegistry::instance().decodeFullResult(scenePath, decodedMeta);
        CHECK(decoded.isOk());
        if (decoded.isOk())
            CHECK(decoded.value().width == 64 && decoded.value().height == 32);
        CHECK(decodedMeta.format == "RAW (plain)");
        PlainRawDecoder decoder;
        CHECK(decoder.canDecode("dir/B.RAW") && !decoder.canDecode("dir/b.cr2"));
        ImageMetadata unknownMeta;
        CHECK(!decoder.decodeFullResult(nosizePath, unknownMeta).isOk());
        CHECK(decoder.decodeScaled(nosizePath, 128).isNull());
        CHECK(mviewer::core::ImageFormats::isSupportedSuffix("raw"));
        CHECK(!mviewer::core::isRawExtension("raw"));

        const auto nosizeRaw = dir / "nosize.raw";
        writeBytes(nosizeRaw, std::vector<std::uint8_t>(100, 0));
        const auto nosizeRes = DecoderRegistry::instance().decodeFullResult(nosizeRaw.string());
        CHECK(!nosizeRes.isOk());
        if (!nosizeRes.isOk())
            CHECK(contains(nosizeRes.error().message, "RAW parameters"));

        const auto trunc = dir / "trunc_64x32.raw";
        writeBytes(trunc, std::vector<std::uint8_t>(10, 0));
        const auto truncRes = DecoderRegistry::instance().decodeFullResult(trunc.string());
        CHECK(!truncRes.isOk());
        if (!truncRes.isOk())
            CHECK(contains(truncRes.error().message, "smaller than one frame"));

        testPlainRawCapabilities(scenePath, nosizePath);
    }
#endif

    testSampleReadCache(dir);
    testGrayDump(dir);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    store.clearAll();

    if (g_failures != 0)
    {
        std::printf("test_plainraw_file: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_plainraw_file: all passed\n");
    return 0;
}
// NOLINTEND
