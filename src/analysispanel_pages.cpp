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

static QString formatPixelValues(mviewer::core::ColorSpace cs, int r, int g, int b,
                                 bool hasTwoImages, bool raw16, int r16, int g16, int b16,
                                 int rawMax)
{
    const char *csLabel = mviewer::core::colorSpaceLabel(cs);
    const mviewer::core::ColorTriple px = mviewer::core::displayedColorSpace(
        clampByte(r), clampByte(g), clampByte(b), raw16, clampSample16(r16), clampSample16(g16),
        clampSample16(b16), clampSample16(rawMax), cs);
    const QString prefix = hasTwoImages ? QStringLiteral("左图 ") : QString();
    if (cs == mviewer::core::ColorSpace::HEX)
    {
        const QString hex = QString::fromStdString(mviewer::core::toHex(
            static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)));
        return QString("<span style='color:#e66;'>●</span> %1HEX: %2<br>").arg(prefix, hex);
    }
    if (cs == mviewer::core::ColorSpace::XYZ)
    {
        return QString("<span style='color:#e66;'>●</span> %1XYZ(%2, %3, %4)<br>")
            .arg(prefix)
            .arg(px.c1, 0, 'f', 3)
            .arg(px.c2, 0, 'f', 3)
            .arg(px.c3, 0, 'f', 3);
    }
    return QString("<span style='color:#e66;'>●</span> %1%2(%3, %4, %5)<br>")
        .arg(prefix)
        .arg(csLabel)
        .arg(px.c1, 0, 'f', 1)
        .arg(px.c2, 0, 'f', 1)
        .arg(px.c3, 0, 'f', 1);
}

void AnalysisPanel::updateInspectorPage()
{
    if (!m_pValid)
    {
        m_inspectorLabel->setText(tr("将鼠标悬停在图像上以检视像素。"));
        return;
    }

    const char *csLabel = mviewer::core::colorSpaceLabel(m_colorSpace);
    QString txt = QString("<h3>%1 — %2</h3>").arg(tr("像素检视"), csLabel);
    txt += QString("坐标: (%1, %2)<br>").arg(m_px).arg(m_py);
    txt += formatPixelValues(m_colorSpace, m_pR, m_pG, m_pB, m_hasB && !m_imageB.isNull(),
                             m_rawKind == 2, m_r16, m_g16, m_b16, m_rawMax);
    if (m_pA >= 0 && m_pA < 255)
        txt += QString("Alpha: %1<br>").arg(m_pA);

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
        txt += QString("8-bit 源 归一化 R=%1 G=%2 B=%3")
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
            txt += QString("<br><b>%1×%1 邻域统计</b> (亮度)<br>").arg(m_kernel);
            txt +=
                QString("均值: %1  标准差: %2<br>").arg(s.mean, 0, 'f', 1).arg(s.stdDev, 0, 'f', 1);
            txt += QString("最小值: %1  最大值: %2  方差: %3  采样数: %4")
                       .arg(s.min, 0, 'f', 0)
                       .arg(s.max, 0, 'f', 0)
                       .arg(s.variance, 0, 'f', 1)
                       .arg(s.count);
            txt += QString("<br>通道均值: R %1  G %2  B %3  HSV-V %4")
                       .arg(s.rMean, 0, 'f', 1)
                       .arg(s.gMean, 0, 'f', 1)
                       .arg(s.bMean, 0, 'f', 1)
                       .arg(s.vMean, 0, 'f', 1);
            txt += QString("<br>通道标准差: R %1  G %2  B %3")
                       .arg(s.rStdDev, 0, 'f', 1)
                       .arg(s.gStdDev, 0, 'f', 1)
                       .arg(s.bStdDev, 0, 'f', 1);
        }
    }

    if (m_hasB && !m_imageB.isNull() && m_px >= 0 && m_py >= 0 && m_px < m_imageB.width() &&
        m_py < m_imageB.height())
    {
        const QRgb c = m_imageB.pixel(m_px, m_py);
        const int rR = qRed(c), rG = qGreen(c), rB = qBlue(c);
        const int dR = m_pR - rR, dG = m_pG - rG, dB = m_pB - rB;
        const int vA = std::max({m_pR, m_pG, m_pB});
        const int vB = std::max({rR, rG, rB});
        const int dV = vA - vB;
        const double dist = qSqrt(static_cast<double>(dR * dR + dG * dG + dB * dB));
        const auto labA =
            mviewer::core::toColorSpace(static_cast<uint8_t>(m_pR), static_cast<uint8_t>(m_pG),
                                        static_cast<uint8_t>(m_pB), mviewer::core::ColorSpace::Lab);
        const auto labB =
            mviewer::core::toColorSpace(static_cast<uint8_t>(rR), static_cast<uint8_t>(rG),
                                        static_cast<uint8_t>(rB), mviewer::core::ColorSpace::Lab);
        const double dL = labB.c1 - labA.c1, da = labB.c2 - labA.c2, dbv = labB.c3 - labA.c3;
        const double dE76 = std::sqrt(dL * dL + da * da + dbv * dbv);
        txt += QString("<br><span style='color:#6e6;'>●</span> 右图 RGB(%1, %2, %3)  V %4<br>")
                   .arg(rR)
                   .arg(rG)
                   .arg(rB)
                   .arg(vB);
        txt += QString("ΔRGB   (%1, %2, %3)  ΔV %4<br>").arg(dR).arg(dG).arg(dB).arg(dV);
        txt += QString("距离: %1  色差 ΔE76: %2").arg(dist, 0, 'f', 2).arg(dE76, 0, 'f', 2);
    }
    else
    {
        txt += tr("<br><span style='color:gray;'>(加载第二张图像可对比左右差异 Δ/ΔE76)</span>");
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
        m_metaLabel->setText(tr("未选择图片"));
        return;
    }
    QString txt = QString("<h3>%1</h3>").arg(tr("元数据"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2 x %3</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "</table>")
               .arg(tr("尺寸"))
               .arg(m_imageA.width())
               .arg(m_imageA.height())
               .arg(tr("格式"))
               .arg(formatToString(m_imageA.format()))
               .arg(tr("位深"))
               .arg(m_imageA.depth());
    if (!m_imagePath.isEmpty())
        txt += QString("<br><b>%1</b> %2").arg(tr("路径：")).arg(m_imagePath);
    m_metaLabel->setText(txt);
}
