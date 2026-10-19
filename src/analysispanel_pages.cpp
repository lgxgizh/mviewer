#include "analysispanel.h"
#include "analyzermodel.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/compare/Aligner.h"
#include "widgets/rawimageview.h"
#include <QSettings>

#include "core/image/QtConvert.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

static uint8_t clampByte(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return static_cast<uint8_t>(value);
}

static uint16_t clampSample16(int value)
{
    if (value < 0)
        return 0;
    if (value > 65535)
        return 65535;
    return static_cast<uint16_t>(value);
}

static QString formatPixelValues(mviewer::core::ColorSpace cs, int r, int g, int b, bool raw16,
                                 int r16, int g16, int b16, int rawMax)
{
    const char *csLabel = mviewer::core::colorSpaceLabel(cs);
    const mviewer::core::ColorTriple px = mviewer::core::displayedColorSpace(
        clampByte(r), clampByte(g), clampByte(b), raw16, clampSample16(r16), clampSample16(g16),
        clampSample16(b16), clampSample16(rawMax), cs);
    if (cs == mviewer::core::ColorSpace::HEX)
    {
        const QString hex = QString::fromStdString(mviewer::core::toHex(
            static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)));
        return QString("<span style='color:#e66;'>●</span> HEX: %1<br>").arg(hex);
    }
    if (cs == mviewer::core::ColorSpace::XYZ)
    {
        return QString("<span style='color:#e66;'>●</span> XYZ(%1, %2, %3)<br>")
            .arg(px.c1, 0, 'f', 3)
            .arg(px.c2, 0, 'f', 3)
            .arg(px.c3, 0, 'f', 3);
    }
    return QString("<span style='color:#e66;'>●</span> %1(%2, %3, %4)<br>")
        .arg(csLabel)
        .arg(px.c1, 0, 'f', 1)
        .arg(px.c2, 0, 'f', 1)
        .arg(px.c3, 0, 'f', 1);
}

void AnalysisPanel::updateInspectorPage()
{
    if (!m_pValid)
    {
        m_inspectorLabel->setText(tr("Hover over the image to inspect pixels."));
        return;
    }

    const char *csLabel = mviewer::core::colorSpaceLabel(m_colorSpace);
    QString txt = QString("<h3>%1 — %2</h3>").arg(tr("Pixel Inspector"), csLabel);
    txt += tr("Coordinates: (%1, %2)<br>").arg(m_px).arg(m_py);
    txt += formatPixelValues(m_colorSpace, m_pR, m_pG, m_pB, m_rawKind == 2, m_r16, m_g16, m_b16,
                             m_rawMax);
    if (m_pA >= 0 && m_pA < 255)
        txt += QString("Alpha: %1<br>").arg(m_pA);

    // P0-2/PixelInspector: original high-bit-depth readout.
    txt += tr("<br><b>Raw sample</b> ");
    if (m_rawKind == 2)
    {
        const double n = m_rawMax > 0 ? static_cast<double>(m_rawMax) : 65535.0;
        txt += QString("16-bit R=%1 G=%2 B=%3 (0..%4)<br>")
                   .arg(m_r16)
                   .arg(m_g16)
                   .arg(m_b16)
                   .arg(m_rawMax);
        txt += tr("Normalized R=%1 G=%2 B=%3")
                   .arg(m_r16 / n, 0, 'f', 4)
                   .arg(m_g16 / n, 0, 'f', 4)
                   .arg(m_b16 / n, 0, 'f', 4);
    }
    else if (m_rawKind == 1)
    {
        txt += tr("RAW preview (demosaic 8-bit), no linear 16-bit sample");
    }
    else
    {
        txt += tr("8-bit source normalized R=%1 G=%2 B=%3")
                   .arg(m_pR / 255.0, 0, 'f', 4)
                   .arg(m_pG / 255.0, 0, 'f', 4)
                   .arg(m_pB / 255.0, 0, 'f', 4);
    }

    // NxN neighborhood luminance statistics over the left image (real pixels,
    // read from m_imageA which is Format_RGB32). Clipped to image bounds.
    if (m_hasA && !m_imageA.isNull())
    {
        const int w = m_imageA.width(), h = m_imageA.height();
        if (m_px >= 0 && m_py >= 0 && m_px < w && m_py < h)
        {
            const uchar *data = m_imageA.constBits();
            const int stride = m_imageA.bytesPerLine();
            // m_imageA is Format_RGB32: 4 bytes per pixel in B,G,R,A order.
            // Passing it as RGB24 read the wrong pixels (and the wrong channel
            // order), so the layout is stated explicitly.
            const mviewer::core::NeighborhoodStats s =
                mviewer::core::neighborhoodStats(data, stride, w, h, m_px, m_py, m_kernel, 4);
            txt += tr("<br><b>%1×%1 neighborhood statistics</b> (luminance)<br>").arg(m_kernel);
            txt += tr("Mean: %1  Std dev: %2<br>").arg(s.mean, 0, 'f', 1).arg(s.stdDev, 0, 'f', 1);
            txt += tr("Min: %1  Max: %2  Variance: %3  Samples: %4")
                       .arg(s.min, 0, 'f', 0)
                       .arg(s.max, 0, 'f', 0)
                       .arg(s.variance, 0, 'f', 1)
                       .arg(s.count);
            txt += tr("<br>Channel means: R %1  G %2  B %3  HSV-V %4")
                       .arg(s.rMean, 0, 'f', 1)
                       .arg(s.gMean, 0, 'f', 1)
                       .arg(s.bMean, 0, 'f', 1)
                       .arg(s.vMean, 0, 'f', 1);
            txt += tr("<br>Channel std dev: R %1  G %2  B %3")
                       .arg(s.rStdDev, 0, 'f', 1)
                       .arg(s.gStdDev, 0, 'f', 1)
                       .arg(s.bStdDev, 0, 'f', 1);
        }
    }

    m_inspectorLabel->setText(txt);
}

static QString formatToString(QImage::Format f)
{
    switch (f)
    {
    case QImage::Format_RGB32:
        return "RGB32";
    case QImage::Format_ARGB32:
        return "ARGB32";
    case QImage::Format_ARGB32_Premultiplied:
        return "ARGB32 PM";
    case QImage::Format_RGB888:
        return "RGB888";
    case QImage::Format_RGBA8888:
        return "RGBA8888";
    case QImage::Format_Grayscale8:
        return "Gray8";
    default:
        return QString("Format_%1").arg(static_cast<int>(f));
    }
}

void AnalysisPanel::updateMetadataPage()
{
    if (!m_hasA)
    {
        m_metaLabel->setText(tr("No Image Selected"));
        return;
    }
    QString txt = QString("<h3>%1</h3>").arg(tr("Metadata"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2 x %3</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "</table>")
               .arg(tr("Dimensions"))
               .arg(m_imageA.width())
               .arg(m_imageA.height())
               .arg(tr("Format"))
               .arg(formatToString(m_imageA.format()))
               .arg(tr("Bit Depth"))
               .arg(m_imageA.depth());
    if (!m_imagePath.isEmpty())
        txt += QString("<br><b>%1</b> %2").arg(tr("Path:")).arg(m_imagePath);
    m_metaLabel->setText(txt);
}
