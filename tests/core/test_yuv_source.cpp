// NOLINTBEGIN
// Unit tests for YuvSource: parameter store, raw/y4m resolution, frame reads, RGB render.

#include "core/image/yuv/YuvScaledRender.h"
#include "core/image/yuv/YuvSource.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace mviewer::core::yuv;

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

ReadAtFn readerFor(const std::vector<uint8_t> &file)
{
    return [&file](int64_t offset, uint8_t *buf, size_t n) -> size_t
    {
        if (buf == nullptr || offset < 0 || n == 0)
        {
            return 0;
        }
        const uint64_t begin = static_cast<uint64_t>(offset);
        if (begin >= file.size())
        {
            return 0;
        }
        const size_t avail = file.size() - static_cast<size_t>(begin);
        const size_t take = n < avail ? n : avail;
        std::memcpy(buf, file.data() + static_cast<size_t>(begin), take);
        return take;
    };
}

void appendBytes(std::vector<uint8_t> &dst, const void *data, size_t n)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    dst.insert(dst.end(), bytes, bytes + n);
}

void appendStr(std::vector<uint8_t> &dst, const std::string &text)
{
    appendBytes(dst, text.data(), text.size());
}

Format makeFmt(const char *name, int w, int h)
{
    Format f;
    f.width = w;
    f.height = h;
    CHECK(applyPixFmtName(name, f));
    return f;
}

std::vector<uint8_t> solidFrame(const Format &f, uint8_t y, uint8_t u, uint8_t v)
{
    const auto layout = computeFrameLayout(f);
    CHECK(layout.has_value());
    std::vector<uint8_t> frame(static_cast<size_t>(layout->frameBytes), y);
    if (f.subsampling == Subsampling::S400 || f.bitDepth != 8 || f.layout != Layout::Planar)
    {
        return frame;
    }
    const size_t yBytes = static_cast<size_t>(layout->planes[0].bytes);
    const size_t cBytes = static_cast<size_t>(layout->planes[1].bytes);
    if (yBytes + cBytes * 2 == frame.size())
    {
        std::fill(frame.begin() + static_cast<std::ptrdiff_t>(yBytes),
                  frame.begin() + static_cast<std::ptrdiff_t>(yBytes + cBytes), u);
        std::fill(frame.begin() + static_cast<std::ptrdiff_t>(yBytes + cBytes), frame.end(), v);
    }
    return frame;
}

YuvPlanes planesFilled(const Format &f, uint16_t y, uint16_t u, uint16_t v)
{
    YuvPlanes p;
    p.width = f.width;
    p.height = f.height;
    p.bitDepth = f.bitDepth;
    p.y.assign(static_cast<size_t>(f.width) * static_cast<size_t>(f.height), y);
    if (f.subsampling != Subsampling::S400)
    {
        p.chromaWidth = chromaWidth(f);
        p.chromaHeight = chromaHeight(f);
        const size_t n = static_cast<size_t>(p.chromaWidth) * static_cast<size_t>(p.chromaHeight);
        p.u.assign(n, u);
        p.v.assign(n, v);
    }
    return p;
}

void testStore()
{
    YuvParamStore &store = YuvParamStore::instance();
    store.clearAll();
    const uint64_t rev0 = store.revision();
    Format a = makeFmt("yuv420p", 16, 16);
    Format b = makeFmt("yuv422p", 8, 8);
    store.set("clip-a", a);
    store.set("clip-b", b);
    CHECK(store.revision() == rev0 + 2);
    const auto got = store.get("clip-a");
    CHECK(got.has_value() && *got == a);
    CHECK(!store.get("missing").has_value());
    store.clear("clip-a");
    CHECK(!store.get("clip-a").has_value());
    CHECK(store.get("clip-b").has_value());
    store.clearAll();
    CHECK(!store.get("clip-b").has_value());
    CHECK(store.revision() > rev0);

    std::atomic<int> writes{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            [t, &writes]()
            {
                YuvParamStore &s = YuvParamStore::instance();
                for (int i = 0; i < 50; ++i)
                {
                    Format f;
                    f.width = 4 + t;
                    f.height = 2 + (i & 1);
                    f.subsampling = Subsampling::S420;
                    f.bitDepth = 8;
                    const std::string key = "t" + std::to_string(t);
                    s.set(key, f);
                    const auto g = s.get(key);
                    if (g.has_value() && g->width == 4 + t)
                    {
                        writes.fetch_add(1);
                    }
                }
            });
    }
    for (std::thread &th : threads)
    {
        th.join();
    }
    CHECK(writes.load() == 200);
    for (int t = 0; t < 4; ++t)
    {
        const auto g = store.get("t" + std::to_string(t));
        CHECK(g.has_value());
        CHECK(g->width == 4 + t);
        CHECK(g->height == 2 || g->height == 3);
    }
    store.clearAll();
}

void testRaw()
{
    const Format f = makeFmt("yuv420p", 4, 2);
    const std::vector<uint8_t> one = solidFrame(f, 16, 128, 128);
    CHECK(one.size() == 12);
    std::vector<uint8_t> file;
    for (int i = 0; i < 3; ++i)
    {
        std::vector<uint8_t> frame = one;
        frame[0] = static_cast<uint8_t>(10 + i);
        file.insert(file.end(), frame.begin(), frame.end());
    }
    file.insert(file.end(), 5, 0xAB);
    const ReadAtFn read = readerFor(file);
    const YuvSourceInfo info = resolveYuvSource(
        "clip_4x2.yuv", "04x02", static_cast<int64_t>(file.size()), read, std::nullopt);
    CHECK(info.known);
    CHECK(!info.isY4m);
    CHECK(info.source == ParamSource::Inferred);
    CHECK(info.frameBytes == 12);
    CHECK(info.frameCount == 3);
    CHECK(info.trailingBytes == 5);
    CHECK(info.dataOffset == 0);
    CHECK(yuvFrameOffset(info, 0) == 0);
    CHECK(yuvFrameOffset(info, 1) == 12);
    CHECK(yuvFrameOffset(info, 2) == 24);
    CHECK(yuvFrameOffset(info, 3) == -1);
    CHECK(yuvFrameOffset(info, -1) == -1);

    YuvPlanes planes;
    std::string why;
    CHECK(readYuvFrame(info, 1, read, planes, &why));
    CHECK(planes.width == 4 && planes.height == 2);
    CHECK(!planes.y.empty() && planes.y[0] == 11);

    ReadAtFn shortRead = [&file](int64_t offset, uint8_t *buf, size_t n) -> size_t
    {
        if (n > 0)
        {
            --n;
        }
        return readerFor(file)(offset, buf, n);
    };
    CHECK(!readYuvFrame(info, 0, shortRead, planes, &why));
    CHECK(!why.empty());

    std::vector<uint8_t> tiny(5, 1);
    const YuvSourceInfo small =
        resolveYuvSource("clip_4x2.yuv", "04x02", 5, readerFor(tiny), std::nullopt);
    CHECK(!small.known);
    CHECK(small.why == "file is smaller than one frame");

    std::vector<uint8_t> ambig(100, 0);
    const YuvSourceInfo unknown = resolveYuvSource(
        "clip.yuv", "misc", static_cast<int64_t>(ambig.size()), readerFor(ambig), std::nullopt);
    CHECK(!unknown.known);
    CHECK(!unknown.why.empty());
    CHECK(unknown.format.subsampling == Subsampling::S420);
    CHECK(unknown.format.matrix == Matrix::BT709);
    CHECK(unknown.format.range == Range::Limited);
    CHECK(unknown.format.bitDepth == 8);

    Format saved = makeFmt("yuv420p", 4, 2);
    const YuvSourceInfo withSaved = resolveYuvSource("clip.yuv", "misc", 12 * 2 + 3,
                                                     readerFor(std::vector<uint8_t>(27, 0)), saved);
    CHECK(withSaved.known);
    CHECK(withSaved.source == ParamSource::Saved);
    CHECK(withSaved.frameCount == 2);
    CHECK(withSaved.trailingBytes == 3);
    CHECK(withSaved.format.width == 4 && withSaved.format.height == 2);
}

std::vector<uint8_t> buildY4m(const Format &f, int frames, bool truncateLast)
{
    std::vector<uint8_t> file;
    appendStr(file, makeY4mHeader(f, 30, 1));
    const auto layout = computeFrameLayout(f);
    const size_t fb = layout ? static_cast<size_t>(layout->frameBytes) : 0;
    for (int i = 0; i < frames; ++i)
    {
        appendStr(file, "FRAME\n");
        std::vector<uint8_t> pix(fb, static_cast<uint8_t>(i + 1));
        if (f.bitDepth > 8)
        {
            for (size_t b = 0; b < pix.size(); b += 2)
            {
                pix[b] = static_cast<uint8_t>(i + 1);
                pix[b + 1] = 0;
            }
        }
        if (truncateLast && i + 1 == frames && !pix.empty())
        {
            pix.pop_back();
        }
        file.insert(file.end(), pix.begin(), pix.end());
    }
    return file;
}

void testY4m()
{
    Format jpeg = makeFmt("yuv420p", 4, 2);
    jpeg.siting = ChromaSiting::Center; // C420jpeg
    CHECK(makeY4mHeader(jpeg).find("C420jpeg") != std::string::npos);
    const std::vector<uint8_t> file = buildY4m(jpeg, 3, false);
    Format saved = makeFmt("yuv444p", 32, 32);
    const YuvSourceInfo info = resolveYuvSource(
        "clip.y4m", "media", static_cast<int64_t>(file.size()), readerFor(file), saved);
    CHECK(info.known);
    CHECK(info.isY4m);
    CHECK(info.source == ParamSource::Y4mHeader);
    CHECK(info.format.width == 4 && info.format.height == 2);
    CHECK(info.format.subsampling == Subsampling::S420);
    CHECK(info.frameCount == 3);
    CHECK(info.fpsNum == 30 && info.fpsDen == 1);
    CHECK(info.y4mFrameOffsets.size() == 3);
    const std::string header = makeY4mHeader(jpeg, 30, 1);
    const int64_t frameBytes = info.frameBytes;
    CHECK(info.y4mFrameOffsets[0] == static_cast<int64_t>(header.size() + 6));
    CHECK(info.y4mFrameOffsets[1] == info.y4mFrameOffsets[0] + frameBytes + 6);
    CHECK(info.y4mFrameOffsets[2] == info.y4mFrameOffsets[1] + frameBytes + 6);

    YuvPlanes planes;
    CHECK(readYuvFrame(info, 2, readerFor(file), planes, nullptr));
    CHECK(!planes.y.empty() && planes.y[0] == 3);

    const std::vector<uint8_t> cut = buildY4m(jpeg, 3, true);
    const YuvSourceInfo trunc =
        resolveYuvSource("cut.Y4M", "", static_cast<int64_t>(cut.size()), readerFor(cut), saved);
    CHECK(trunc.known);
    CHECK(trunc.frameCount == 2);

    Format p10 = makeFmt("yuv420p10le", 4, 2);
    const std::vector<uint8_t> file10 = buildY4m(p10, 3, false);
    const YuvSourceInfo info10 = resolveYuvSource(
        "ten.y4m", "", static_cast<int64_t>(file10.size()), readerFor(file10), std::nullopt);
    CHECK(info10.known);
    CHECK(info10.frameCount == 3);
    CHECK(info10.format.bitDepth == 10);
    CHECK(readYuvFrame(info10, 0, readerFor(file10), planes, nullptr));
    CHECK(planes.y[0] == 1);

    std::vector<uint8_t> bad = {'Y', 'U', 'V', '4', 'M', 'P', 'E', 'G', '2', ' ', 'W', 'x', '\n'};
    const YuvSourceInfo corrupt = resolveYuvSource("bad.y4m", "", static_cast<int64_t>(bad.size()),
                                                   readerFor(bad), std::nullopt);
    CHECK(!corrupt.known);
    CHECK(corrupt.isY4m);
    CHECK(!corrupt.why.empty());
    CHECK(isY4mFileName("Foo.Y4M"));
    CHECK(!isY4mFileName("Foo.yuv"));
    CHECK(!isY4mFileName("y4m"));
}

void testRender()
{
    const Format f = makeFmt("yuv420p", 8, 4);
    const YuvPlanes white = planesFilled(f, 235, 128, 128);
    const ImageData full = renderYuvImage(white, f, 0);
    CHECK(full.format == PixelFormat::RGB24);
    CHECK(full.width == 8 && full.height == 4);
    CHECK(full.buffer && full.buffer->size() == 8u * 4u * 3u);
    bool allWhite = full.buffer != nullptr;
    if (full.buffer)
    {
        for (uint8_t b : *full.buffer)
        {
            if (b != 255)
            {
                allWhite = false;
            }
        }
    }
    CHECK(allWhite);

    const Format big = makeFmt("yuv420p", 64, 32);
    const YuvPlanes mid = planesFilled(big, 16, 128, 128);
    const ImageData thumb = renderYuvImage(mid, big, 16);
    CHECK(thumb.width == 16 && thumb.height == 8);
    CHECK(thumb.format == PixelFormat::RGB24);
    CHECK(thumb.buffer && thumb.buffer->size() == 16u * 8u * 3u);

    const Format odd = makeFmt("yuv420p", 3, 1000);
    const YuvPlanes tallP = planesFilled(odd, 16, 128, 128);
    const ImageData slim = renderYuvImage(tallP, odd, 10);
    CHECK(slim.width >= 1);
    CHECK(slim.height == 10);

    const ImageData same = renderYuvImage(mid, big, 1000);
    CHECK(same.width == 64 && same.height == 32);

    const ImageData empty = renderYuvImage(YuvPlanes{}, f, 0);
    CHECK(empty.buffer == nullptr || empty.isNull());
}

bool nearMidGray(const ImageData &image)
{
    if (image.buffer == nullptr || image.buffer->empty())
    {
        return false;
    }
    for (uint8_t byte : *image.buffer)
    {
        const int v = byte;
        if (v < 126 || v > 130)
        {
            return false;
        }
    }
    return true;
}

bool sameBytes(const ImageData &image, const std::vector<uint8_t> &rgb)
{
    return image.buffer != nullptr && *image.buffer == rgb;
}

void testScaledLumaBox()
{
    Format row = makeFmt("gray", 4, 1);
    row.range = Range::Full;
    YuvPlanes stripe = planesFilled(row, 0, 0, 0);
    stripe.y = {0, 255, 0, 255};
    const ImageData down =
        scaled_detail::renderScaledPlanes(stripe, row, 2, 1, ChromaUpsample::Nearest);
    CHECK(down.width == 2 && down.height == 1);
    CHECK(nearMidGray(down));

    Format wide = makeFmt("gray", 8, 2);
    wide.range = Range::Full;
    YuvPlanes grid = planesFilled(wide, 0, 0, 0);
    for (int i = 0; i < 16; ++i)
    {
        grid.y[static_cast<size_t>(i)] = (i % 2) == 0 ? 0 : 255;
    }
    const ImageData thumb =
        scaled_detail::renderScaledPlanes(grid, wide, 2, 2, ChromaUpsample::Bilinear);
    CHECK(thumb.width == 2 && thumb.height == 2);
    CHECK(nearMidGray(thumb));

    const ImageData up =
        scaled_detail::renderScaledPlanes(stripe, row, 8, 1, ChromaUpsample::Nearest);
    CHECK(up.buffer && (*up.buffer)[0] == 0 && (*up.buffer)[6] == 255);

    const auto layout = computeFrameLayout(row);
    CHECK(layout.has_value());
    YuvSourceInfo info;
    info.known = true;
    info.format = row;
    info.frameBytes = layout ? layout->frameBytes : 0;
    info.frameCount = 1;
    const std::vector<uint8_t> file = {0, 255, 0, 255};
    const ImageData raw = readYuvFrameImage(info, 0, readerFor(file), 2, ChromaUpsample::Nearest);
    CHECK(raw.width == 2 && raw.height == 1);
    CHECK(nearMidGray(raw));
}

void testScaledMatchesFull()
{
    Format f = makeFmt("yuv420p", 4, 2);
    f.range = Range::Full;
    f.siting = ChromaSiting::Left;
    YuvPlanes pic = planesFilled(f, 128, 40, 80);
    pic.y = {10, 20, 30, 40, 50, 60, 70, 80};
    pic.u = {40, 200};
    pic.v = {80, 160};
    for (const ChromaUpsample up : {ChromaUpsample::Nearest, ChromaUpsample::Bilinear})
    {
        const ImageData scaled = scaled_detail::renderScaledPlanes(pic, f, 4, 2, up);
        CHECK(sameBytes(scaled, toRgb24(pic, f, up)));
    }
}

void testFuzz()
{
    std::mt19937 rng(0x594u);
    std::uniform_int_distribution<int> lenDist(0, 180);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> nameDist(0, 7);
    const char *names[] = {"clip.yuv", "a.y4m",       "foo_4x2.yuv", "x.Y4M",
                           "bad",      "320x240.yuv", "clip.Y4m",    "noext"};
    for (int i = 0; i < 2000; ++i)
    {
        const int n = lenDist(rng);
        std::vector<uint8_t> file(static_cast<size_t>(n));
        for (int b = 0; b < n; ++b)
        {
            file[static_cast<size_t>(b)] = static_cast<uint8_t>(byteDist(rng));
        }
        std::string name = names[nameDist(rng)];
        if (name.size() >= 4 &&
            (name.ends_with("y4m") || name.ends_with("Y4M") || name.ends_with("Y4m")))
        {
            const std::string magic = "YUV4MPEG2 W4 H2 F25:1 Ip C420jpeg\n";
            if (file.size() < magic.size())
            {
                file.resize(magic.size());
            }
            std::memcpy(file.data(), magic.data(), magic.size());
            if (!file.empty())
            {
                const size_t at = static_cast<size_t>(byteDist(rng)) % file.size();
                file[at] = static_cast<uint8_t>(byteDist(rng));
            }
        }
        const ReadAtFn read = readerFor(file);
        const YuvSourceInfo info =
            resolveYuvSource(name, "fuzz", static_cast<int64_t>(file.size()), read, std::nullopt);
        YuvPlanes planes;
        if (info.known)
        {
            readYuvFrame(info, 0, read, planes, nullptr);
            if (planes.width > 0)
            {
                renderYuvImage(planes, info.format, (i & 1) != 0 ? 8 : 0);
            }
        }
        else
        {
            renderYuvImage(planes, info.format, 4);
        }
    }
}

} // namespace

int main()
{
    testStore();
    testRaw();
    testY4m();
    testRender();
    testScaledLumaBox();
    testScaledMatchesFull();
    testFuzz();
    if (g_failures != 0)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}

// NOLINTEND
