#include "core/image/ImageTransform.h"

#include "core/image/Encoder.h"
#include "core/image/QtConvert.h"

#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>
#include <algorithm>
#include <cstdio>
#include <fstream>

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

} // namespace

ImageData resizeToFit(const ImageData &src, int maxW, int maxH)
{
    if (src.isNull())
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
    const int nw = std::max(1, static_cast<int>(std::round(w * s)));
    const int nh = std::max(1, static_cast<int>(std::round(h * s)));
    QImage img = mvcore::toQImage(src);
    if (img.isNull())
        return ImageData();
    QImage r = img.scaled(nw, nh, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return mvcore::fromQImage(r);
}

ImageData resizeByFactor(const ImageData &src, double factor)
{
    if (src.isNull() || factor <= 0.0)
        return ImageData();
    const int nw = std::max(1, static_cast<int>(std::round(src.width * factor)));
    const int nh = std::max(1, static_cast<int>(std::round(src.height * factor)));
    QImage img = mvcore::toQImage(src);
    if (img.isNull())
        return ImageData();
    QImage r = img.scaled(nw, nh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return mvcore::fromQImage(r);
}

ImageData addTextWatermark(const ImageData &src, const std::string &text, WatermarkPosition pos,
                           double opacity01, int fontSizePx)
{
    if (src.isNull())
        return ImageData();
    if (text.empty())
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

    const QString qtext = QString::fromStdString(text);
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

    return mvcore::fromQImage(out);
}

ImageData makeContactSheet(const std::vector<ImageData> &imgs, int cols, int thumb)
{
    if (imgs.empty())
        return ImageData();
    cols = std::max(1, cols);
    const int n = static_cast<int>(imgs.size());
    const int rows = (n + cols - 1) / cols;
    const int pad = 6;
    const int cell = thumb + pad * 2;
    const int W = cols * cell + pad;
    const int H = rows * cell + pad;

    QImage sheet(W, H, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < n; ++i)
    {
        QImage t = mvcore::toQImage(imgs[i]);
        if (t.isNull())
            continue;
        QImage tt = t.scaled(thumb, thumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

    return out;
}

bool writePdf(const std::string &path, const std::vector<ImageData> &images, int quality)
{
    if (images.empty())
        return false;

    struct Page
    {
        std::vector<uint8_t> jpg;
        int w = 0;
        int h = 0;
    };
    std::vector<Page> pages;
    for (const auto &im : images)
    {
        if (im.isNull())
            continue;
        std::vector<uint8_t> buf = Encoder::encodeToBuffer(im, "jpeg", Encoder::Params{quality});
        if (buf.empty())
            continue;
        Page pg;
        pg.jpg = std::move(buf);
        pg.w = im.width;
        pg.h = im.height;
        pages.push_back(std::move(pg));
    }
    if (pages.empty())
        return false;

    const int K = static_cast<int>(pages.size());
    const int M = 2 + 3 * K; // objects: 1 catalog, 2 pages, then 3 per image

    std::string pdf = "%PDF-1.3\n";
    std::vector<long> off(M + 1, 0); // 1-based object offsets

    auto writeObj = [&](int num, const std::string &dict, const std::string &streamData)
    {
        off[num] = static_cast<long>(pdf.size());
        pdf += std::to_string(num);
        pdf += " 0 obj\n";
        pdf += dict;
        if (!streamData.empty())
        {
            pdf += "stream\n";
            pdf.append(reinterpret_cast<const char *>(streamData.data()),
                       static_cast<std::streamsize>(streamData.size()));
            pdf += "\nendstream\n";
        }
        pdf += "endobj\n";
    };

    // 1: Catalog
    writeObj(1, "<< /Type /Catalog /Pages 2 0 R >>\n", "");

    // 2: Pages
    std::string kids = "<< /Type /Pages /Kids [";
    for (int k = 0; k < K; ++k)
        kids += std::to_string(3 + 3 * k) + " 0 R ";
    kids += "] /Count " + std::to_string(K) + " >>\n";
    writeObj(2, kids, "");

    // Per image: page, content, image xobject
    for (int k = 0; k < K; ++k)
    {
        const Page &pg = pages[k];
        const int pageNum = 3 + 3 * k;
        const int contentNum = 4 + 3 * k;
        const int imageNum = 5 + 3 * k;

        const std::string pageDict =
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + std::to_string(pg.w) + " " +
            std::to_string(pg.h) + "] /Resources << /XObject << /Im0 " + std::to_string(imageNum) +
            " 0 R >> >> /Contents " + std::to_string(contentNum) + " 0 R >>\n";
        writeObj(pageNum, pageDict, "");

        const std::string content =
            "q " + std::to_string(pg.w) + " 0 0 " + std::to_string(pg.h) + " 0 0 cm /Im0 Do Q\n";
        const std::string contentDict = "<< /Length " + std::to_string(content.size()) + " >>\n";
        writeObj(contentNum, contentDict, content);

        const std::string imgDict =
            "<< /Type /XObject /Subtype /Image /Width " + std::to_string(pg.w) + " /Height " +
            std::to_string(pg.h) +
            " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " +
            std::to_string(pg.jpg.size()) + " >>\n";
        writeObj(imageNum, imgDict, std::string(pg.jpg.begin(), pg.jpg.end()));
    }

    const long xrefPos = static_cast<long>(pdf.size());
    std::string xref = "xref\n0 " + std::to_string(M) + "\n";
    xref += "0000000000 65535 f \n";
    for (int i = 1; i < M; ++i)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010ld 00000 n \n", off[i]);
        xref += buf;
    }
    pdf += xref;
    pdf += "trailer\n<< /Size " + std::to_string(M) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xrefPos) + "\n%%EOF\n";

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
        return false;
    ofs.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
    return static_cast<bool>(ofs);
}

} // namespace mviewer::core
