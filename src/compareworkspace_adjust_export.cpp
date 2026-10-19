// Per-cell USM controls and full-resolution adjusted export.
#include "compareworkspace_p.h"

#include "core/image/Encoder.h"
#include "core/image/ImageFrame.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{

using ExportStatus = std::function<void(const QString &, int)>;
using ExportTaskSink = std::function<void(TaskScheduler::TaskHandle)>;

struct AdjustedExportRequest
{
    mviewer::core::CellImageAdjust adjust;
    std::string outPath;
    std::shared_ptr<std::atomic<bool>> cancel;
    ExportStatus status;
    ExportTaskSink storeTask;
};

bool exportCancelled(const std::shared_ptr<std::atomic<bool>> &cancel,
                     const TaskScheduler::TaskContext *context)
{
    if (cancel && cancel->load(std::memory_order_relaxed))
        return true;
    return context && context->isCancelled();
}

void finishAdjustedExport(const ImageData &pixels, const AdjustedExportRequest &request,
                          const TaskScheduler::TaskContext &context)
{
    const auto stopped = [&]() { return exportCancelled(request.cancel, &context); };
    if (stopped())
        return;
    const ImageData out = mviewer::core::applyCellImageAdjust(pixels, request.adjust, stopped);
    if (stopped())
        return;
    if (out.isNull())
    {
        if (request.status)
            request.status(QStringLiteral("Export failed: adjustment processing failed"), 6000);
        return;
    }
    const bool wrote = Encoder::encode(out, request.outPath);
    if (stopped())
        return;
    if (!request.status)
        return;
    if (!wrote)
    {
        request.status(QStringLiteral("Export failed: cannot write file"), 6000);
        return;
    }
    const QString name = QFileInfo(QString::fromUtf8(request.outPath.c_str())).fileName();
    request.status(QStringLiteral("Tuning result exported: %1").arg(name), 5000);
}

void onAdjustedExportDecoded(const mviewer::application::ImageLoadingService::Result &res,
                             const AdjustedExportRequest &request)
{
    if (exportCancelled(request.cancel, nullptr))
        return;
    if (!res.success() || !res.frame || res.frame->pixels().isNull())
    {
        if (request.status)
            request.status(
                QStringLiteral("Export failed: unable to decode the full-resolution image"), 6000);
        return;
    }
    const ImageData pixels = res.frame->pixels();
    TaskScheduler::TaskHandle task = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [pixels, request](const TaskScheduler::TaskContext &context)
        { finishAdjustedExport(pixels, request, context); });
    if (!task)
    {
        if (request.status)
            request.status(QStringLiteral("Export failed: adjustment processing failed"), 6000);
        return;
    }
    if (request.storeTask)
        request.storeTask(std::move(task));
}

QString adjustedExportSuggestion(const QString &sourcePath)
{
    const QString saved = QSettings().value(QStringLiteral("compare/adjustedExportDir")).toString();
    const QFileInfo source(sourcePath);
    const QString dir = (!saved.isEmpty() && QDir(saved).exists()) ? saved : source.absolutePath();
    return QDir(dir).filePath(source.completeBaseName() + QStringLiteral("_adjusted.png"));
}

QString ensureAdjustedExportSuffix(const QString &path, const QString &selectedFilter)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png") || suffix == QLatin1String("tif") ||
        suffix == QLatin1String("tiff"))
        return path;
    const bool tiff = selectedFilter.contains(QStringLiteral("tif"), Qt::CaseInsensitive);
    return path + (tiff ? QStringLiteral(".tif") : QStringLiteral(".png"));
}

void setBlockedSlider(QSlider *slider, QLabel *label, int value, const QString &text)
{
    if (!slider)
        return;
    const QSignalBlocker blocker(slider);
    slider->setValue(value);
    if (label)
        label->setText(text);
}

} // namespace

void CompareWorkspace::buildUsmControls(QVBoxLayout *editLay)
{
    m_usmChk = new QCheckBox(tr("USM Sharpen"), m_editPanel);
    m_usmChk->setObjectName(QStringLiteral("usmEnabledCheck"));
    editLay->addWidget(m_usmChk);

    auto addSlider = [this, editLay](const QString &label, QSlider *&slider, QLabel *&value,
                                     const char *objectName, int minValue, int maxValue,
                                     int initial, const QString &initialText)
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, m_editPanel));
        slider = new QSlider(Qt::Horizontal, m_editPanel);
        slider->setObjectName(QLatin1String(objectName));
        slider->setRange(minValue, maxValue);
        slider->setValue(initial);
        value = new QLabel(initialText, m_editPanel);
        value->setMinimumWidth(40);
        row->addWidget(slider);
        row->addWidget(value);
        editLay->addLayout(row);
    };
    addSlider(tr("Quantity"), m_usmAmountSlider, m_usmAmountVal, "usmAmountSlider", 0, 500, 150,
              QStringLiteral("150%"));
    addSlider(tr("Radius"), m_usmRadiusSlider, m_usmRadiusVal, "usmRadiusSlider", 1, 200, 15,
              QStringLiteral("1.5"));
    addSlider(tr("Threshold"), m_usmThresholdSlider, m_usmThresholdVal, "usmThresholdSlider", 0,
              255, 4, QStringLiteral("4"));
    connect(m_usmAmountSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (m_usmAmountVal)
                    m_usmAmountVal->setText(QString::number(value) + QLatin1Char('%'));
                onAdjChanged();
            });
    connect(m_usmRadiusSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (m_usmRadiusVal)
                    m_usmRadiusVal->setText(QString::number(value / 10.0, 'f', 1));
                onAdjChanged();
            });
    connect(m_usmThresholdSlider, &QSlider::valueChanged, this,
            [this](int value)
            {
                if (m_usmThresholdVal)
                    m_usmThresholdVal->setText(QString::number(value));
                onAdjChanged();
            });

    m_usmLumaChk = new QCheckBox(tr("Brightness only"), m_editPanel);
    m_usmLumaChk->setObjectName(QStringLiteral("usmLumaOnlyCheck"));
    editLay->addWidget(m_usmLumaChk);
    connect(m_usmLumaChk, &QCheckBox::toggled, this, [this](bool) { onAdjChanged(); });
    connect(m_usmChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (m_usmAmountSlider)
                    m_usmAmountSlider->setEnabled(on);
                if (m_usmRadiusSlider)
                    m_usmRadiusSlider->setEnabled(on);
                if (m_usmThresholdSlider)
                    m_usmThresholdSlider->setEnabled(on);
                if (m_usmLumaChk)
                    m_usmLumaChk->setEnabled(on);
                onAdjChanged();
            });
    if (m_usmAmountSlider)
        m_usmAmountSlider->setEnabled(false);
    if (m_usmRadiusSlider)
        m_usmRadiusSlider->setEnabled(false);
    if (m_usmThresholdSlider)
        m_usmThresholdSlider->setEnabled(false);
    if (m_usmLumaChk)
        m_usmLumaChk->setEnabled(false);
}

void CompareWorkspace::syncUsmControls(const CellAdjust &adjust)
{
    if (!m_usmChk)
        return;
    {
        const QSignalBlocker blocker(m_usmChk);
        m_usmChk->setChecked(adjust.usmEnabled);
    }
    const int amount = std::clamp(static_cast<int>(std::lround(adjust.usmAmount * 100.0)), 0, 500);
    const int radius = std::clamp(static_cast<int>(std::lround(adjust.usmRadius * 10.0)), 1, 200);
    const int threshold = std::clamp(adjust.usmThreshold, 0, 255);
    setBlockedSlider(m_usmAmountSlider, m_usmAmountVal, amount,
                     QString::number(amount) + QLatin1Char('%'));
    setBlockedSlider(m_usmRadiusSlider, m_usmRadiusVal, radius,
                     QString::number(radius / 10.0, 'f', 1));
    setBlockedSlider(m_usmThresholdSlider, m_usmThresholdVal, threshold,
                     QString::number(threshold));
    if (m_usmLumaChk)
    {
        const QSignalBlocker blocker(m_usmLumaChk);
        m_usmLumaChk->setChecked(adjust.usmLumaOnly);
    }
    if (m_usmAmountSlider)
        m_usmAmountSlider->setEnabled(adjust.usmEnabled);
    if (m_usmRadiusSlider)
        m_usmRadiusSlider->setEnabled(adjust.usmEnabled);
    if (m_usmThresholdSlider)
        m_usmThresholdSlider->setEnabled(adjust.usmEnabled);
    if (m_usmLumaChk)
        m_usmLumaChk->setEnabled(adjust.usmEnabled);
}

void CompareWorkspace::addAdjustedExportButton(QVBoxLayout *editLay)
{
    m_exportAdjustedBtn =
        new QPushButton(tr("Export adjusted result for this image..."), m_editPanel);
    m_exportAdjustedBtn->setObjectName(QStringLiteral("exportAdjustedButton"));
    m_exportAdjustedBtn->setToolTip(
        tr("Export current pane at full resolution with brightness/contrast/gamma, white balance, "
           "USM, rotate/flip, and crop applied, "
           "as PNG or TIFF without overwriting the original"));
    connect(m_exportAdjustedBtn, &QPushButton::clicked, this,
            &CompareWorkspace::exportCurrentCellAdjusted);
    editLay->addWidget(m_exportAdjustedBtn);
}

void CompareWorkspace::trackCellPointerForExportMenu(int idx, QEvent *event)
{
    if (!event)
        return;
    if (event->type() == QEvent::MouseButtonPress)
    {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::RightButton)
            return;
        m_cellMenuPress = true;
        m_cellMenuMoved = false;
        m_cellMenuPressPos = me->pos();
        m_cellMenuIdx = idx;
        return;
    }
    if (event->type() == QEvent::MouseMove && m_cellMenuPress)
    {
        const auto *me = static_cast<QMouseEvent *>(event);
        if ((me->pos() - m_cellMenuPressPos).manhattanLength() >= 4)
            m_cellMenuMoved = true;
        return;
    }
    if (event->type() != QEvent::MouseButtonRelease)
        return;
    const auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::RightButton)
        return;
    const bool show = m_cellMenuPress && !m_cellMenuMoved && m_cellMenuIdx == idx;
    const QPoint globalPos = me->globalPosition().toPoint();
    m_cellMenuPress = false;
    if (show)
        queueCellExportMenu(idx, globalPos);
}

void CompareWorkspace::queueCellExportMenu(int idx, const QPoint &globalPos)
{
    const QPointer<CompareWorkspace> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard, idx, globalPos]()
        {
            if (CompareWorkspace *workspace = guard.data())
                workspace->showCellExportMenu(idx, globalPos);
        },
        Qt::QueuedConnection);
}

void CompareWorkspace::showCellExportMenu(int idx, const QPoint &globalPos)
{
    if (idx < 0 || idx >= m_cellViews.size())
        return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("cellExportMenu"));
    QAction *action = menu.addAction(tr("Export adjusted result for this image..."));
    action->setObjectName(QStringLiteral("exportAdjustedAction"));
    connect(action, &QAction::triggered, this, [this, idx]() { exportCellAdjusted(idx); });
    menu.exec(globalPos);
}

void CompareWorkspace::exportCurrentCellAdjusted()
{
    int idx = m_editIdx;
    if (idx < 0)
        idx = m_focusIndex;
    if (idx < 0)
        idx = m_hoverIdx;
    exportCellAdjusted(idx);
}

void CompareWorkspace::exportCellAdjusted(int idx)
{
    if (idx < 0 || idx >= m_engine.imageCount())
    {
        showCompareStatus(tr("Export failed: no exportable images"), 6000);
        return;
    }
    const ImageFrame *img = m_engine.imageAt(idx);
    std::string sourcePath;
    if (img && !img->metadata().filePath.empty())
        sourcePath = img->metadata().filePath;
    else if (idx < static_cast<int>(m_comparePaths.size()))
        sourcePath = m_comparePaths[static_cast<size_t>(idx)];
    if (sourcePath.empty())
    {
        showCompareStatus(tr("Export failed: source file not found"), 6000);
        return;
    }
    QString selectedFilter;
    const QString selected = QFileDialog::getSaveFileName(
        this, tr("Export this image tuning result"),
        adjustedExportSuggestion(QString::fromStdString(sourcePath)),
        tr("PNG Image (*.png);;TIFF Image (*.tif *.tiff)"), &selectedFilter);
    if (selected.isEmpty())
        return;
    const QString outPath = ensureAdjustedExportSuffix(selected, selectedFilter);
    QSettings().setValue(QStringLiteral("compare/adjustedExportDir"),
                         QFileInfo(outPath).absolutePath());
    const CellAdjust adjust = idx < static_cast<int>(m_cellAdjusts.size())
                                  ? m_cellAdjusts[static_cast<size_t>(idx)]
                                  : CellAdjust{};
    int frameIndex = 0;
    if (m_session && idx >= 0 && idx < static_cast<int>(m_session->frameIndices.size()))
        frameIndex = std::max(0, m_session->frameIndices[static_cast<size_t>(idx)]);
    else if (img)
        frameIndex = std::max(0, img->frameIndex());
    showCompareStatus(tr("Exporting tuning results…"), 5000);
    cancelAdjustedExport();
    launchAdjustedExport(sourcePath, frameIndex, outPath, toCellImageAdjust(adjust));
}

void CompareWorkspace::cancelAdjustedExport()
{
    ++m_adjustExportGen;
    if (m_adjustExportCancel)
        m_adjustExportCancel->store(true, std::memory_order_relaxed);
    if (m_adjustExportTask)
        TaskScheduler::cancel(m_adjustExportTask);
    m_adjustExportTask.reset();
    if (m_adjustExportHandle)
        mviewer::application::ImageLoadingService::instance().cancelAsync(m_adjustExportHandle);
}

void CompareWorkspace::launchAdjustedExport(const std::string &sourcePath, int frameIndex,
                                            const QString &outPath,
                                            const mviewer::core::CellImageAdjust &adjust)
{
    const uint64_t generation = m_adjustExportGen;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_adjustExportCancel = cancel;
    const QPointer<CompareWorkspace> guard(this);
    AdjustedExportRequest request;
    request.adjust = adjust;
    request.outPath = outPath.toUtf8().toStdString();
    request.cancel = std::move(cancel);
    request.status = [guard, generation](const QString &text, int msec)
    {
        if (!qApp)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, text, msec]()
            {
                CompareWorkspace *workspace = guard.data();
                if (!workspace || generation != workspace->m_adjustExportGen)
                    return;
                workspace->showCompareStatus(text, msec);
            },
            Qt::QueuedConnection);
    };
    request.storeTask = [guard, generation](TaskScheduler::TaskHandle task)
    {
        if (!qApp)
        {
            if (task)
                TaskScheduler::cancel(task);
            return;
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, task]()
            {
                CompareWorkspace *workspace = guard.data();
                TaskScheduler::TaskHandle handle = task;
                if (!workspace || generation != workspace->m_adjustExportGen)
                {
                    if (handle)
                        TaskScheduler::cancel(handle);
                    return;
                }
                workspace->m_adjustExportTask = std::move(handle);
            },
            Qt::QueuedConnection);
    };
    const ImageLoadOptions opts{true, false, 256};
    m_adjustExportHandle = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        sourcePath, frameIndex,
        [request](const mviewer::application::ImageLoadingService::Result &res)
        { onAdjustedExportDecoded(res, request); }, opts, m_lifetime);
    if (!m_adjustExportHandle && request.status)
        request.status(QStringLiteral("Export failed: unable to decode the full-resolution image"),
                       6000);
}
