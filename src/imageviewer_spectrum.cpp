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
        for (int x = 0; x < w; ++x)
        {
            const int sx = (x + w / 2) % w;
            const int sy = (y + h / 2) % h;
            const float v = logMag[static_cast<size_t>(sy) * w + sx];
            out.setPixel(x, y, qRgb(static_cast<int>(v * scale), 0, 0));
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
            const int edge = std::min(320, std::max(160, std::min(width(), height()) / 2));
            m_spectrumPanel->setGeometry(width() - edge - 12, 12, edge, edge + 16);
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
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit statusMessageRequested(tr("CV boxes: cannot read %1").arg(QFileInfo(path).fileName()),
                                    4000);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QJsonArray boxes = doc.object().value(QStringLiteral("boxes")).toArray();
    m_cvBoxes.clear();
    m_cvLabels.clear();
    m_cvBoxes.reserve(boxes.size());
    m_cvLabels.reserve(boxes.size());
    for (const QJsonValue &value : boxes)
    {
        const QJsonArray item = value.toArray();
        if (item.size() < 4)
            continue;
        const QRectF rect(item.at(0).toDouble(), item.at(1).toDouble(), item.at(2).toDouble(),
                          item.at(3).toDouble());
        if (rect.width() <= 0 || rect.height() <= 0)
            continue;
        m_cvBoxes.append(rect);
        m_cvLabels.append(item.size() >= 5 ? item.at(4).toString() : QString());
    }
    emit statusMessageRequested(tr("CV boxes: %1 loaded").arg(m_cvBoxes.size()), 4000);
    update();
}

void ImageViewer::drawAnalysisOverlay(QPainter &painter) const
{
    if (m_cvBoxes.isEmpty() || m_view.scale <= 0.0)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_cvBoxes.size(); ++i)
    {
        const QRectF &box = m_cvBoxes.at(i);
        const QPointF topLeft(box.x() * m_view.scale + m_view.offsetX,
                              box.y() * m_view.scale + m_view.offsetY);
        const QRectF screen(topLeft,
                            QSizeF(box.width() * m_view.scale, box.height() * m_view.scale));
        painter.setPen(QPen(QColor(0, 220, 120), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(screen);
        const QString label = m_cvLabels.value(i);
        if (!label.isEmpty())
            painter.drawText(screen.topLeft() + QPointF(4, -4), label);
    }
    painter.restore();
}
