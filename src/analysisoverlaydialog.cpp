#include "analysisoverlaydialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>

#include "core/analysis/ImageOverlay.h"
#include "core/image/QtConvert.h"

namespace
{

// Scope widget: RGB-parade waveform (left) + vectorscope (right).
class ScopeWidget : public QWidget
{
  public:
    void setImage(const QImage &img)
    {
        m_img = img;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_img.isNull())
        {
            QPainter p(this);
            p.fillRect(rect(), Qt::black);
            return;
        }
        QImage buf(width(), height(), QImage::Format_ARGB32);
        buf.fill(qRgb(0, 0, 0));
        const QRect wf(0, 0, width() / 2, height());
        const QRect vs(width() / 2, 0, width() / 2, height());
        drawWaveform(buf, wf);
        drawVectorscope(buf, vs);
        QPainter p(this);
        p.drawImage(0, 0, buf);
    }

  private:
    QImage m_img;

    void drawWaveform(QImage &buf, const QRect &r) const
    {
        const int s = std::max(1, std::max(m_img.width(), m_img.height()) / r.width());
        const QImage d = m_img.scaled(m_img.width() / s, m_img.height() / s, Qt::IgnoreAspectRatio,
                                      Qt::FastTransformation);
        for (int x = 0; x < d.width(); ++x)
        {
            const int px = r.x() + (x * r.width()) / d.width();
            for (int y = 0; y < d.height(); ++y)
            {
                const QRgb c = d.pixel(x, y);
                const int ry =
                    qBound(r.top(), r.y() + ((255 - qRed(c)) * r.height()) / 255, r.bottom());
                const int gy =
                    qBound(r.top(), r.y() + ((255 - qGreen(c)) * r.height()) / 255, r.bottom());
                const int by =
                    qBound(r.top(), r.y() + ((255 - qBlue(c)) * r.height()) / 255, r.bottom());
                buf.setPixel(px, ry, qRgb(255, 40, 40));
                buf.setPixel(px, gy, qRgb(40, 255, 40));
                buf.setPixel(px, by, qRgb(40, 128, 255));
            }
        }
    }

    void drawVectorscope(QImage &buf, const QRect &r) const
    {
        QPainter g(&buf);
        g.setPen(QColor(60, 60, 60));
        g.drawEllipse(r.center(), static_cast<int>(r.width() * 0.4),
                      static_cast<int>(r.height() * 0.4));
        g.drawLine(r.center().x(), r.top(), r.center().x(), r.bottom());
        g.drawLine(r.left(), r.center().y(), r.right(), r.center().y());

        const int s = std::max(1, std::max(m_img.width(), m_img.height()) / 200);
        const QImage d = m_img.scaled(m_img.width() / s, m_img.height() / s, Qt::IgnoreAspectRatio,
                                      Qt::FastTransformation);
        for (int y = 0; y < d.height(); ++y)
            for (int x = 0; x < d.width(); ++x)
            {
                const QRgb c = d.pixel(x, y);
                const float R = qRed(c) / 255.f, G = qGreen(c) / 255.f, B = qBlue(c) / 255.f;
                const float U = -0.169f * R - 0.331f * G + 0.5f * B;
                const float V = 0.5f * R - 0.419f * G - 0.081f * B;
                const int px = r.center().x() + static_cast<int>(U * r.width() * 0.9f);
                const int py = r.center().y() - static_cast<int>(V * r.height() * 0.9f);
                if (r.contains(px, py))
                    buf.setPixel(px, py, qRgb(200, 200, 255));
            }
    }
};

} // namespace

AnalysisOverlayDialog::AnalysisOverlayDialog(const QImage &image, QWidget *parent)
    : QDialog(parent), m_src(image)
{
    setWindowTitle(tr("分析叠加层 / 示波器"));
    resize(760, 560);
    QSettings s;

    m_mode = new QComboBox;
    m_mode->addItem(tr("无"), 0);
    m_mode->addItem(tr("过曝/欠曝斑马线"), 1);
    m_mode->addItem(tr("伪彩色"), 2);
    m_mode->setCurrentIndex(m_mode->findData(s.value("defaultAnalysisOverlay", 0).toInt()));

    m_threshold = new QSlider(Qt::Horizontal);
    m_threshold->setRange(1, 40);
    m_threshold->setValue(s.value("zebraThreshold", 2).toInt());

    m_preview = new QLabel;
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(340, 260);
    m_preview->setStyleSheet("QLabel { background: #111; }");

    m_scope = new ScopeWidget;
    m_scope->setMinimumSize(340, 260);

    auto *top = new QHBoxLayout;
    top->addWidget(m_preview);
    top->addWidget(m_scope);

    auto *ctrl = new QHBoxLayout;
    ctrl->addWidget(new QLabel(tr("叠加层：")));
    ctrl->addWidget(m_mode);
    ctrl->addWidget(new QLabel(tr("斑马线阈值：")));
    ctrl->addWidget(m_threshold);
    ctrl->addStretch(1);

    auto *main = new QVBoxLayout(this);
    main->addLayout(ctrl);
    main->addLayout(top);

    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AnalysisOverlayDialog::updateOverlay);
    connect(m_threshold, &QSlider::valueChanged, this, &AnalysisOverlayDialog::updateOverlay);

    updateOverlay();
}

void AnalysisOverlayDialog::updateOverlay()
{
    if (m_src.isNull())
        return;
    const auto overlayMode = static_cast<mviewer::OverlayMode>(m_mode->currentData().toInt());

    // Work on a capped-size copy so even large images process instantly.
    QImage work = m_src.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_ARGB32);

    // Reuse the single overlay implementation shared with the ImageViewer's
    // F4 live overlay (mviewer::applyOverlay) so the dialog and the live
    // viewer can never drift in zebra / false-color behavior or appearance.
    if (overlayMode != mviewer::OverlayMode::None)
    {
        ImageData data = mvcore::fromQImage(work);
        mviewer::applyOverlay(data, overlayMode, m_threshold->value());
        work = mvcore::toQImage(data);
    }

    m_preview->setPixmap(QPixmap::fromImage(work).scaled(m_preview->size(), Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));

    static_cast<ScopeWidget *>(m_scope)->setImage(m_src);
}

void AnalysisOverlayDialog::reject()
{
    QSettings s;
    s.setValue("zebraThreshold", m_threshold->value());
    QDialog::reject();
}
