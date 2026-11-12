// NOLINTBEGIN
// Multi-frame YUV navigation, frame info, and cache-key identity.

#include "core/image/FrameSequence.h"
#include "core/image/yuv/Y4mHeader.h"
#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvFormat.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using mviewer::core::FrameSequenceKind;
using mviewer::core::FrameSequenceReader;
using mviewer::core::yuv::applyPixFmtName;
using mviewer::core::yuv::Format;
using mviewer::core::yuv::isYuvPath;
using mviewer::core::yuv::makeY4mHeader;
using mviewer::core::yuv::yuvCacheKeyAgrees;
using mviewer::core::yuv::yuvCacheKeySuffix;
using mviewer::core::yuv::yuvParamKey;
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

static std::vector<std::uint8_t> yuv420Frame(int width, int height, std::uint8_t y)
{
    const int chromaW = (width + 1) / 2;
    const int chromaH = (height + 1) / 2;
    const std::size_t yBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t cBytes =
        static_cast<std::size_t>(chromaW) * static_cast<std::size_t>(chromaH);
    std::vector<std::uint8_t> frame(yBytes + cBytes + cBytes, 128);
    for (std::size_t i = 0; i < yBytes; ++i)
        frame[i] = y;
    return frame;
}

static void testIsYuvPath()
{
    CHECK(isYuvPath("clip.yuv"));
    CHECK(isYuvPath("clip.YUV"));
    CHECK(isYuvPath("a/b.y4m"));
    CHECK(isYuvPath("A.Y4M"));
    CHECK(isYuvPath("x.y"));
    CHECK(isYuvPath("x.Y"));
    CHECK(!isYuvPath("x.gray"));
    CHECK(!isYuvPath("x.GRAY"));
    CHECK(!isYuvPath("x.grey"));
    CHECK(!isYuvPath("x.GREY"));
    CHECK(!isYuvPath("clip.yuv.bak"));
    CHECK(!isYuvPath("clip.png"));
    CHECK(!isYuvPath("yuv"));
    CHECK(!isYuvPath(""));
}

static void testCacheKey()
{
    const std::string bare = "/tmp/no-such-seq.yuv";
    // Unsaved raw files key on the extension mapping that drives inference.
    const std::string autoSuffix = yuvCacheKeySuffix(bare);
    CHECK(autoSuffix.rfind("|yuv=auto:", 0) == 0);
    CHECK(yuvCacheKeySuffix("/tmp/no-such.png").empty());
    CHECK(yuvCacheKeySuffix("/tmp/no-such.y4m").empty());
    CHECK(yuvCacheKeyAgrees("rev", ""));
    CHECK(!yuvCacheKeyAgrees("rev|yuv=saved", ""));
    const std::string suffix = "|yuv=w16";
    CHECK(yuvCacheKeyAgrees("rev" + suffix, suffix));
    CHECK(!yuvCacheKeyAgrees("rev|yuv=other", suffix));

    Format format;
    format.width = 16;
    format.height = 10;
    CHECK(applyPixFmtName("yuv444p", format));
    const auto path = g_root / "saved_key.yuv";
    writeBytes(path, std::vector<std::uint8_t>(16, 0));
    const std::string keyPath = utf8(path);
    CHECK(yuvCacheKeySuffix(keyPath) == autoSuffix);
    YuvParamStore::instance().set(yuvParamKey(keyPath), format);
    const std::string saved = yuvCacheKeySuffix(keyPath);
    CHECK(saved.rfind("|yuv=", 0) == 0);
    CHECK(saved.size() > 5);
    CHECK(saved != autoSuffix);
    CHECK(yuvCacheKeyAgrees("abc" + saved, saved));
    CHECK(!yuvCacheKeyAgrees("abc", saved));
    YuvParamStore::instance().clear(yuvParamKey(keyPath));
    CHECK(yuvCacheKeySuffix(keyPath) == autoSuffix);
}

static void testSequence()
{
    const auto path = g_root / "seq_16x10.yuv";
    std::vector<std::uint8_t> bytes;
    const std::uint8_t levels[3] = {16, 128, 235};
    for (const std::uint8_t y : levels)
    {
        const auto frame = yuv420Frame(16, 10, y);
        CHECK(frame.size() == 240);
        bytes.insert(bytes.end(), frame.begin(), frame.end());
    }
    bytes.insert(bytes.end(), 7, static_cast<std::uint8_t>(0));
    CHECK(bytes.size() == 727);
    writeBytes(path, bytes);
    const std::string keyPath = utf8(path);

    const auto probed = FrameSequenceReader::probe(keyPath);
    CHECK(probed.valid);
    CHECK(probed.kind == FrameSequenceKind::Pages);
    CHECK(probed.frameCount == 3);
    CHECK(!probed.animated);

    const auto mid = FrameSequenceReader::decodeFrame(keyPath, 1);
    CHECK(mid.ok);
    CHECK(mid.pixels.width == 16);
    CHECK(mid.pixels.height == 10);
    const auto px = samplePixel(mid.pixels, 0, 0);
    CHECK(px.valid);
    CHECK(px.r == px.g && px.g == px.b);

    const auto black = FrameSequenceReader::decodeFrame(keyPath, 0);
    const auto white = FrameSequenceReader::decodeFrame(keyPath, 2);
    CHECK(black.ok);
    CHECK(white.ok);
    const auto px0 = samplePixel(black.pixels, 0, 0);
    const auto px2 = samplePixel(white.pixels, 0, 0);
    CHECK(px0.valid && px0.r == 0 && px0.g == 0 && px0.b == 0);
    CHECK(px2.valid && px2.r == 255 && px2.g == 255 && px2.b == 255);
    CHECK(px.r > px0.r && px.r < px2.r);

    const auto oor = FrameSequenceReader::decode(keyPath, 3);
    CHECK(!oor.ok);
    CHECK(!oor.error.empty());

    const auto scaled = FrameSequenceReader::decode(keyPath, 1, 4);
    CHECK(scaled.ok);
    const int longest =
        scaled.pixels.width > scaled.pixels.height ? scaled.pixels.width : scaled.pixels.height;
    CHECK(longest == 4);

    const auto info2 = FrameSequenceReader::frameInfo(keyPath, 2);
    CHECK(info2.index == 2);
    CHECK(info2.durationMs == 0);
    CHECK(info2.width == 16);
    CHECK(info2.height == 10);
    const auto info5 = FrameSequenceReader::frameInfo(keyPath, 5);
    CHECK(info5.width == 0);

    const std::string before = FrameSequenceReader::decodeFrame(keyPath, 0).identity.fileRevision;
    Format y444;
    y444.width = 16;
    y444.height = 10;
    CHECK(applyPixFmtName("yuv444p", y444));
    YuvParamStore::instance().set(yuvParamKey(keyPath), y444);
    const auto afterProbe = FrameSequenceReader::probe(keyPath);
    CHECK(afterProbe.valid);
    CHECK(afterProbe.frameCount == 1);
    const std::string after = FrameSequenceReader::decodeFrame(keyPath, 0).identity.fileRevision;
    CHECK(before != after);
    YuvParamStore::instance().clear(yuvParamKey(keyPath));
}

static void testUnknown()
{
    const auto path = g_root / "mystery.yuv";
    writeBytes(path, std::vector<std::uint8_t>(1000, 16));
    const std::string keyPath = utf8(path);
    const auto probed = FrameSequenceReader::probe(keyPath);
    CHECK(probed.valid == false);
    const auto decoded = FrameSequenceReader::decodeFrame(keyPath, 0);
    CHECK(!decoded.ok);
    CHECK(!decoded.error.empty());
}

static void testY4m()
{
    Format format;
    format.width = 4;
    format.height = 2;
    CHECK(applyPixFmtName("yuv420p", format));
    const std::string header = makeY4mHeader(format);
    CHECK(!header.empty());
    const auto frame = yuv420Frame(4, 2, 235);
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    const char frameTag[] = "FRAME\n";
    for (int i = 0; i < 2; ++i)
    {
        bytes.insert(bytes.end(), frameTag, frameTag + 6);
        bytes.insert(bytes.end(), frame.begin(), frame.end());
    }
    const auto path = g_root / "two.y4m";
    writeBytes(path, bytes);
    const auto probed = FrameSequenceReader::probe(utf8(path));
    CHECK(probed.valid);
    CHECK(probed.frameCount == 2);
    CHECK(probed.kind == FrameSequenceKind::Pages);
}

int main()
{
    std::error_code ec;
    g_root = std::filesystem::temp_directory_path() / "mviewer_yuv_seq_test";
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);
    CHECK(!ec);

    testIsYuvPath();
    testCacheKey();
    testSequence();
    testUnknown();
    testY4m();

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
