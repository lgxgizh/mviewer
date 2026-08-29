#include "core/image/FramePlaybackController.h"
#include "core/image/ImageRepository.h"
#include "core/image/FrameSequence.h"
#include "core/compare/CompareEngine.h"
#include "core/workspace/WorkspaceSerializer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryFile>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

// M57 reader contract tests use the deployed Qt image plugins and sequential fallback.
namespace
{

using mviewer::core::FrameSequenceKind;

void put16(QByteArray &data, int offset, uint16_t value)
{
    data[offset] = static_cast<char>(value & 0xff);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void put32(QByteArray &data, int offset, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = static_cast<char>((value >> (i * 8)) & 0xff);
}

void appendChunk(QByteArray &body, const char tag[5], const QByteArray &payload)
{
    body.append(tag, 4);
    const uint32_t size = static_cast<uint32_t>(payload.size());
    body.append(static_cast<char>(size & 0xff));
    body.append(static_cast<char>((size >> 8) & 0xff));
    body.append(static_cast<char>((size >> 16) & 0xff));
    body.append(static_cast<char>((size >> 24) & 0xff));
    body.append(payload);
    if (size & 1)
        body.append('\0');
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray makeAnimatedGif()
{
    // A tiny deterministic three-frame GIF.  The per-frame local palettes
    // deliberately exercise palette changes as well as frame timing in qgif.
    QByteArray gif("GIF89a", 6);
    gif.append("\x04\x00\x04\x00\x80\x00\x00", 7);
    const char globalPalette[] = {char(0xff), 0, 0, 0, 0, 0};
    gif.append(globalPalette, sizeof(globalPalette));
    gif.append("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 19);

    const char colors[][3] = {{char(0xff), 0, 0}, {0, char(0xff), 0},
                              {0, 0, char(0xff)}};
    for (int frame = 0; frame < 3; ++frame)
    {
        gif.append("\x21\xf9\x04\x00", 4);
        gif.append(static_cast<char>((frame + 1) * 10));
        gif.append("\x00\x00\x00", 3);
        gif.append('\x2c');
        gif.append("\x00\x00\x00\x00\x04\x00\x04\x00", 8);
        gif.append(frame == 0 ? '\x00' : '\x80');
        if (frame != 0)
        {
            gif.append(colors[frame], 3);
            gif.append("\x00\x00\x00", 3);
        }
        // LZW minimum code size 8, followed by a 16-pixel zero-index scanline.
        gif.append("\x08\x09\x00\x01\x08\x1c\x48\xb0\x20\x80\x80\x00", 12);
    }
    gif.append('\x3b');
    return gif;
}

QByteArray makeAnimatedWebp()
{
    // These are tiny lossless VP8L payloads produced once with libwebp.  Keeping
    // the payloads in the fixture makes the test independent of writer support
    // in the Qt build while still exercising the real qwebp reader/plugin.
    const std::vector<QByteArray> images{
        QByteArray::fromHex("5650384c0f0000002f03c000000710fd8ffe0722a2ff0100"),
        QByteArray::fromHex("5650384c0f0000002f03c0000007d0ff88fe0722a2ff0100"),
        QByteArray::fromHex("5650384c0f0000002f03c000000710d1fffe0722a2ff0100")};
    QByteArray body("WEBP", 4);
    QByteArray vp8x(10, '\0');
    vp8x[0] = '\x02'; // animation feature flag
    vp8x[4] = '\x03';
    vp8x[7] = '\x03';
    appendChunk(body, "VP8X", vp8x);
    appendChunk(body, "ANIM", QByteArray(6, '\0'));
    for (size_t i = 0; i < images.size(); ++i)
    {
        QByteArray frame(16, '\0');
        const uint32_t duration = static_cast<uint32_t>((i + 1) * 100);
        frame[6] = '\x03'; // width - 1, 4 pixels
        frame[9] = '\x03'; // height - 1, 4 pixels
        frame[12] = static_cast<char>(duration & 0xff);
        frame[13] = static_cast<char>((duration >> 8) & 0xff);
        frame[14] = static_cast<char>((duration >> 16) & 0xff);
        frame[15] = '\x02';
        frame.append(images[i]);
        appendChunk(body, "ANMF", frame);
    }
    QByteArray result("RIFF", 4);
    const uint32_t riffSize = static_cast<uint32_t>(body.size());
    result.append(static_cast<char>(riffSize & 0xff));
    result.append(static_cast<char>((riffSize >> 8) & 0xff));
    result.append(static_cast<char>((riffSize >> 16) & 0xff));
    result.append(static_cast<char>((riffSize >> 24) & 0xff));
    result.append(body);
    return result;
}

QByteArray makeMultipageTiff()
{
    constexpr int pageCount = 3;
    constexpr int ifdStart = 8;
    constexpr int ifdSize = 2 + 9 * 12 + 4;
    constexpr int bitsStart = ifdStart + pageCount * ifdSize;
    constexpr int pixelsStart = bitsStart + pageCount * 6;
    QByteArray tiff(pixelsStart + pageCount * 48, '\0');
    tiff[0] = 'I';
    tiff[1] = 'I';
    put16(tiff, 2, 42);
    put32(tiff, 4, ifdStart);
    for (int page = 0; page < pageCount; ++page)
    {
        const int ifd = ifdStart + page * ifdSize;
        put16(tiff, ifd, 9);
        const int entries = ifd + 2;
        auto entry = [&](int n, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
        {
            const int at = entries + n * 12;
            put16(tiff, at, tag);
            put16(tiff, at + 2, type);
            put32(tiff, at + 4, count);
            put32(tiff, at + 8, value);
        };
        entry(0, 256, 3, 1, 4);
        entry(1, 257, 3, 1, 4);
        entry(2, 258, 3, 3, bitsStart + page * 6);
        entry(3, 259, 3, 1, 1);
        entry(4, 262, 3, 1, 2);
        entry(5, 273, 4, 1, pixelsStart + page * 48);
        entry(6, 277, 3, 1, 3);
        entry(7, 278, 4, 1, 4);
        entry(8, 279, 4, 1, 48);
        put32(tiff, entries + 9 * 12, page + 1 < pageCount ? ifd + ifdSize : 0);
        for (int channel = 0; channel < 3; ++channel)
            put16(tiff, bitsStart + page * 6 + channel * 2, 8);
        for (int pixel = 0; pixel < 16; ++pixel)
        {
            const int at = pixelsStart + page * 48 + pixel * 3;
            tiff[at + 0] = page == 0 ? char(0xff) : 0;
            tiff[at + 1] = page == 1 ? char(0xff) : 0;
            tiff[at + 2] = page == 2 ? char(0xff) : 0;
        }
    }
    return tiff;
}

bool check(bool condition, const std::string &message)
{
    if (condition)
        return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

void printBaseline(const QString &label, const QString &path)
{
    QImageReader reader(path);
    std::cout << "baseline " << label.toStdString() << " format="
              << reader.format().toStdString() << " imageCount=" << reader.imageCount()
              << " supportsAnimation=" << (reader.supportsAnimation() ? 1 : 0)
              << " loopCount=" << reader.loopCount() << '\n';
    for (int i = 0; i < std::max(1, reader.imageCount()); ++i)
    {
        const bool jumped = reader.jumpToImage(i);
        const QImage image = jumped ? reader.read() : QImage();
        std::cout << "baseline " << label.toStdString() << " frame=" << i
                  << " jump=" << (jumped ? 1 : 0) << " read=" << (!image.isNull() ? 1 : 0)
                  << " delay=" << reader.nextImageDelay() << '\n';
    }
}

bool checkColor(const ImageData &data, int r, int g, int b, const std::string &label)
{
    const auto px = samplePixel(data, 0, 0);
    return check(px.valid && px.r == r && px.g == g && px.b == b, label);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString root = QDir::tempPath() + "/mviewer_m57_multiframe_" +
                         QString::number(QCoreApplication::applicationPid());
    QDir(root).removeRecursively();
    if (!QDir().mkpath(root))
        return 1;

    const QString gif = root + "/animated.gif";
    const QString webp = root + "/animated.webp";
    const QString tiff = root + "/pages.tiff";
    const QString png = root + "/static.png";
    bool ok = writeBytes(gif, makeAnimatedGif());
    QImage staticImage(QSize(4, 4), QImage::Format_RGB32);
    staticImage.fill(Qt::red);
    ok = ok && staticImage.save(png);
    ok = ok && writeBytes(webp, makeAnimatedWebp());
    ok = ok && writeBytes(tiff, makeMultipageTiff());
    if (!check(ok, "deterministic fixtures were written"))
        return 1;

    printBaseline("gif", gif);
    printBaseline("webp", webp);
    printBaseline("tiff", tiff);

    const auto gifInfo = mviewer::core::FrameSequenceReader::probe(gif.toUtf8().toStdString());
    const auto webpInfo = mviewer::core::FrameSequenceReader::probe(webp.toUtf8().toStdString());
    const auto tiffInfo = mviewer::core::FrameSequenceReader::probe(tiff.toUtf8().toStdString());
    ok = check(gifInfo.valid && gifInfo.kind == FrameSequenceKind::Animation &&
                   gifInfo.frameCount == 3 && gifInfo.durationKnown,
               "GIF is detected as a 3-frame animation") &&
         ok;
    ok = check(webpInfo.valid && webpInfo.kind == FrameSequenceKind::Animation &&
                   webpInfo.frameCount == 3,
               "WebP is detected as a 3-frame animation") &&
         ok;
    ok = check(tiffInfo.valid && tiffInfo.kind == FrameSequenceKind::Pages &&
                   tiffInfo.frameCount == 3 && !tiffInfo.animated,
               "TIFF is detected as three navigable pages") &&
         ok;

    const std::vector<std::tuple<QString, int, int, int, int>> expected{
        {gif, 0, 255, 0, 0}, {gif, 1, 0, 255, 0}, {gif, 2, 0, 0, 255},
        {webp, 0, 255, 0, 0}, {webp, 1, 0, 255, 0}, {webp, 2, 0, 0, 255},
        {tiff, 0, 255, 0, 0}, {tiff, 1, 0, 255, 0}, {tiff, 2, 0, 0, 255}};
    for (const auto &[path, index, r, g, b] : expected)
    {
        const auto decoded = mviewer::core::FrameSequenceReader::decodeFull(
            path.toUtf8().toStdString(), index);
        ok = check(decoded.ok && checkColor(decoded.pixels, r, g, b,
                                             path.toStdString() + " frame " +
                                                 std::to_string(index)),
                   "explicit frame/page decode succeeds") &&
             ok;
    }

    auto &repo = ImageRepository::instance();
    const auto frame0 = repo.loadFrame(gif.toUtf8().toStdString(), 0);
    const auto frame1 = repo.loadFrame(gif.toUtf8().toStdString(), 1);
    const auto frame1Again = repo.loadFrame(gif.toUtf8().toStdString(), 1);
    ok = check(frame0.success() && frame1.success() && frame1Again.success() &&
                   frame0.frame->frameIdentity() != frame1.frame->frameIdentity() &&
                   frame0.frame->frameIndex() == 0 && frame1.frame->frameIndex() == 1 &&
                   frame1Again.fromCache,
               "repository cache identity includes frame index and revision") &&
         ok;

    CompareEngine compare;
    compare.setImages({gif.toUtf8().toStdString(), tiff.toUtf8().toStdString()}, {2, 1});
    const auto compareSession = compare.session();
    ok = check(compare.imageCount() == 2 && compare.imageAt(0) && compare.imageAt(1) &&
                   compare.imageAt(0)->frameIndex() == 2 && compare.imageAt(1)->frameIndex() == 1 &&
                   compareSession.frameIndices == std::vector<int>({2, 1}),
               "Compare owns the explicitly selected frame/page per pane") &&
         ok;
    const auto compareRestored = mviewer::core::deserializeCompareSession(
        mviewer::core::serializeCompareSession(compareSession));
    ok = check(compareRestored && compareRestored->frameIndices == std::vector<int>({2, 1}),
               "Compare session round-trip preserves frame/page identity") &&
         ok;

    mviewer::core::FramePlaybackController timeline;
    mviewer::core::FrameSequenceInfo sequence;
    sequence.valid = true;
    sequence.animated = true;
    sequence.kind = FrameSequenceKind::Animation;
    sequence.frameCount = 3;
    sequence.loopCount = -1;
    timeline.configure(sequence, {{0, 100, 4, 4}, {1, 200, 4, 4}, {2, 300, 4, 4}});
    const auto epoch = mviewer::core::FramePlaybackController::TimePoint{};
    timeline.start(epoch);
    ok = check(!timeline.tick(epoch + std::chrono::milliseconds(99)).due &&
                   timeline.tick(epoch + std::chrono::milliseconds(100)).frameIndex == 1 &&
                   timeline.tick(epoch + std::chrono::milliseconds(300)).frameIndex == 2 &&
                   timeline.tick(epoch + std::chrono::milliseconds(600)).frameIndex == 0,
               "playback follows per-frame delays without cumulative drift") &&
         ok;
    sequence.frameCount = 10000;
    sequence.durationKnown = true;
    sequence.totalDurationMs = 500000;
    timeline.configure(sequence);
    timeline.start(epoch);
    const auto catchup = timeline.tick(epoch + std::chrono::milliseconds(5001));
    ok = check(catchup.frameIndex == 100 && catchup.skipped,
               "long decode stalls catch up to the monotonic timeline") &&
         ok;

    mviewer::domain::Workspace workspace;
    workspace.rootPath = "C:/fixtures";
    workspace.currentImagePath = gif.toUtf8().toStdString();
    workspace.currentFrameIndex = 2;
    workspace.currentPlaying = true;
    const auto restored = mviewer::core::deserializeWorkspace(
        mviewer::core::serializeWorkspace(workspace));
    ok = check(restored && restored->currentImagePath == workspace.currentImagePath &&
                   restored->currentFrameIndex == 2 && restored->currentPlaying,
               "workspace persists current frame and playing state") &&
         ok;

    const std::string legacy =
        "{\"version\":3,\"root\":\"C:/fixtures\",\"folders\":[],\"comparedImages\":[],"
        "\"compareSession\":\"\"}";
    const auto old = mviewer::core::deserializeWorkspace(legacy);
    ok = check(old && old->currentFrameIndex == 0 && !old->currentPlaying,
               "legacy workspace defaults to frame zero and paused") &&
         ok;

    QDir(root).removeRecursively();
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(ok ? 0 : 1);
}
