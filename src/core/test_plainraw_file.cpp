// NOLINTBEGIN
#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/rawplain/PlainRawUnpack.h"
#ifndef PLAINRAW_NO_REGISTRY
#include "core/image/ImageFormats.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/PlainRawDecoder.h"
#endif

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
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
    }
#endif

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
