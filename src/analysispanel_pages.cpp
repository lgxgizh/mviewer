#include "analysispanel.h"
#include "analyzermodel.h"
#include "core/analysis/AnalysisEngine.h"
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

void AnalysisPanel::updateInspectorPage()
{
    if (!m_pValid)
    {
        m_inspectorLabel->setText(tr("Move the mouse over an image to inspect pixels."));
        return;
    }

    const char *csLabel = mviewer::core::colorSpaceLabel(m_colorSpace);
    const mviewer::core::ColorTriple px =
        mviewer::core::toColorSpace(static_cast<uint8_t>(m_pR), static_cast<uint8_t>(m_pG),
                                    static_cast<uint8_t>(m_pB), m_colorSpace);

    QString txt = QString("<h3>Pixel Inspector — %1</h3>").arg(csLabel);
    txt += QString("pos: (%1, %2)<br>").arg(m_px).arg(m_py);
    if (m_colorSpace == mviewer::core::ColorSpace::HEX)
    {
        const QString hex = QString::fromStdString(mviewer::core::toHex(
            static_cast<uint8_t>(m_pR), static_cast<uint8_t>(m_pG), static_cast<uint8_t>(m_pB)));
        txt += QString("<span style='color:#e66;'>●</span> Left HEX %1<br>").arg(hex);
    }
    else if (m_colorSpace == mviewer::core::ColorSpace::XYZ)
    {
        txt += QString("<span style='color:#e66;'>●</span> Left XYZ(%1, %2, %3)<br>")
                   .arg(px.c1, 0, 'f', 3)
                   .arg(px.c2, 0, 'f', 3)
                   .arg(px.c3, 0, 'f', 3);
    }
    else
    {
        txt += QString("<span style='color:#e66;'>●</span> Left %1(%2, %3, %4)<br>")
                   .arg(csLabel)
                   .arg(px.c1, 0, 'f', 1)
                   .arg(px.c2, 0, 'f', 1)
                   .arg(px.c3, 0, 'f', 1);
    }

    // P0-2/PixelInspector: original high-bit-depth readout.
    txt += QString("<br><b>原始采样</b> ");
    if (m_rawKind == 2)
    {
        const double n = m_rawMax > 0 ? static_cast<double>(m_rawMax) : 65535.0;
        txt += QString("16-bit R=%1 G=%2 B=%3 (0..%4)<br>")
                   .arg(m_r16)
                   .arg(m_g16)
                   .arg(m_b16)
                   .arg(m_rawMax);
        txt += QString("归一化 R=%1 G=%2 B=%3")
                   .arg(m_r16 / n, 0, 'f', 4)
                   .arg(m_g16 / n, 0, 'f', 4)
                   .arg(m_b16 / n, 0, 'f', 4);
    }
    else if (m_rawKind == 1)
    {
        txt += QString("RAW 预览 (demosaic 8-bit)，无线性 16-bit 采样");
    }
    else
    {
        txt += QString("8-bit 源 (无高位深)");
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
            const mviewer::core::NeighborhoodStats s =
                mviewer::core::neighborhoodStats(data, stride, w, h, m_px, m_py, m_kernel);
            txt += QString("<br><b>%1×%1 Kernel</b> (lum)<br>").arg(m_kernel);
            txt += QString("mean:%1  std:%2<br>").arg(s.mean, 0, 'f', 1).arg(s.stdDev, 0, 'f', 1);
            txt += QString("min:%1  max:%2  var:%3  n:%4")
                       .arg(s.min, 0, 'f', 0)
                       .arg(s.max, 0, 'f', 0)
                       .arg(s.variance, 0, 'f', 1)
                       .arg(s.count);
            txt += QString("<br>ROI 通道均值: R %1  G %2  B %3")
                       .arg(s.rMean, 0, 'f', 1)
                       .arg(s.gMean, 0, 'f', 1)
                       .arg(s.bMean, 0, 'f', 1);
        }
    }

    if (m_hasB && !m_imageB.isNull() && m_px >= 0 && m_py >= 0 && m_px < m_imageB.width() &&
        m_py < m_imageB.height())
    {
        const QRgb c = m_imageB.pixel(m_px, m_py);
        const int rR = qRed(c), rG = qGreen(c), rB = qBlue(c);
        const int dR = m_pR - rR, dG = m_pG - rG, dB = m_pB - rB;
        const double dist = qSqrt(static_cast<double>(dR * dR + dG * dG + dB * dB));
        txt += QString("<br><span style='color:#6e6;'>●</span> Right RGB(%1, %2, %3)<br>")
                   .arg(rR)
                   .arg(rG)
                   .arg(rB);
        txt += QString("Δ      (%1, %2, %3)<br>").arg(dR).arg(dG).arg(dB);
        txt += QString("dist: %1").arg(dist, 0, 'f', 2);
    }
    else
    {
        txt += tr("<br>(load a second image to compare Left/Right/Δ)");
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
        m_metaLabel->setText(tr("No image selected"));
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
               .arg(tr("Depth"))
               .arg(m_imageA.depth());
    if (!m_imagePath.isEmpty())
        txt += QString("<br><b>%1</b> %2").arg(tr("Path:")).arg(m_imagePath);
    m_metaLabel->setText(txt);
}
