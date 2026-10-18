#include "core/image/ImageTransform.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/Encoder.h"
#include "core/image/QtConvert.h"

#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>

namespace mviewer::core
{

namespace
{

void replaceAll(std::string &s, const std::string &token, const std::string &value)
{
    if (token.empty())
        return;
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::string::npos)
    {
        s.replace(pos, token.size(), value);
        pos += value.size();
    }
}

// A rename-pattern result is used as a FILE NAME inside the destination
// directory (BatchProcessor and ExportJob join it straight onto that path), so
// any directory component it produced is dropped: "{name}/../../elsewhere"
// used to write outside the export folder. An empty or dot-only name falls back
// to the source basename.
std::string toSafeFileName(const std::string &name, const std::string &fallback)
{
    const size_t separator = name.find_last_of("/\\");
    std::string leaf = separator == std::string::npos ? name : name.substr(separator + 1);
    if (leaf.empty() || leaf == "." || leaf == "..")
        return fallback;
    return leaf;
}

} // namespace

ImageData resizeToFit(const ImageData &src, int maxW, int maxH)
{
    if (src.isNull() || src.width <= 0 || src.height <= 0)
        return ImageData();
    const int w = src.width;
    const int h = src.height;
    const int cw = maxW > 0 ? maxW : w;
    const int ch = maxH > 0 ? maxH : h;
    if (w <= cw && h <= ch)
        return src; // no upscaling
    const double sx = static_cast<double>(cw) / w;
    const double sy = static_cast<double>(ch) / h;
    const double s = std::min(sx, sy);
    if (!std::isfinite(s) || s <= 0.0)
        return src;
    const int nw = std::max(1, static_cast<int>(std::round(w * s)));
    const int nh = std::max(1, static_cast<int>(std::round(h * s)));
    constexpr int64_t kMaxPixels = 256LL * 1024 * 1024;
    if (static_cast<int64_t>(nw) * nh > kMaxPixels)
        return ImageData();
    QImage img = mvcore::toQImage(src);
    if (img.isNull())
        return ImageData();
    QImage r = img.scaled(nw, nh, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return mvcore::fromQImagePreservingAlpha(r);
}

ImageData resizeByFactor(const ImageData &src, double factor)
{
    if (src.isNull() || src.width <= 0 || src.height <= 0 || !std::isfinite(factor) ||
        factor <= 0.0)
        return ImageData();
    const double targetW = src.width * factor;
    const double targetH = src.height * factor;
    if (!std::isfinite(targetW) || !std::isfinite(targetH))
        return ImageData();
    const int nw = std::max(1, static_cast<int>(std::round(targetW)));
    const int nh = std::max(1, static_cast<int>(std::round(targetH)));
    constexpr int64_t kMaxPixels = 256LL * 1024 * 1024;
    if (static_cast<int64_t>(nw) * nh > kMaxPixels)
        return ImageData();
    QImage img = mvcore::toQImage(src);
    if (img.isNull())
        return ImageData();
    QImage r = img.scaled(nw, nh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return mvcore::fromQImagePreservingAlpha(r);
}

ImageData addTextWatermark(const ImageData &src, const std::string &text, WatermarkPosition pos,
                           double opacity01, int fontSizePx)
{
    if (src.isNull())
        return ImageData();
    if (text.empty() || !std::isfinite(opacity01) || opacity01 <= 0.0)
        return src;
    QImage img = mvcore::toQImage(src);
    if (img.isNull())
        return ImageData();

    QImage out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(std::max(8, fontSizePx));
    f.setBold(true);
    p.setFont(f);

    const QString qtext = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    const QFontMetrics fm(f);
    const QSize ts = fm.size(Qt::TextSingleLine, qtext);
    const int tw = ts.width();
    const int th = fm.height();
    const int margin = 12;
    const int alpha = static_cast<int>(std::clamp(opacity01, 0.0, 1.0) * 255);

    auto draw = [&](int x, int y)
    {
        p.setPen(QColor(0, 0, 0, alpha * 3 / 4));
        p.drawText(x + 1, y + 1, qtext);
        p.setPen(QColor(255, 255, 255, alpha));
        p.drawText(x, y, qtext);
    };

    if (pos == WatermarkPosition::Tile)
    {
        const int stepX = tw + margin * 4;
        const int stepY = th + margin * 4;
        if (stepX > 0 && stepY > 0)
        {
            for (int y = margin; y < out.height(); y += stepY)
                for (int x = margin; x < out.width(); x += stepX)
                    draw(x, y);
        }
    }
    else
    {
        int x = margin;
        int y = margin + th - fm.ascent() + fm.descent(); // baseline near top
        switch (pos)
        {
        case WatermarkPosition::TopRight:
            x = out.width() - tw - margin;
            break;
        case WatermarkPosition::BottomLeft:
            y = out.height() - margin;
            break;
        case WatermarkPosition::BottomRight:
            x = out.width() - tw - margin;
            y = out.height() - margin;
            break;
        case WatermarkPosition::Center:
            x = (out.width() - tw) / 2;
            y = out.height() / 2 + th / 4;
            break;
        case WatermarkPosition::TopLeft:
        default:
            break;
        }
        draw(x, y);
    }

    const bool hasAlpha = (src.format == PixelFormat::RGBA32 || src.format == PixelFormat::BGRA32);
    return hasAlpha ? mvcore::fromQImagePreservingAlpha(out) : mvcore::fromQImage(out);
}

ImageData makeContactSheet(const std::vector<ImageData> &imgs, int cols, int thumb)
{
    if (imgs.empty())
        return ImageData();
    cols = std::max(1, cols);
    const int safeThumb = std::max(1, thumb);
    const int n = static_cast<int>(imgs.size());
    const int rows = (n + cols - 1) / cols;
    const int pad = 6;
    const int cell = safeThumb + pad * 2;

    // The sheet is composed as one RGB32 image, so a large job (or a large
    // cols/thumb combination) would allocate without limit and fail the export
    // with a bad_alloc. Refuse a sheet above the pixel budget instead; callers
    // report the failure. The geometry is computed in 64-bit so the bound itself
    // cannot overflow.
    constexpr long long kMaxSheetPixels = 64LL * 1024 * 1024;
    const long long sheetW = 1LL * cols * cell + pad;
    const long long sheetH = 1LL * rows * cell + pad;
    if (sheetW <= 0 || sheetH <= 0 || sheetW * sheetH > kMaxSheetPixels)
        return ImageData();
    const int W = static_cast<int>(sheetW);
    const int H = static_cast<int>(sheetH);

    QImage sheet(W, H, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < n; ++i)
    {
        QImage t = mvcore::toQImage(imgs[i]);
        if (t.isNull())
            continue;
        QImage tt = t.scaled(safeThumb, safeThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int col = i % cols;
        const int row = i / cols;
        const int x = pad + col * cell + (cell - tt.width()) / 2;
        const int y = pad + row * cell + (cell - tt.height()) / 2;
        p.drawImage(x, y, tt);
    }

    return mvcore::fromQImage(sheet);
}

std::string applyRenamePattern(const std::string &pattern, const std::string &baseName,
                               const std::string &ext, int index, int total)
{
    std::string out = pattern;
    if (out.empty())
        return baseName;

    replaceAll(out, "{name}", baseName);
    replaceAll(out, "{ext}", ext);
    replaceAll(out, "{n}", std::to_string(index + 1));
    replaceAll(out, "{total}", std::to_string(total));

    // {seq:W}
    size_t pos = 0;
    while ((pos = out.find("{seq:", pos)) != std::string::npos)
    {
        const size_t close = out.find('}', pos);
        if (close == std::string::npos)
            break;
        const std::string numStr = out.substr(pos + 5, close - pos - 5);
        int width = 0;
        try
        {
            width = std::stoi(numStr);
        }
        catch (...)
        {
            width = 0;
        }
        width = std::max(1, std::min(9, width));
        const std::string seq = std::to_string(index + 1);
        std::string padded = seq;
        if (static_cast<int>(padded.size()) < width)
            padded = std::string(width - padded.size(), '0') + padded;
        out.replace(pos, close - pos + 1, padded);
        pos += padded.size();
    }

    return toSafeFileName(out, baseName);
}

namespace
{

struct PdfPage
{
    QString path;
    int w = 0;
    int h = 0;
    qint64 bytes = 0;
};

bool writeText(std::ofstream &ofs, const std::string &text)
{
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(ofs);
}

bool beginObject(std::ofstream &ofs, std::vector<long> &offsets, int number,
                 const std::string &dict)
{
    const std::streamoff pos = ofs.tellp();
    if (pos < 0 || number < 0 || static_cast<size_t>(number) >= offsets.size())
        return false;
    offsets[static_cast<size_t>(number)] = static_cast<long>(pos);
    return writeText(ofs, std::to_string(number) + " 0 obj\n" + dict);
}

bool copyJpegStream(std::ofstream &ofs, const PdfPage &page)
{
    QFile in(page.path);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    if (!writeText(ofs, "stream\n"))
        return false;
    qint64 left = page.bytes;
    char chunk[64 * 1024];
    while (left > 0)
    {
        const qint64 room = static_cast<qint64>(sizeof(chunk));
        const qint64 want = left < room ? left : room;
        const qint64 n = in.read(chunk, want);
        if (n <= 0)
            return false;
        ofs.write(chunk, static_cast<std::streamsize>(n));
        if (!ofs)
            return false;
        left -= n;
    }
    return writeText(ofs, "\nendstream\nendobj\n");
}

bool storePageJpeg(QTemporaryDir &tmp, int index, const std::vector<uint8_t> &jpg, PdfPage &page)
{
    if (jpg.empty() || jpg.data() == nullptr)
        return false;
    if (jpg.size() > static_cast<size_t>(std::numeric_limits<qint64>::max()))
        return false;
    page.path = tmp.filePath(QStringLiteral("p%1.jpg").arg(index));
    QFile out(page.path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    const auto n = static_cast<qint64>(jpg.size());
    if (out.write(reinterpret_cast<const char *>(jpg.data()), n) != n)
        return false;
    page.bytes = n;
    return true;
}

bool encodePdfPages(const std::vector<ImageData> &images, int quality, QTemporaryDir &tmp,
                    std::vector<PdfPage> &pages)
{
    int index = 0;
    for (const ImageData &im : images)
    {
        if (im.isNull())
            continue;
        const std::vector<uint8_t> jpg =
            Encoder::encodeToBuffer(im, "jpeg", Encoder::Params{quality});
        if (jpg.empty())
            continue;
        PdfPage page;
        page.w = im.width;
        page.h = im.height;
        if (!storePageJpeg(tmp, index, jpg, page))
            return false;
        pages.push_back(std::move(page));
        ++index;
    }
    return true;
}

bool writePageObjects(std::ofstream &ofs, std::vector<long> &offsets, const PdfPage &page, int k)
{
    const int pageNum = 3 + 3 * k;
    const int contentNum = 4 + 3 * k;
    const int imageNum = 5 + 3 * k;
    const std::string pageDict = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " +
                                 std::to_string(page.w) + " " + std::to_string(page.h) +
                                 "] /Resources << /XObject << /Im0 " + std::to_string(imageNum) +
                                 " 0 R >> >> /Contents " + std::to_string(contentNum) + " 0 R >>\n";
    if (!beginObject(ofs, offsets, pageNum, pageDict) || !writeText(ofs, "endobj\n"))
        return false;

    const std::string content =
        "q " + std::to_string(page.w) + " 0 0 " + std::to_string(page.h) + " 0 0 cm /Im0 Do Q\n";
    const std::string contentDict = "<< /Length " + std::to_string(content.size()) + " >>\n";
    if (!beginObject(ofs, offsets, contentNum, contentDict) || !writeText(ofs, "stream\n") ||
        !writeText(ofs, content) || !writeText(ofs, "\nendstream\nendobj\n"))
        return false;

    const std::string imgDict =
        "<< /Type /XObject /Subtype /Image /Width " + std::to_string(page.w) + " /Height " +
        std::to_string(page.h) +
        " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " +
        std::to_string(static_cast<long long>(page.bytes)) + " >>\n";
    if (!beginObject(ofs, offsets, imageNum, imgDict))
        return false;
    return copyJpegStream(ofs, page);
}

bool writeXref(std::ofstream &ofs, const std::vector<long> &offsets, int objectCount)
{
    const std::streamoff pos = ofs.tellp();
    if (pos < 0)
        return false;
    const long xrefPos = static_cast<long>(pos);
    std::string xref = "xref\n0 " + std::to_string(objectCount + 1) + "\n";
    xref += "0000000000 65535 f \n";
    for (int i = 1; i <= objectCount; ++i)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010ld 00000 n \n", offsets[static_cast<size_t>(i)]);
        xref += buf;
    }
    xref += "trailer\n<< /Size " + std::to_string(objectCount + 1) + " /Root 1 0 R >>\nstartxref\n";
    xref += std::to_string(xrefPos) + "\n%%EOF\n";
    return writeText(ofs, xref);
}

} // namespace

bool writePdf(const std::string &path, const std::vector<ImageData> &images, int quality)
{
    if (images.empty())
        return false;

    // Encode one page at a time onto scratch files so the JPEG bytes and the
    // PDF body are not all resident together. The export caller already writes
    // this path as a temporary file and renames it into place.
    QTemporaryDir scratch;
    if (!scratch.isValid())
        return false;
    std::vector<PdfPage> pages;
    if (!encodePdfPages(images, quality, scratch, pages) || pages.empty())
        return false;

    const int pageCount = static_cast<int>(pages.size());
    const int objectCount = 2 + 3 * pageCount; // catalog, pages, then 3 per image
    std::ofstream ofs(pathFromUtf8(path), std::ios::binary);
    if (!ofs)
        return false;
    if (!writeText(ofs, "%PDF-1.3\n"))
        return false;

    std::vector<long> offsets(static_cast<size_t>(objectCount) + 1, 0);
    if (!beginObject(ofs, offsets, 1, "<< /Type /Catalog /Pages 2 0 R >>\n") ||
        !writeText(ofs, "endobj\n"))
        return false;

    std::string kids = "<< /Type /Pages /Kids [";
    for (int k = 0; k < pageCount; ++k)
        kids += std::to_string(3 + 3 * k) + " 0 R ";
    kids += "] /Count " + std::to_string(pageCount) + " >>\n";
    if (!beginObject(ofs, offsets, 2, kids) || !writeText(ofs, "endobj\n"))
        return false;

    for (int k = 0; k < pageCount; ++k)
    {
        if (!writePageObjects(ofs, offsets, pages[static_cast<size_t>(k)], k))
            return false;
    }
    return writeXref(ofs, offsets, objectCount);
}

} // namespace mviewer::core
