#include "usmcomparedialog.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/image/Encoder.h"
#include "core/image/QtConvert.h"
#include "core/image/UsmSharpen.h"

#include <QCheckBox>
#include <QColorSpace>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>
#include <cmath>

namespace
{
QString formatName(PixelFormat fmt)
{
    switch (fmt)
    {
    case PixelFormat::RGB24:
        return QStringLiteral("RGB24");
    case PixelFormat::BGR24:
        return QStringLiteral("BGR24");
    case PixelFormat::RGBA32:
        return QStringLiteral("RGBA32");
    case PixelFormat::BGRA32:
        return QStringLiteral("BGRA32");
    case PixelFormat::Grayscale8:
        return QStringLiteral("Grayscale8");
    default:
        return QStringLiteral("Unknown");
    }
}
} // namespace

UsmCompareDialog::UsmCompareDialog(const QString &filePath, const ImageData &initialData,
                                   QWidget *parent)
    : QDialog(parent), m_filePath(filePath), m_debounceTimer(new QTimer(this))
{
    setWindowTitle(filePath.isEmpty()
                       ? tr("USM 锐化前后对比")
                       : tr("USM 锐化前后对比 - %1").arg(QFileInfo(filePath).fileName()));
    resize(1200, 780);
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    m_leftView = new RawImageView(this);
    m_rightView = new RawImageView(this);

    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);

    setupUi();
    setupConnections();
    loadImageSource(filePath, initialData);

    if (!m_sourceData.isNull())
        scheduleSharpen();
}

UsmCompareDialog::~UsmCompareDialog()
{
    if (m_currentCancel)
        *m_currentCancel = true;
}

void UsmCompareDialog::loadImageSource(const QString & /*filePath*/, const ImageData &initialData)
{
    if (!initialData.isNull())
        m_sourceData = initialData;

    if (m_sourceData.isNull())
        return;

    m_origQImage = mvcore::toQImage(m_sourceData);
    m_leftView->setImage(m_origQImage, m_origQImage.size());
    m_rightView->setImage(m_origQImage, m_origQImage.size());

    QString csDesc = QStringLiteral("sRGB");
    if (m_origQImage.colorSpace().isValid())
        csDesc = m_origQImage.colorSpace().description();

    m_lblImageInfo->setText(QString("%1×%2 | %3 | %4")
                                .arg(m_sourceData.width)
                                .arg(m_sourceData.height)
                                .arg(formatName(m_sourceData.format))
                                .arg(csDesc));
}

void UsmCompareDialog::setupUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    rootLayout->addWidget(createTopToolBar());
    rootLayout->addWidget(createCenterCanvas(), 1);
    rootLayout->addWidget(createBottomControls());
    rootLayout->addWidget(createStatusBarWidget());
}

QWidget *UsmCompareDialog::createTopToolBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_btnFit = new QPushButton(tr("适应窗口 (0)"), bar);
    m_btn100 = new QPushButton(tr("100% (1)"), bar);
    m_btn200 = new QPushButton(tr("200% (2)"), bar);
    m_syncCheck = new QCheckBox(tr("同步缩放/平移"), bar);
    m_syncCheck->setChecked(true);
    m_btnHoldCompare = new QPushButton(tr("按住看原图 (Space)"), bar);
    m_btnSave = new QPushButton(tr("另存为... (Ctrl+S)"), bar);

    layout->addWidget(m_btnFit);
    layout->addWidget(m_btn100);
    layout->addWidget(m_btn200);
    layout->addWidget(m_syncCheck);
    layout->addWidget(m_btnHoldCompare);
    layout->addStretch(1);
    layout->addWidget(m_btnSave);
    return bar;
}

QWidget *UsmCompareDialog::createCenterCanvas()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    auto *leftPane = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);
    auto *lblLeft = new QLabel(tr("原图 (Before)"), leftPane);
    lblLeft->setAlignment(Qt::AlignCenter);
    lblLeft->setStyleSheet(QStringLiteral("font-weight: bold; background: #2b2b2b; color: #e0e0e0; "
                                          "padding: 4px; border-radius: 2px;"));
    leftLayout->addWidget(lblLeft);
    leftLayout->addWidget(m_leftView, 1);

    auto *rightPane = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);
    auto *lblRight = new QLabel(tr("USM 锐化后 (After)"), rightPane);
    lblRight->setAlignment(Qt::AlignCenter);
    lblRight->setStyleSheet(
        QStringLiteral("font-weight: bold; background: #2b2b2b; color: #4fc3f7; "
                       "padding: 4px; border-radius: 2px;"));
    rightLayout->addWidget(lblRight);
    rightLayout->addWidget(m_rightView, 1);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    return splitter;
}

QWidget *UsmCompareDialog::createBottomControls()
{
    auto *controls = new QWidget(this);
    auto *grid = new QGridLayout(controls);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);

    auto *lblAmount = new QLabel(tr("数量 (Amount):"), controls);
    m_sliderAmount = new QSlider(Qt::Horizontal, controls);
    m_sliderAmount->setRange(0, 500);
    m_sliderAmount->setValue(150);
    m_spinAmount = new QSpinBox(controls);
    m_spinAmount->setRange(0, 500);
    m_spinAmount->setValue(150);
    m_spinAmount->setSuffix(QStringLiteral("%"));

    auto *lblRadius = new QLabel(tr("半径 (Radius):"), controls);
    m_sliderRadius = new QSlider(Qt::Horizontal, controls);
    m_sliderRadius->setRange(1, 200);
    m_sliderRadius->setValue(10);
    m_spinRadius = new QDoubleSpinBox(controls);
    m_spinRadius->setRange(0.1, 20.0);
    m_spinRadius->setSingleStep(0.1);
    m_spinRadius->setDecimals(1);
    m_spinRadius->setValue(1.0);
    m_spinRadius->setSuffix(QStringLiteral(" px"));

    auto *lblThreshold = new QLabel(tr("阈值 (Threshold):"), controls);
    m_sliderThreshold = new QSlider(Qt::Horizontal, controls);
    m_sliderThreshold->setRange(0, 255);
    m_sliderThreshold->setValue(0);
    m_spinThreshold = new QSpinBox(controls);
    m_spinThreshold->setRange(0, 255);
    m_spinThreshold->setValue(0);

    m_checkLumaOnly = new QCheckBox(tr("仅亮度通道锐化 (Luma Only)"), controls);
    m_checkLumaOnly->setChecked(true);
    m_btnReset = new QPushButton(tr("重置"), controls);
    m_btnApply = new QPushButton(tr("应用锐化"), controls);

    grid->addWidget(lblAmount, 0, 0);
    grid->addWidget(m_sliderAmount, 0, 1);
    grid->addWidget(m_spinAmount, 0, 2);
    grid->addWidget(lblRadius, 1, 0);
    grid->addWidget(m_sliderRadius, 1, 1);
    grid->addWidget(m_spinRadius, 1, 2);
    grid->addWidget(lblThreshold, 2, 0);
    grid->addWidget(m_sliderThreshold, 2, 1);
    grid->addWidget(m_spinThreshold, 2, 2);
    grid->addWidget(m_checkLumaOnly, 0, 3, 1, 2);
    grid->addWidget(m_btnReset, 1, 3);
    grid->addWidget(m_btnApply, 1, 4);
    return controls;
}

QWidget *UsmCompareDialog::createStatusBarWidget()
{
    auto *statusWidget = new QWidget(this);
    auto *layout = new QHBoxLayout(statusWidget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(12);

    m_lblImageInfo = new QLabel(statusWidget);
    m_lblCalcStatus = new QLabel(tr("就绪"), statusWidget);
    m_lblMetrics = new QLabel(statusWidget);

    layout->addWidget(m_lblImageInfo);
    layout->addWidget(m_lblCalcStatus);
    layout->addStretch(1);
    layout->addWidget(m_lblMetrics);
    return statusWidget;
}

void UsmCompareDialog::setupConnections()
{
    connect(m_btnFit, &QPushButton::clicked, this, &UsmCompareDialog::zoomFit);
    connect(m_btn100, &QPushButton::clicked, this, &UsmCompareDialog::zoom100);
    connect(m_btn200, &QPushButton::clicked, this, &UsmCompareDialog::zoom200);
    connect(m_btnSave, &QPushButton::clicked, this, &UsmCompareDialog::saveSharpenedAs);
    connect(m_btnReset, &QPushButton::clicked, this, &UsmCompareDialog::resetParameters);
    connect(m_btnApply, &QPushButton::clicked, this, &UsmCompareDialog::scheduleSharpen);

    connect(m_btnHoldCompare, &QPushButton::pressed, this,
            [this]()
            {
                if (!m_origQImage.isNull())
                    m_rightView->setTransientDisplay(m_origQImage, m_origQImage.size(),
                                                     m_origQImage.rect());
            });
    connect(m_btnHoldCompare, &QPushButton::released, this,
            [this]() { m_rightView->clearTransientDisplay(); });

    connect(m_leftView, &RawImageView::transformChanged, this,
            [this]() { syncTransforms(m_leftView, m_rightView); });
    connect(m_rightView, &RawImageView::transformChanged, this,
            [this]() { syncTransforms(m_rightView, m_leftView); });

    connect(m_sliderAmount, &QSlider::valueChanged, m_spinAmount, &QSpinBox::setValue);
    connect(m_spinAmount, qOverload<int>(&QSpinBox::valueChanged), m_sliderAmount,
            &QSlider::setValue);
    connect(m_sliderRadius, &QSlider::valueChanged, this,
            [this](int val) { m_spinRadius->setValue(val / 10.0); });
    connect(m_spinRadius, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double val)
            { m_sliderRadius->setValue(static_cast<int>(std::round(val * 10.0))); });
    connect(m_sliderThreshold, &QSlider::valueChanged, m_spinThreshold, &QSpinBox::setValue);
    connect(m_spinThreshold, qOverload<int>(&QSpinBox::valueChanged), m_sliderThreshold,
            &QSlider::setValue);

    connect(m_spinAmount, qOverload<int>(&QSpinBox::valueChanged), this,
            &UsmCompareDialog::onParametersChanged);
    connect(m_spinRadius, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &UsmCompareDialog::onParametersChanged);
    connect(m_spinThreshold, qOverload<int>(&QSpinBox::valueChanged), this,
            &UsmCompareDialog::onParametersChanged);
    connect(m_checkLumaOnly, &QCheckBox::toggled, this, &UsmCompareDialog::onParametersChanged);
    connect(m_debounceTimer, &QTimer::timeout, this, &UsmCompareDialog::scheduleSharpen);
}

void UsmCompareDialog::onParametersChanged()
{
    m_debounceTimer->start();
}

void UsmCompareDialog::scheduleSharpen()
{
    if (m_sourceData.isNull())
        return;

    m_debounceTimer->stop();
    if (m_currentCancel)
        *m_currentCancel = true;

    const uint64_t gen = ++m_currentGen;
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    m_currentCancel = cancelFlag;

    mviewer::core::UsmParams params;
    params.amount = static_cast<float>(m_spinAmount->value()) / 100.0f;
    params.radius = static_cast<float>(m_spinRadius->value());
    params.threshold = m_spinThreshold->value();
    params.lumaOnly = m_checkLumaOnly->isChecked();

    m_lblCalcStatus->setText(tr("正在计算锐化..."));

    m_activeTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [this, gen, cancelFlag, params, src = m_sourceData](const TaskScheduler::TaskContext &)
        {
            if (cancelFlag->load(std::memory_order_relaxed))
                return;
            const auto start = std::chrono::steady_clock::now();
            auto cancelCb = [cancelFlag]() -> bool
            { return cancelFlag->load(std::memory_order_relaxed); };
            auto out = mviewer::core::applyUsmSharpen(src, params, cancelCb);
            if (cancelFlag->load(std::memory_order_relaxed))
                return;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            const double psnr = AnalysisEngine::psnr(src, out);
            const double ssim = AnalysisEngine::ssim(src, out);
            if (cancelFlag->load(std::memory_order_relaxed))
                return;

            QMetaObject::invokeMethod(
                this,
                [this, gen, out = std::move(out), elapsed, psnr, ssim]() mutable
                {
                    if (gen != m_currentGen)
                        return;
                    m_sharpenedData = std::move(out);
                    m_sharpenedQImage = mvcore::toQImage(m_sharpenedData);
                    m_rightView->setImage(m_sharpenedQImage, m_origQImage.size(),
                                          m_origQImage.rect());
                    m_rightView->setTransform(m_leftView->scale(), m_leftView->offset());
                    updateStatusInfo(elapsed, psnr, ssim);
                },
                Qt::QueuedConnection);
        });
}

void UsmCompareDialog::updateStatusInfo(qint64 elapsedMs, double psnr, double ssim)
{
    m_lblCalcStatus->setText(tr("计算完成 (耗时: %1 ms)").arg(elapsedMs));
    m_lblMetrics->setText(
        QString("PSNR: %1 dB | SSIM: %2")
            .arg(psnr >= 100.0 ? QStringLiteral("∞") : QString::number(psnr, 'f', 2))
            .arg(ssim, 0, 'f', 4));
}

void UsmCompareDialog::resetParameters()
{
    m_spinAmount->setValue(150);
    m_spinRadius->setValue(1.0);
    m_spinThreshold->setValue(0);
    m_checkLumaOnly->setChecked(true);
    scheduleSharpen();
}

void UsmCompareDialog::saveSharpenedAs()
{
    if (m_sharpenedData.isNull())
        return;

    const QFileInfo fi(m_filePath);
    const QString defaultName =
        fi.dir().filePath(fi.baseName() + "_usm." + (fi.suffix().isEmpty() ? "png" : fi.suffix()));
    const QString savePath =
        QFileDialog::getSaveFileName(this, tr("另存为锐化图像"), defaultName,
                                     tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All Files (*)"));
    if (savePath.isEmpty())
        return;

    Encoder::Params params;
    if (m_origQImage.colorSpace().isValid())
    {
        const QByteArray icc = m_origQImage.colorSpace().iccProfile();
        if (!icc.isEmpty())
            params.iccProfile.assign(reinterpret_cast<const uint8_t *>(icc.constData()),
                                     reinterpret_cast<const uint8_t *>(icc.constData()) +
                                         icc.size());
    }
    const bool ok = Encoder::encode(m_sharpenedData, savePath.toStdString(), params);
    if (ok)
        m_lblCalcStatus->setText(tr("保存成功: %1").arg(QFileInfo(savePath).fileName()));
    else
        QMessageBox::warning(this, tr("保存失败"), tr("无法将图像保存至: %1").arg(savePath));
}

void UsmCompareDialog::zoomFit()
{
    m_leftView->resetFit();
    m_rightView->resetFit();
}

void UsmCompareDialog::zoom100()
{
    m_leftView->setTransform(1.0, QPointF(0, 0));
    m_rightView->setTransform(1.0, QPointF(0, 0));
}

void UsmCompareDialog::zoom200()
{
    m_leftView->setTransform(2.0, QPointF(0, 0));
    m_rightView->setTransform(2.0, QPointF(0, 0));
}

void UsmCompareDialog::syncTransforms(RawImageView *source, RawImageView *target)
{
    if (m_syncing || !m_syncCheck || !m_syncCheck->isChecked())
        return;
    m_syncing = true;
    target->setTransform(source->scale(), source->offset());
    m_syncing = false;
}

void UsmCompareDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        if (!m_origQImage.isNull())
            m_rightView->setTransientDisplay(m_origQImage, m_origQImage.size(),
                                             m_origQImage.rect());
        if (m_btnHoldCompare)
            m_btnHoldCompare->setDown(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_0)
    {
        zoomFit();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_1)
    {
        zoom100();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_2)
    {
        zoom200();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Save))
    {
        saveSharpenedAs();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void UsmCompareDialog::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        m_rightView->clearTransientDisplay();
        if (m_btnHoldCompare)
            m_btnHoldCompare->setDown(false);
        event->accept();
        return;
    }
    QDialog::keyReleaseEvent(event);
}
