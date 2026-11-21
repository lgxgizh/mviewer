#include "widgets/displayadjustpanel.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace mviewer::ui
{
namespace
{
constexpr int kExposureSteps = 60; // -3.0 .. +3.0 EV in 0.1 steps
constexpr int kGammaSteps = 50;    // 0.5 .. 3.0 in 0.05 steps

int exposureToSlider(double ev)
{
    return static_cast<int>(std::lround((ev + 3.0) * 10.0));
}

double sliderToExposure(int value)
{
    return value / 10.0 - 3.0;
}

int gammaToSlider(double gamma)
{
    return static_cast<int>(std::lround((gamma - 0.5) * 20.0));
}

double sliderToGamma(int value)
{
    return 0.5 + value / 20.0;
}
} // namespace

DisplayAdjustPanel::DisplayAdjustPanel(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(tr("Display Adjust"));
    // Styled as a canvas floating card (see the canvasFloatCard QSS rule).
    setObjectName(QStringLiteral("canvasFloatCard"));
    auto *form = new QFormLayout(this);
    form->setContentsMargins(10, 8, 10, 8);

    // Right-aligned monospaced readout: dragging a slider used to show no
    // numeric value at all.
    m_readout = new QLabel(this);
    m_readout->setObjectName(QStringLiteral("adjustReadout"));
    m_readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(m_readout);

    m_exposure = new QSlider(Qt::Horizontal, this);
    m_exposure->setRange(0, kExposureSteps);
    m_exposure->setValue(exposureToSlider(0.0));
    form->addRow(tr("Exposure (EV)"), m_exposure);

    m_gamma = new QSlider(Qt::Horizontal, this);
    m_gamma->setRange(0, kGammaSteps);
    m_gamma->setValue(gammaToSlider(1.0));
    form->addRow(tr("Gamma"), m_gamma);

    auto *buttons = new QHBoxLayout();
    m_autoStretch = new QPushButton(tr("Auto-Stretch"), this);
    m_autoStretch->setCheckable(true);
    m_lut = new QPushButton(tr("Load 3D LUT…"), this);
    m_reset = new QPushButton(tr("Reset"), this);
    buttons->addWidget(m_autoStretch);
    buttons->addWidget(m_lut);
    buttons->addWidget(m_reset);
    form->addRow(QString(), buttons);

    connect(m_exposure, &QSlider::valueChanged, this,
            [this](int v)
            {
                updateReadout();
                emit exposureChanged(sliderToExposure(v));
            });
    connect(m_gamma, &QSlider::valueChanged, this,
            [this](int v)
            {
                updateReadout();
                emit gammaChanged(sliderToGamma(v));
            });
    connect(m_autoStretch, &QPushButton::toggled, this, &DisplayAdjustPanel::autoStretchToggled);
    connect(m_lut, &QPushButton::clicked, this, &DisplayAdjustPanel::loadLutRequested);
    connect(m_reset, &QPushButton::clicked, this, &DisplayAdjustPanel::resetRequested);
    updateReadout();
}

void DisplayAdjustPanel::updateReadout()
{
    if (!m_readout)
        return;
    const double ev = sliderToExposure(m_exposure->value());
    const double gamma = sliderToGamma(m_gamma->value());
    m_readout->setText(QStringLiteral("%1%2 EV · γ %3")
                           .arg(ev >= 0.0 ? QStringLiteral("+") : QString())
                           .arg(ev, 0, 'f', 1)
                           .arg(gamma, 0, 'f', 2));
}

void DisplayAdjustPanel::setExposure(double ev)
{
    m_exposure->setValue(exposureToSlider(ev));
    updateReadout();
}

void DisplayAdjustPanel::setGamma(double gamma)
{
    m_gamma->setValue(gammaToSlider(gamma));
    updateReadout();
}

void DisplayAdjustPanel::setAutoStretch(bool on)
{
    m_autoStretch->setChecked(on);
}

void DisplayAdjustPanel::setLutLabel(const QString &label)
{
    m_lut->setText(label.isEmpty() ? tr("Load 3D LUT…") : label);
}

} // namespace mviewer::ui
