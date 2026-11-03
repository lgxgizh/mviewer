// NOLINTBEGIN
// Unit tests for YuvDecoder (ADR 032): extension claim, resolved frames,
// unknown parameters, Y4M, truncation, and registry order.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "core/image/decoder/YuvDecoder.h"
#pragma clang diagnostic pop
#include "core/image/yuv/Y4mHeader.h"
#include "core/image/yuv/YuvFormat.h"

#ifndef YUV_DECODER_NO_REGISTRY
#include "core/image/decoder/DecoderRegistry.h"
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using mviewer::core::yuv::applyPixFmtName;
using mviewer::core::yuv::Format;
using mviewer::core::yuv::Layout;
using mviewer::core::yuv::makeY4mHeader;
using mviewer::core::yuv::Subsampling;
using mviewer::core::yuv::YuvParamStore;

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

static std::filesystem::path g_root;

static std::string utf8(const std::filesystem::path &path)
{
    return path.string();
}

static void writeBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(path, std::ios::binary);
    CHECK(out.good());
    if (!bytes.empty())
    {
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    CHECK(out.good());
}

static std::vector<std::uint8_t> yuv420Frame(int width, int height)
{
    const int chromaW = (width + 1) / 2;
    const int chromaH = (height + 1) / 2;
    const std::size_t yBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t cBytes =
        static_cast<std::size_t>(chromaW) * static_cast<std::size_t>(chromaH);
    std::vector<std::uint8_t> frame(yBytes + cBytes + cBytes, 128);
    for (std::size_t i = 0; i < yBytes; ++i)
    {
        frame[i] = 235;
    }
    return frame;
}

static void testCanDecode()
{
    YuvDecoder decoder;
    CHECK(decoder.canDecode("a.yuv"));
    CHECK(decoder.canDecode("A.Y4M"));
    CHECK(!decoder.canDecode("x.yuvx"));
    CHECK(!decoder.canDecode("x.png"));
    const auto ext = decoder.extensions();
    CHECK(ext.size() == 2);
    CHECK(ext[0] == "yuv");
    CHECK(ext[1] == "y4m");
    CHECK(std::string(decoder.name()) == "YuvDecoder");
}

static void testClip4x2()
{
    // Parent folder carries a 2-digit WxH token; "4x2" alone is one digit per side.
    const auto path = g_root / "04x02" / "clip_4x2.yuv";
    std::filesystem::create_directories(path.parent_path());
    const auto frame = yuv420Frame(4, 2);
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 3; ++i)
    {
        bytes.insert(bytes.end(), frame.begin(), frame.end());
    }
    writeBytes(path, bytes);

    YuvDecoder decoder;
    mviewer::domain::ImageMetadata meta;
    const ImageData image = decoder.decodeFull(utf8(path), meta);
    CHECK(!image.isNull());
    CHECK(image.width == 4);
    CHECK(image.height == 2);
    CHECK(image.format == PixelFormat::RGB24);
    CHECK(image.buffer && image.buffer->size() == 24);
    if (image.buffer)
    {
        for (const auto byte : *image.buffer)
        {
            CHECK(byte == 255);
        }
    }
    CHECK(meta.width == 4);
    CHECK(meta.height == 2);
    CHECK(meta.format == "YUV");
    CHECK(meta.colorSpace.find("709") != std::string::npos);
    CHECK(meta.frameCount == 3);
    CHECK(meta.sequenceKind == "pages");
}

static void testScaled()
{
    const auto path = g_root / "clip_64x32.yuv";
    writeBytes(path, yuv420Frame(64, 32));
    YuvDecoder decoder;
    mviewer::domain::ImageMetadata meta;
    const ImageData image = decoder.decodeScaled(utf8(path), 16, meta);
    CHECK(!image.isNull());
    CHECK(image.width == 16);
    CHECK(image.height == 8);
    CHECK(meta.width == 64);
    CHECK(meta.height == 32);
    CHECK(meta.frameCount == 1);
    CHECK(meta.sequenceKind == "static");
}

static void testUnknownThenSaved()
{
    const auto path = g_root / "mystery.yuv";
    writeBytes(path, std::vector<std::uint8_t>(1000, 16));
    const std::string keyPath = utf8(path);
    YuvDecoder decoder;
    mviewer::domain::ImageMetadata meta;
    const ImageData unknown = decoder.decodeFull(keyPath, meta);
    CHECK(unknown.isNull());
    const auto result = decoder.decodeFullResult(keyPath, meta);
    CHECK(!result.isOk());
    CHECK(!decoder.probeMetadata(keyPath, meta));

    Format gray;
    gray.width = 10;
    gray.height = 10;
    gray.layout = Layout::Planar;
    gray.subsampling = Subsampling::S400;
    gray.bitDepth = 8;
    YuvParamStore::instance().set(YuvDecoder::paramKey(keyPath), gray);
    const ImageData known = decoder.decodeFull(keyPath);
    CHECK(!known.isNull());
    CHECK(known.width == 10);
    CHECK(known.height == 10);
    mviewer::domain::ImageMetadata probed;
    CHECK(decoder.probeMetadata(keyPath, probed));
    CHECK(probed.width == 10);
    CHECK(probed.height == 10);
    CHECK(probed.channels == 1);
    YuvParamStore::instance().clear(YuvDecoder::paramKey(keyPath));
}

static void testY4m()
{
    Format format;
    format.width = 4;
    format.height = 2;
    CHECK(applyPixFmtName("yuv420p", format));
    const std::string header = makeY4mHeader(format);
    CHECK(!header.empty());
    const auto frame = yuv420Frame(4, 2);
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    const char frameTag[] = "FRAME\n";
    bytes.insert(bytes.end(), frameTag, frameTag + 6);
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    const auto path = g_root / "clip.y4m";
    writeBytes(path, bytes);

    YuvDecoder decoder;
    mviewer::domain::ImageMetadata meta;
    const ImageData image = decoder.decodeFull(utf8(path), meta);
    CHECK(!image.isNull());
    CHECK(image.width == 4);
    CHECK(image.height == 2);
    CHECK(meta.format == "Y4M");
    CHECK(meta.width == 4);
    CHECK(meta.height == 2);
}

static void testTruncatedAndMissing()
{
    const auto path = g_root / "t_4x2.yuv";
    auto frame = yuv420Frame(4, 2);
    frame.resize(11);
    writeBytes(path, frame);
    YuvDecoder decoder;
    CHECK(decoder.decodeFull(utf8(path)).isNull());

    bool threw = false;
    try
    {
        const ImageData missing = decoder.decodeFull(utf8(g_root / "no_such_file.yuv"));
        CHECK(missing.isNull());
    }
    catch (...)
    {
        threw = true;
    }
    CHECK(!threw);
}

#ifndef YUV_DECODER_NO_REGISTRY
static void testRegistryOrder()
{
    auto &registry = DecoderRegistry::instance();
    registry.resetToDefaults();
    const auto names = registry.available();
    int raw = -1;
    int yuv = -1;
    int qt = -1;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == "RawDecoder")
        {
            raw = static_cast<int>(i);
        }
        else if (names[i] == "YuvDecoder")
        {
            yuv = static_cast<int>(i);
        }
        else if (names[i] == "QtDecoder")
        {
            qt = static_cast<int>(i);
        }
    }
    CHECK(raw >= 0);
    CHECK(yuv == raw + 1);
    CHECK(qt == yuv + 1);
}
#endif

int main()
{
    std::error_code ec;
    g_root = std::filesystem::temp_directory_path() / "mviewer_yuv_decoder_test";
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);
    CHECK(!ec);

    testCanDecode();
    testClip4x2();
    testScaled();
    testUnknownThenSaved();
    testY4m();
    testTruncatedAndMissing();
#ifndef YUV_DECODER_NO_REGISTRY
    testRegistryOrder();
#endif

    std::filesystem::remove_all(g_root, ec);
    if (g_failures != 0)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
// NOLINTEND
