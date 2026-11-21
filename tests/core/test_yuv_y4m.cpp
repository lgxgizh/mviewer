// NOLINTBEGIN
// Unit tests for YUV4MPEG2 stream headers, frame headers, and frame indexing.

#include "core/image/yuv/Y4mHeader.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
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

static const uint8_t *bytes(const std::string &text)
{
    return reinterpret_cast<const uint8_t *>(text.data());
}

static std::optional<Y4mInfo> parse(const std::string &text, std::string *why = nullptr)
{
    return parseY4mHeader(bytes(text), text.size(), why);
}

static int64_t frameLen(const std::string &text)
{
    return parseY4mFrameHeader(bytes(text), text.size());
}

struct ChromaCase
{
    const char *tag;
    Subsampling subsampling;
    int depth;
    ChromaSiting siting;
};

static const ChromaCase kChroma[] = {
    {"420jpeg", Subsampling::S420, 8, ChromaSiting::Center},
    {"420", Subsampling::S420, 8, ChromaSiting::Center},
    {"420paldv", Subsampling::S420, 8, ChromaSiting::TopLeft},
    {"420mpeg2", Subsampling::S420, 8, ChromaSiting::Left},
    {"422", Subsampling::S422, 8, ChromaSiting::Left},
    {"444", Subsampling::S444, 8, ChromaSiting::Left},
    {"mono", Subsampling::S400, 8, ChromaSiting::Left},
    {"420p10", Subsampling::S420, 10, ChromaSiting::Left},
    {"420p12", Subsampling::S420, 12, ChromaSiting::Left},
    {"420p16", Subsampling::S420, 16, ChromaSiting::Left},
    {"422p10", Subsampling::S422, 10, ChromaSiting::Left},
    {"422p12", Subsampling::S422, 12, ChromaSiting::Left},
    {"422p16", Subsampling::S422, 16, ChromaSiting::Left},
    {"444p10", Subsampling::S444, 10, ChromaSiting::Left},
    {"444p12", Subsampling::S444, 12, ChromaSiting::Left},
    {"444p16", Subsampling::S444, 16, ChromaSiting::Left},
    {"mono10", Subsampling::S400, 10, ChromaSiting::Left},
    {"mono12", Subsampling::S400, 12, ChromaSiting::Left},
    {"mono16", Subsampling::S400, 16, ChromaSiting::Left},
};

static void expectFail(const std::string &text, const char *whyExact = nullptr)
{
    std::string why;
    const auto info = parse(text, &why);
    CHECK(!info.has_value());
    if (whyExact != nullptr)
        CHECK(why == whyExact);
    else
        CHECK(!why.empty());
}

static void testChromaTags()
{
    for (const ChromaCase &item : kChroma)
    {
        const std::string header = std::string("YUV4MPEG2  W64   H48   C") + item.tag +
                                   "   F30:1   A4:3   Ib   XCOLORRANGE=FULL   Zignored\n";
        std::string why;
        const auto info = parse(header, &why);
        CHECK(info.has_value());
        if (!info)
        {
            std::printf("  rejected C%s (%s)\n", item.tag, why.c_str());
            continue;
        }
        CHECK(info->format.width == 64 && info->format.height == 48);
        CHECK(info->format.subsampling == item.subsampling);
        CHECK(info->format.bitDepth == item.depth);
        CHECK(info->format.siting == item.siting);
        CHECK(info->format.layout == Layout::Planar);
        CHECK(info->format.order == PlaneOrder::UV);
        CHECK(info->format.packing == Packing::None);
        CHECK(info->format.endian == Endian::Little);
        CHECK(!info->format.msbAligned);
        CHECK(info->format.matrix == Matrix::BT709);
        CHECK(info->format.range == Range::Full);
        CHECK(info->colorRangeExplicit);
        CHECK(info->chromaTag == item.tag);
        CHECK(info->fpsNum == 30 && info->fpsDen == 1);
        CHECK(info->parNum == 4 && info->parDen == 3);
        CHECK(info->interlace == 'b');
        CHECK(info->headerBytes == static_cast<int64_t>(header.size()));
        CHECK(isValid(info->format));
        const auto layout = computeFrameLayout(info->format);
        CHECK(layout.has_value() && layout->frameBytes > 0);
        if (item.depth > 8)
            CHECK(bytesPerSample(info->format) == 2);
        else
            CHECK(bytesPerSample(info->format) == 1);
    }
}

static void testFieldParsing()
{
    const std::string absentText = "YUV4MPEG2 W16 H8\n";
    const auto absent = parse(absentText);
    CHECK(absent.has_value());
    if (absent)
    {
        CHECK(absent->chromaTag == "420jpeg");
        CHECK(absent->format.subsampling == Subsampling::S420);
        CHECK(absent->format.siting == ChromaSiting::Center);
        CHECK(absent->format.bitDepth == 8);
        CHECK(absent->format.range == Range::Limited);
        CHECK(!absent->colorRangeExplicit);
        CHECK(absent->interlace == '?');
        CHECK(absent->fpsNum == 0 && absent->fpsDen == 0);
        CHECK(absent->parNum == 0 && absent->parDen == 0);
        CHECK(absent->format.matrix == Matrix::BT709);
        CHECK(absent->headerBytes == static_cast<int64_t>(absentText.size()));
    }

    const auto ordered = parse("YUV4MPEG2 F24:1 Cmono H4 W6 Ip A10:11 XFOO=1\n");
    CHECK(ordered && ordered->format.width == 6 && ordered->format.height == 4);
    CHECK(ordered && ordered->format.subsampling == Subsampling::S400);
    CHECK(ordered && ordered->fpsNum == 24 && ordered->fpsDen == 1);
    CHECK(ordered && ordered->parNum == 10 && ordered->parDen == 11);
    CHECK(ordered && ordered->interlace == 'p');
    CHECK(ordered && ordered->chromaTag == "mono");
    CHECK(ordered && !ordered->colorRangeExplicit);

    const auto limited = parse("YUV4MPEG2 W2 H2 C422 XCOLORRANGE=limited\n");
    CHECK(limited && limited->format.range == Range::Limited && limited->colorRangeExplicit);
    const auto mixed = parse("YUV4MPEG2 W2 H2 C444 XCOLORRANGE=FuLl\n");
    CHECK(mixed && mixed->format.range == Range::Full && mixed->colorRangeExplicit);
    const auto lastRange = parse("YUV4MPEG2 W2 H2 Cmono XCOLORRANGE=FULL XCOLORRANGE=LIMITED\n");
    CHECK(lastRange && lastRange->format.range == Range::Limited && lastRange->colorRangeExplicit);
    const auto notRange = parse("YUV4MPEG2 W2 H2 Cmono XCOLORRANGE=FULLX\n");
    CHECK(notRange && notRange->format.range == Range::Limited && !notRange->colorRangeExplicit);

    const auto badRatio = parse("YUV4MPEG2 W8 H8 Cmono Fnope A1:2:3 Ipp F9999999999:1\n");
    CHECK(badRatio && badRatio->fpsNum == 0 && badRatio->fpsDen == 0);
    CHECK(badRatio && badRatio->parNum == 0 && badRatio->parDen == 0);
    CHECK(badRatio && badRatio->interlace == '?');

    const auto keepRatio = parse("YUV4MPEG2 W8 H8 Cmono F10:1 Fbad A0002:0003 I\n");
    CHECK(keepRatio && keepRatio->fpsNum == 10 && keepRatio->fpsDen == 1);
    CHECK(keepRatio && keepRatio->parNum == 2 && keepRatio->parDen == 3);
    CHECK(keepRatio && keepRatio->interlace == '?');

    const auto top = parse("YUV4MPEG2 W8 H8 Cmono It\n");
    const auto mixedMode = parse("YUV4MPEG2 W8 H8 Cmono Im\n");
    const auto badMode = parse("YUV4MPEG2 W8 H8 Cmono Ix\n");
    const auto longMode = parse("YUV4MPEG2 W8 H8 Cmono Ipx\n");
    CHECK(top && top->interlace == 't');
    CHECK(mixedMode && mixedMode->interlace == 'm');
    CHECK(badMode && badMode->interlace == '?');
    CHECK(longMode && longMode->interlace == '?');

    const auto maxDim = parse("YUV4MPEG2 W65536 H1 Cmono\n");
    CHECK(maxDim && maxDim->format.width == 65536 && isValid(maxDim->format));

    const int64_t kIntMax = std::numeric_limits<int>::max();
    const std::string bigF =
        "YUV4MPEG2 W2 H2 Cmono F" + std::to_string(kIntMax) + ":" + std::to_string(kIntMax) + "\n";
    const auto bigRatio = parse(bigF);
    CHECK(bigRatio && bigRatio->fpsNum == static_cast<int>(kIntMax) &&
          bigRatio->fpsDen == static_cast<int>(kIntMax));
    const auto tooWide = parse("YUV4MPEG2 W2 H2 Cmono F2147483648:1\n");
    CHECK(tooWide && tooWide->fpsNum == 0 && tooWide->fpsDen == 0);

    std::string padded = "YUV4MPEG2 W2 H2 Cmono";
    padded.append(4095 - padded.size(), ' ');
    padded.push_back('\n');
    CHECK(padded.size() == 4096);
    const auto edge = parse(padded);
    CHECK(edge && edge->headerBytes == 4096 && edge->format.width == 2);

    std::string withNul = "YUV4MPEG2 W1 H1 Cmono\n";
    withNul.push_back('\0');
    CHECK(parse(withNul) &&
          parse(withNul)->headerBytes == static_cast<int64_t>(withNul.size() - 1));
}

static void testRejects()
{
    expectFail("");
    expectFail("YUV4");
    expectFail("YUV4MPEG");
    expectFail("YUV4MPEG W1 H1\n");
    expectFail("YUV4MPEG2");
    expectFail("yuv4mpeg2 W1 H1\n");
    expectFail("YUV4MPEG3 W1 H1\n");
    expectFail("YUV4MPEG2W1 H1\n");
    expectFail("YUV4MPEG2\n");
    expectFail("YUV4MPEG2 W10\n");
    expectFail("YUV4MPEG2 H10\n");
    expectFail("YUV4MPEG2 W0 H2\n");
    expectFail("YUV4MPEG2 W-5 H2\n");
    expectFail("YUV4MPEG2 W+5 H2\n");
    expectFail("YUV4MPEG2 W99999999999 H2\n");
    expectFail("YUV4MPEG2 W65537 H2\n");
    expectFail("YUV4MPEG2 W1 H0\n");
    expectFail("YUV4MPEG2 W8 H8 W4\n");
    expectFail("YUV4MPEG2 W8 H8 H4\n");
    expectFail("YUV4MPEG2 W8 H8 C420 C422\n");
    expectFail("YUV4MPEG2 W8 H8 C420p9\n");
    expectFail("YUV4MPEG2 W8 H8 C411\n");
    expectFail("YUV4MPEG2 W8 H8 C444ALPHA\n");
    expectFail("YUV4MPEG2 W8 H8 Cmonop10\n");
    expectFail("YUV4MPEG2 W8 H8 C444alpha\n", "alpha not supported");

    std::string noNl = "YUV4MPEG2 W2 H2 C420 ";
    noNl.append(kMaxY4mHeaderBytes, 'A');
    expectFail(noNl);

    std::string lateNl(4096, ' ');
    lateNl.replace(0, 9, "YUV4MPEG2");
    lateNl[9] = ' ';
    lateNl += '\n';
    expectFail(lateNl);

    std::string embedded = "YUV4MPEG2 W2 H2 C420\n";
    embedded.insert(embedded.begin() + 12, '\0');
    expectFail(embedded);

    std::string why;
    CHECK(!parseY4mHeader(nullptr, 0, &why) && !why.empty());
    CHECK(!parseY4mHeader(nullptr, 32, &why));
    const uint8_t tiny[] = {'Y', 'U', 'V'};
    CHECK(!parseY4mHeader(tiny, sizeof(tiny), &why));
}

static void testFrameHeader()
{
    CHECK(frameLen("FRAME\n") == 6);
    CHECK(frameLen("FRAME Ixyz\n") == 11);
    CHECK(frameLen("FRAME  A B\n") == 11);
    CHECK(frameLen("FRAME\nEXTRA") == 6);
    CHECK(frameLen("FRAMX\n") == -1);
    CHECK(frameLen("FRAMEX\n") == -1);
    CHECK(frameLen("frame\n") == -1);
    CHECK(frameLen("FRAME\r\n") == -1);
    CHECK(frameLen("FRAME ") == -1);
    CHECK(frameLen("") == -1);
    CHECK(parseY4mFrameHeader(bytes("FRAME"), 5) == -1);
    CHECK(parseY4mFrameHeader(nullptr, 6) == -1);

    std::string atLimit = "FRAME";
    atLimit.append(4090, ' ');
    atLimit.push_back('\n');
    CHECK(atLimit.size() == 4096);
    CHECK(frameLen(atLimit) == 4096);

    std::string pastLimit = "FRAME";
    pastLimit.append(4091, ' ');
    pastLimit.push_back('\n');
    CHECK(frameLen(pastLimit) == -1);
}

static ReadAtFn readerFor(const std::vector<uint8_t> &buf)
{
    return [&buf](int64_t offset, uint8_t *dst, size_t n) -> size_t
    {
        if (dst == nullptr || offset < 0)
            return 0;
        const uint64_t off = static_cast<uint64_t>(offset);
        if (off >= buf.size())
            return 0;
        const size_t avail = buf.size() - static_cast<size_t>(off);
        const size_t take = n < avail ? n : avail;
        for (size_t i = 0; i < take; ++i)
            dst[i] = buf[static_cast<size_t>(off) + i];
        return take;
    };
}

static void appendStr(std::vector<uint8_t> &buf, const std::string &text)
{
    buf.insert(buf.end(), text.begin(), text.end());
}

static std::vector<int64_t> indexBuf(const Y4mInfo &info, const std::vector<uint8_t> &buf,
                                     size_t maxFrames = 1000000)
{
    return indexY4mFrames(info, static_cast<int64_t>(buf.size()), readerFor(buf), maxFrames);
}

static void testIndex()
{
    const auto info = parse("YUV4MPEG2 W4 H2 F25:1 Ip A1:1 C420jpeg\n");
    CHECK(info.has_value());
    if (!info)
        return;
    const auto layout = computeFrameLayout(info->format);
    CHECK(layout && layout->frameBytes == 12);
    const int64_t frameBytes = 12;
    const std::string frameHdr = "FRAME\n";

    std::vector<uint8_t> buf;
    appendStr(buf, "YUV4MPEG2 W4 H2 F25:1 Ip A1:1 C420jpeg\n");
    for (int i = 0; i < 3; ++i)
    {
        appendStr(buf, frameHdr);
        buf.insert(buf.end(), static_cast<size_t>(frameBytes), static_cast<uint8_t>(i + 1));
    }
    const auto three = indexBuf(*info, buf);
    CHECK(three.size() == 3);
    const int64_t fhl = static_cast<int64_t>(frameHdr.size());
    CHECK(three.size() == 3 && three[0] == info->headerBytes + fhl);
    CHECK(three.size() == 3 && three[1] == three[0] + frameBytes + fhl);
    CHECK(three.size() == 3 && three[2] == three[1] + frameBytes + fhl);

    std::vector<uint8_t> truncated = buf;
    truncated.resize(truncated.size() - 4);
    CHECK(indexBuf(*info, truncated).size() == 2);

    std::vector<uint8_t> corrupt;
    appendStr(corrupt, "YUV4MPEG2 W4 H2 F25:1 Ip A1:1 C420jpeg\n");
    for (int i = 0; i < 2; ++i)
    {
        appendStr(corrupt, frameHdr);
        corrupt.insert(corrupt.end(), static_cast<size_t>(frameBytes), static_cast<uint8_t>(9));
    }
    appendStr(corrupt, "FRAMX\n");
    corrupt.insert(corrupt.end(), static_cast<size_t>(frameBytes), static_cast<uint8_t>(9));
    CHECK(indexBuf(*info, corrupt).size() == 2);

    const std::string paramHdr = "FRAME Ixyz\n";
    std::vector<uint8_t> withParams;
    appendStr(withParams, "YUV4MPEG2 W4 H2 F25:1 Ip A1:1 C420jpeg\n");
    for (int i = 0; i < 3; ++i)
    {
        appendStr(withParams, paramHdr);
        withParams.insert(withParams.end(), static_cast<size_t>(frameBytes),
                          static_cast<uint8_t>(3));
    }
    const auto paramOffsets = indexBuf(*info, withParams);
    const int64_t paramLen = static_cast<int64_t>(paramHdr.size());
    CHECK(paramOffsets.size() == 3);
    CHECK(paramOffsets.size() == 3 && paramOffsets[0] == info->headerBytes + paramLen);
    CHECK(paramOffsets.size() == 3 && paramOffsets[1] == paramOffsets[0] + frameBytes + paramLen);

    CHECK(indexBuf(*info, buf, 2).size() == 2);
    CHECK(indexBuf(*info, buf, 0).empty());
    CHECK(indexBuf(*info, buf, 1).size() == 1);
    CHECK(indexY4mFrames(*info, 0, readerFor(buf)).empty());
    CHECK(indexY4mFrames(*info, -8, readerFor(buf)).empty());
    CHECK(indexY4mFrames(*info, info->headerBytes, readerFor(buf)).empty());

    const auto mono = parse("YUV4MPEG2 W4 H2 Cmono\n");
    CHECK(mono && computeFrameLayout(mono->format)->frameBytes == 8);
    std::vector<uint8_t> monoBuf;
    appendStr(monoBuf, "YUV4MPEG2 W4 H2 Cmono\n");
    for (int i = 0; i < 2; ++i)
    {
        appendStr(monoBuf, frameHdr);
        monoBuf.insert(monoBuf.end(), static_cast<size_t>(8), static_cast<uint8_t>(7));
    }
    const auto monoOff = indexBuf(*mono, monoBuf);
    CHECK(monoOff.size() == 2 && monoOff[0] == mono->headerBytes + fhl);
    CHECK(monoOff.size() == 2 && monoOff[1] == monoOff[0] + 8 + fhl);

    const auto p10 = parse("YUV4MPEG2 W4 H2 C420p10\n");
    CHECK(p10 && p10->format.bitDepth == 10 && !p10->format.msbAligned);
    CHECK(p10 && computeFrameLayout(p10->format)->frameBytes == 24);
    std::vector<uint8_t> p10Buf;
    appendStr(p10Buf, "YUV4MPEG2 W4 H2 C420p10\n");
    appendStr(p10Buf, frameHdr);
    p10Buf.insert(p10Buf.end(), static_cast<size_t>(24), static_cast<uint8_t>(1));
    appendStr(p10Buf, frameHdr);
    p10Buf.insert(p10Buf.end(), static_cast<size_t>(23), static_cast<uint8_t>(1));
    const auto p10Off = indexBuf(*p10, p10Buf);
    CHECK(p10Off.size() == 1 && p10Off[0] == p10->headerBytes + fhl);

    Y4mInfo overflow = *info;
    overflow.headerBytes = std::numeric_limits<int64_t>::max() - 3;
    const ReadAtFn alwaysFrame = [](int64_t, uint8_t *dst, size_t n) -> size_t
    {
        const char literal[] = "FRAME\n";
        size_t i = 0;
        for (; i < n && i < sizeof(literal) - 1; ++i)
            dst[i] = static_cast<uint8_t>(literal[i]);
        return i;
    };
    CHECK(indexY4mFrames(overflow, std::numeric_limits<int64_t>::max(), alwaysFrame).empty());
    CHECK(indexY4mFrames(*info, 1000, ReadAtFn{}).empty());
}

static Format planar(int w, int h, Subsampling sub, int depth, ChromaSiting siting, Range range)
{
    Format f;
    f.width = w;
    f.height = h;
    f.layout = Layout::Planar;
    f.subsampling = sub;
    f.order = PlaneOrder::UV;
    f.packing = Packing::None;
    f.bitDepth = depth;
    f.msbAligned = false;
    f.endian = Endian::Little;
    f.matrix = Matrix::BT709;
    f.range = range;
    f.siting = siting;
    return f;
}

static void expectRoundTrip(const Format &format, const char *tag, int fpsNum = 25, int fpsDen = 1)
{
    const std::string header = makeY4mHeader(format, fpsNum, fpsDen);
    CHECK(!header.empty());
    const auto info = parse(header);
    CHECK(info.has_value());
    if (!info)
        return;
    CHECK(info->format == format);
    CHECK(info->chromaTag == tag);
    CHECK(info->fpsNum == fpsNum && info->fpsDen == fpsDen);
    CHECK(info->parNum == 1 && info->parDen == 1);
    CHECK(info->interlace == 'p');
    CHECK(info->headerBytes == static_cast<int64_t>(header.size()));
    CHECK(info->colorRangeExplicit == (format.range == Range::Full));
    if (format.range == Range::Full)
        CHECK(header.find(" XCOLORRANGE=FULL\n") != std::string::npos);
    else
        CHECK(header.find("XCOLORRANGE") == std::string::npos);
}

static void testMakeHeader()
{
    expectRoundTrip(planar(320, 240, Subsampling::S420, 8, ChromaSiting::Center, Range::Limited),
                    "420jpeg");
    expectRoundTrip(planar(320, 240, Subsampling::S420, 8, ChromaSiting::Left, Range::Limited),
                    "420mpeg2");
    expectRoundTrip(planar(320, 240, Subsampling::S420, 8, ChromaSiting::TopLeft, Range::Limited),
                    "420paldv");
    expectRoundTrip(planar(320, 240, Subsampling::S422, 8, ChromaSiting::Left, Range::Limited),
                    "422");
    expectRoundTrip(planar(320, 240, Subsampling::S444, 8, ChromaSiting::Left, Range::Limited),
                    "444");
    expectRoundTrip(planar(320, 240, Subsampling::S400, 8, ChromaSiting::Left, Range::Limited),
                    "mono");
    expectRoundTrip(planar(320, 240, Subsampling::S420, 10, ChromaSiting::Left, Range::Limited),
                    "420p10");
    expectRoundTrip(planar(64, 64, Subsampling::S444, 16, ChromaSiting::Left, Range::Limited),
                    "444p16");
    expectRoundTrip(planar(64, 64, Subsampling::S400, 16, ChromaSiting::Left, Range::Limited),
                    "mono16");
    expectRoundTrip(planar(128, 72, Subsampling::S420, 8, ChromaSiting::Center, Range::Full),
                    "420jpeg", 50, 2);
    const std::string full =
        makeY4mHeader(planar(128, 72, Subsampling::S420, 8, ChromaSiting::Center, Range::Full));
    CHECK(full == "YUV4MPEG2 W128 H72 F25:1 Ip A1:1 C420jpeg XCOLORRANGE=FULL\n");

    Format named;
    named.width = 16;
    named.height = 16;
    CHECK(applyPixFmtName("nv12", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("yuyv422", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("uyvy422", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("yuv420p10be", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("p010le", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("yuv420p9le", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("yuv420p14le", named) && makeY4mHeader(named).empty());
    CHECK(applyPixFmtName("yv12", named) && makeY4mHeader(named).empty());
    Format invalid = planar(0, 0, Subsampling::S420, 8, ChromaSiting::Center, Range::Limited);
    CHECK(makeY4mHeader(invalid).empty());
    Format center10 = planar(32, 32, Subsampling::S420, 10, ChromaSiting::Center, Range::Limited);
    CHECK(makeY4mHeader(center10).empty());
    Format ok = planar(32, 32, Subsampling::S420, 8, ChromaSiting::Center, Range::Limited);
    CHECK(makeY4mHeader(ok, 0, 1).empty());
    CHECK(makeY4mHeader(ok, 25, -1).empty());
}

static uint32_t g_rng = 0xC0FFEEU;
static uint32_t nextRand()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void mutate(std::string &text)
{
    const uint32_t kind = nextRand() % 4;
    if ((kind == 0 || kind == 3) && !text.empty())
    {
        const uint32_t flips = 1 + nextRand() % 4;
        for (uint32_t n = 0; n < flips; ++n)
        {
            const size_t index = static_cast<size_t>(nextRand()) % text.size();
            text[index] = static_cast<char>(nextRand() & 0xFFu);
        }
    }
    if (kind == 1 || kind == 3)
        text.resize(static_cast<size_t>(nextRand()) % (text.size() + 1));
    if (kind == 2 || kind == 3)
    {
        const uint32_t count = nextRand() % 8;
        for (uint32_t n = 0; n < count; ++n)
        {
            const size_t pos =
                text.empty() ? 0 : static_cast<size_t>(nextRand()) % (text.size() + 1);
            text.insert(pos, 1, static_cast<char>(nextRand() & 0xFFu));
        }
    }
}

static void testFuzz()
{
    const std::string valid = "YUV4MPEG2 W320 H240 F25:1 Ip A1:1 C420jpeg XCOLORRANGE=FULL\n";
    int accepted = 0;
    for (int i = 0; i < 20000; ++i)
    {
        std::string text = valid;
        mutate(text);
        std::string why;
        const auto info = parse(text, &why);
        if (!info)
            continue;
        ++accepted;
        CHECK(isValid(info->format));
        CHECK(info->headerBytes >= 10);
        CHECK(static_cast<size_t>(info->headerBytes) <= text.size());
        CHECK(text[static_cast<size_t>(info->headerBytes - 1)] == '\n');
        CHECK(info->format.width >= 1 && info->format.width <= kMaxDimension);
        CHECK(info->format.height >= 1 && info->format.height <= kMaxDimension);
    }

    int frameOk = 0;
    for (int i = 0; i < 5000; ++i)
    {
        std::vector<uint8_t> buffer(static_cast<size_t>(nextRand() % 128));
        for (uint8_t &cell : buffer)
            cell = static_cast<uint8_t>(nextRand() & 0xFFu);
        const int64_t n = parseY4mFrameHeader(buffer.data(), buffer.size());
        CHECK(n == -1 || (n >= 6 && static_cast<size_t>(n) <= buffer.size()));
        if (n >= 0)
            ++frameOk;
    }
    std::vector<uint8_t> wide(6000);
    for (uint8_t &cell : wide)
        cell = static_cast<uint8_t>(nextRand() & 0xFFu);
    const int64_t wideLen = parseY4mFrameHeader(wide.data(), wide.size());
    CHECK(wideLen == -1 || (wideLen >= 6 && wideLen <= static_cast<int64_t>(kMaxY4mHeaderBytes)));
    CHECK(parseY4mHeader(nullptr, 0) == std::nullopt);
    CHECK(parseY4mFrameHeader(nullptr, 0) == -1);
    std::printf("  fuzz accepted %d headers, %d frame headers\n", accepted, frameOk);
}

int main()
{
    testChromaTags();
    testFieldParsing();
    testRejects();
    testFrameHeader();
    testIndex();
    testMakeHeader();
    testFuzz();
    if (g_failures == 0)
        std::printf("test_yuv_y4m: all passed\n");
    else
        std::printf("test_yuv_y4m: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
// NOLINTEND
