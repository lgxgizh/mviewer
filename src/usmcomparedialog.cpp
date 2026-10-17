#include "usmcomparedialog.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/image/Encoder.h"
#include "core/image/QtConvert.h"
#include "core/image/UsmSharpen.h"
#include "display/DisplayColorContextProvider.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QColorSpace>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
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
    if (m_activeTask)
        TaskScheduler::cancel(m_activeTask);
}

void UsmCompareDialog::loadImageSource(const QString &filePath, const ImageData &initialData)
{
    if (!initialData.isNull())
    {
        m_sourceData = initialData;
    }
    else if (!filePath.isEmpty())
    {
        QImage loaded(filePath);
        if (!loaded.isNull())
        {
            m_sourceData = mvcore::fromQImagePreservingAlpha(loaded);
            m_meta.filePath = filePath.toStdString();
            m_meta.width = m_sourceData.width;
            m_meta.height = m_sourceData.height;
            if (loaded.colorSpace().isValid())
            {
                const QByteArray icc = loaded.colorSpace().iccProfile();
                if (!icc.isEmpty())
                {
                    m_iccProfile.assign(reinterpret_cast<const uint8_t *>(icc.constData()),
                                        reinterpret_cast<const uint8_t *>(icc.constData()) +
                                            icc.size());
                    const QByteArray encoded = icc.toBase64();
                    m_meta.textKeys["MViewer.DisplayICC.Base64"] =
                        std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
                    m_meta.hasIccProfile = true;
                }
            }
        }
    }

    if (m_sourceData.isNull())
        return;

    const auto displayCtx = DisplayColorContextProvider::forWindow(windowHandle());
    m_origQImage = mvcore::toDisplayQImage(m_sourceData, m_meta, displayCtx);
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
    m_btnCopy = new QPushButton(tr("复制结果 (Ctrl+C)"), bar);
    m_btnSave = new QPushButton(tr("另存为... (Ctrl+S)"), bar);

    layout->addWidget(m_btnFit);
    layout->addWidget(m_btn100);
    layout->addWidget(m_btn200);
    layout->addWidget(m_syncCheck);
    layout->addWidget(m_btnHoldCompare);
    layout->addStretch(1);
    layout->addWidget(m_btnCopy);
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

    auto *lblPreset = new QLabel(tr("预设模板:"), controls);
    m_comboPreset = new QComboBox(controls);
    m_comboPreset->addItems({tr("标准通用锐化"), tr("轻度细节唤醒"), tr("人像肤质保护"),
                             tr("风景建筑清晰度"), tr("强力边缘增强"), tr("自定义")});
    m_comboPreset->setCurrentIndex(0);

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

    grid->addWidget(lblPreset, 0, 0);
    grid->addWidget(m_comboPreset, 0, 1);
    grid->addWidget(m_checkLumaOnly, 0, 3, 1, 2);

    grid->addWidget(lblAmount, 1, 0);
    grid->addWidget(m_sliderAmount, 1, 1);
    grid->addWidget(m_spinAmount, 1, 2);

    grid->addWidget(lblRadius, 2, 0);
    grid->addWidget(m_sliderRadius, 2, 1);
    grid->addWidget(m_spinRadius, 2, 2);

    grid->addWidget(lblThreshold, 3, 0);
    grid->addWidget(m_sliderThreshold, 3, 1);
    grid->addWidget(m_spinThreshold, 3, 2);
    grid->addWidget(m_btnReset, 3, 3);
    grid->addWidget(m_btnApply, 3, 4);
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
    connect(m_btnCopy, &QPushButton::clicked, this, &UsmCompareDialog::copySharpenedToClipboard);
    connect(m_btnSave, &QPushButton::clicked, this, &UsmCompareDialog::saveSharpenedAs);
    connect(m_btnReset, &QPushButton::clicked, this, &UsmCompareDialog::resetParameters);
    connect(m_btnApply, &QPushButton::clicked, this, &UsmCompareDialog::scheduleSharpen);

    connect(m_comboPreset, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &UsmCompareDialog::onPresetSelected);

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
    if (!m_updatingPreset && m_comboPreset)
    {
        m_updatingPreset = true;
        m_comboPreset->setCurrentIndex(5); // 自定义
        m_updatingPreset = false;
    }
    m_debounceTimer->start();
}

void UsmCompareDialog::onPresetSelected(int index)
{
    if (m_updatingPreset || index < 0 || index >= 5)
        return;

    m_updatingPreset = true;
    switch (index)
    {
    case 0: // 标准通用锐化
        m_spinAmount->setValue(150);
        m_spinRadius->setValue(1.0);
        m_spinThreshold->setValue(0);
        m_checkLumaOnly->setChecked(true);
        break;
    case 1: // 轻度细节唤醒
        m_spinAmount->setValue(80);
        m_spinRadius->setValue(0.8);
        m_spinThreshold->setValue(2);
        m_checkLumaOnly->setChecked(true);
        break;
    case 2: // 人像肤质保护
        m_spinAmount->setValue(120);
        m_spinRadius->setValue(1.2);
        m_spinThreshold->setValue(6);
        m_checkLumaOnly->setChecked(true);
        break;
    case 3: // 风景建筑清晰度
        m_spinAmount->setValue(200);
        m_spinRadius->setValue(1.5);
        m_spinThreshold->setValue(1);
        m_checkLumaOnly->setChecked(false);
        break;
    case 4: // 强力边缘增强
        m_spinAmount->setValue(300);
        m_spinRadius->setValue(2.5);
        m_spinThreshold->setValue(0);
        m_checkLumaOnly->setChecked(false);
        break;
    default:
        break;
    }
    m_updatingPreset = false;
    m_debounceTimer->start();
}

void UsmCompareDialog::scheduleSharpen()
{
    if (m_sourceData.isNull())
        return;

    m_debounceTimer->stop();
    if (m_currentCancel)
        *m_currentCancel = true;
    if (m_activeTask)
        TaskScheduler::cancel(m_activeTask);

    const uint64_t gen = ++m_currentGen;
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    m_currentCancel = cancelFlag;

    mviewer::core::UsmParams params;
    params.amount = static_cast<double>(m_spinAmount->value()) / 100.0;
    params.radius = m_spinRadius->value();
    params.threshold = m_spinThreshold->value();
    params.lumaOnly = m_checkLumaOnly->isChecked();

    m_lblCalcStatus->setText(tr("正在计算锐化..."));

    QPointer<UsmCompareDialog> guard(this);
    m_activeTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [guard, gen, cancelFlag, params, src = m_sourceData](const TaskScheduler::TaskContext &)
        {
            if (cancelFlag->load(std::memory_order_relaxed))
                return;
            const auto start = std::chrono::steady_clock::now();
            auto cancelCb = [cancelFlag]() -> bool
            { return cancelFlag->load(std::memory_order_relaxed); };
            auto out = mviewer::core::applyUsmSharpen(src, params, cancelCb);
            if (cancelFlag->load(std::memory_order_relaxed) || out.isNull())
                return;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            const auto summary = mviewer::core::computeUsmDiffSummary(src, out);
            if (cancelFlag->load(std::memory_order_relaxed))
                return;

            QMetaObject::invokeMethod(
                qApp,
                [guard, gen, out = std::move(out), elapsed, summary]() mutable
                {
                    UsmCompareDialog *self = guard.data();
                    if (!self || gen != self->m_currentGen)
                        return;
                    self->m_sharpenedData = std::move(out);
                    const auto displayCtx =
                        DisplayColorContextProvider::forWindow(self->windowHandle());
                    self->m_sharpenedQImage =
                        mvcore::toDisplayQImage(self->m_sharpenedData, self->m_meta, displayCtx);
                    self->m_rightView->setImage(self->m_sharpenedQImage, self->m_origQImage.size(),
                                                self->m_origQImage.rect());
                    self->m_rightView->setTransform(self->m_leftView->scale(),
                                                    self->m_leftView->offset());
                    self->updateStatusInfo(elapsed, summary);
                },
                Qt::QueuedConnection);
        });
}

void UsmCompareDialog::updateStatusInfo(qint64 elapsedMs,
                                       const mviewer::core::UsmDiffSummary &summary)
{
    m_lblCalcStatus->setText(tr("计算完成 (耗时: %1 ms)").arg(elapsedMs));
    m_lblMetrics->setText(
        QString("PSNR: %1 dB | SSIM: %2 | Δ均值: %3 | Δ最大: %4 | 变化像素: %5%")
            .arg(summary.psnr >= 100.0 ? QStringLiteral("∞")
                                       : QString::number(summary.psnr, 'f', 2))
            .arg(summary.ssim, 0, 'f', 4)
            .arg(summary.meanDelta, 0, 'f', 2)
            .arg(summary.maxDelta)
            .arg(QString::number(summary.changedPercent, 'f', 1)));
}

void UsmCompareDialog::resetParameters()
{
    if (m_comboPreset)
        m_comboPreset->setCurrentIndex(0);
    onPresetSelected(0);
}

void UsmCompareDialog::copySharpenedToClipboard()
{
    if (m_sharpenedQImage.isNull())
        return;
    QApplication::clipboard()->setImage(m_sharpenedQImage);
    m_lblCalcStatus->setText(tr("已复制锐化结果到剪贴板"));
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
    if (!m_iccProfile.empty())
        params.iccProfile = m_iccProfile;
    else if (m_origQImage.colorSpace().isValid())
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
    if (event->matches(QKeySequence::Copy))
    {
        copySharpenedToClipboard();
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
