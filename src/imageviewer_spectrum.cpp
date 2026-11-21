// FFT spectrum panel and CV box overlay (report feature 10).
#include "imageviewer.h"

#include "core/analysis/Fft2D.h"
#include "core/image/QtConvert.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr int kSpectrumMaxEdge = 256;

/// Luma samples, subsampled so the transform stays cheap on large frames.
std::vector<float> lumaSamples(const ImageData &pixels, int width, int height)
{
    const int stepX = std::max(1, width / kSpectrumMaxEdge);
    const int stepY = std::max(1, height / kSpectrumMaxEdge);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(width / stepX + 1) * static_cast<size_t>(height / stepY + 1));
    for (int y = 0; y < height; y += stepY)
    {
        for (int x = 0; x < width; x += stepX)
        {
            const PixelRGBA px = samplePixel(pixels, x, y);
            out.push_back(px.valid ? static_cast<float>(luminance(px.r, px.g, px.b)) : 0.0f);
        }
    }
    return out;
}

/// Centered, log-scaled magnitude as an 8-bit image.
QImage spectrumImage(const mviewer::core::Fft2DResult &fft)
{
    if (fft.magnitude.empty())
        return QImage();
    const int w = fft.width;
    const int h = fft.height;
    std::vector<float> logMag(fft.magnitude.size());
    float peak = 0.0f;
    for (size_t i = 0; i < fft.magnitude.size(); ++i)
    {
        logMag[i] = std::log10(1.0f + fft.magnitude[i] * 1000.0f);
        peak = std::max(peak, logMag[i]);
    }
    const float scale = peak > 0.0f ? 255.0f / peak : 0.0f;
    QImage out(w, h, QImage::Format_Grayscale8);
    // Shift DC to the centre for the usual "four corners" spectrum layout.
    for (int y = 0; y < h; ++y)
    {
        // Format_Grayscale8 is one byte per pixel; QImage::setPixel() would
        // reinterpret a QRgb through Qt's colour conversion and the image came
        // out solid black.
        std::uint8_t *row = out.scanLine(y);
        for (int x = 0; x < w; ++x)
        {
            const int sx = (x + w / 2) % w;
            const int sy = (y + h / 2) % h;
            const float v = logMag[static_cast<size_t>(sy) * w + sx];
            row[x] = static_cast<std::uint8_t>(v * scale);
        }
    }
    return out.convertToFormat(QImage::Format_RGB32);
}
} // namespace

void ImageViewer::setSpectrumVisible(bool visible)
{
    if (m_spectrumVisible == visible)
        return;
    m_spectrumVisible = visible;
    if (visible && !m_spectrumPanel)
    {
        m_spectrumPanel = new QWidget(this);
        m_spectrumPanel->setObjectName(QStringLiteral("spectrumPanel"));
        auto *layout = new QVBoxLayout(m_spectrumPanel);
        layout->setContentsMargins(4, 4, 4, 4);
        auto *label = new QLabel(m_spectrumPanel);
        label->setObjectName(QStringLiteral("spectrumImage"));
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        m_spectrumPanel->setWindowTitle(tr("FFT Spectrum"));
    }
    if (m_spectrumPanel)
    {
        m_spectrumPanel->setVisible(visible);
        if (visible)
        {
            layoutFloatingOverlays();
            m_spectrumPanel->raise();
            if (m_frame && m_frame->isValid())
            {
                const int w = m_frame->width();
                const int h = m_frame->height();
                const int stepX = std::max(1, w / kSpectrumMaxEdge);
                const int stepY = std::max(1, h / kSpectrumMaxEdge);
                const int stride = (w + stepX - 1) / stepX;
                const int sampledH = (h + stepY - 1) / stepY;
                const auto fft = mviewer::core::computeFftMagnitude(
                    lumaSamples(m_frame->pixels(), w, h), stride, sampledH);
                if (QLabel *label = m_spectrumPanel->findChild<QLabel *>())
                {
                    const QImage image = spectrumImage(fft);
                    if (!image.isNull())
                        label->setPixmap(QPixmap::fromImage(image).scaled(
                            label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                }
            }
        }
    }
}

void ImageViewer::loadCvBoxes(const QString &path)
{
    QString error;
    if (!m_cvOverlay.loadFromJsonFile(path, &error))
    {
        emit statusMessageRequested(tr("CV boxes: cannot read %1").arg(QFileInfo(path).fileName()),
                                    4000);
        return;
    }
    emit statusMessageRequested(tr("CV boxes: %1 loaded").arg(m_cvOverlay.count()), 4000);
    update();
}

void ImageViewer::drawAnalysisOverlay(QPainter &painter) const
{
    m_cvOverlay.draw(painter, m_view);
}
