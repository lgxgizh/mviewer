#include "batchdialog.h"

#include "core/image/ImageFormats.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

namespace
{

int addSupportedImages(QListWidget *list, const QString &dir, bool recursive)
{
    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(dir, QDir::Files | QDir::Readable, flags);
    int added = 0;
    while (it.hasNext())
    {
        const QString path = it.next();
        if (mviewer::core::ImageFormats::isSupportedPath(path.toStdString()))
        {
            list->addItem(path);
            ++added;
        }
    }
    return added;
}

QWidget *makeResizePanel(QSpinBox *&maxEdge)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchResizePanel"));
    auto *row = new QHBoxLayout(panel);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel("缩放最大边:"));
    maxEdge = new QSpinBox;
    maxEdge->setRange(64, 32768);
    maxEdge->setValue(1920);
    maxEdge->setSuffix(" px");
    row->addWidget(maxEdge);
    row->addStretch();
    return panel;
}

QWidget *makeCropPanel(QSpinBox *&x, QSpinBox *&y, QSpinBox *&w, QSpinBox *&h)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchCropPanel"));
    panel->setToolTip("按像素矩形裁剪；超出图像范围时自动裁到有效区域");
    auto *grid = new QGridLayout(panel);
    grid->setContentsMargins(0, 0, 0, 0);
    x = new QSpinBox;
    y = new QSpinBox;
    w = new QSpinBox;
    h = new QSpinBox;
    x->setObjectName(QStringLiteral("batchCropX"));
    y->setObjectName(QStringLiteral("batchCropY"));
    w->setObjectName(QStringLiteral("batchCropW"));
    h->setObjectName(QStringLiteral("batchCropH"));
    x->setRange(0, 100000);
    y->setRange(0, 100000);
    w->setRange(1, 100000);
    h->setRange(1, 100000);
    w->setValue(256);
    h->setValue(256);
    grid->addWidget(new QLabel("X:"), 0, 0);
    grid->addWidget(x, 0, 1);
    grid->addWidget(new QLabel("Y:"), 0, 2);
    grid->addWidget(y, 0, 3);
    grid->addWidget(new QLabel("宽:"), 1, 0);
    grid->addWidget(w, 1, 1);
    grid->addWidget(new QLabel("高:"), 1, 2);
    grid->addWidget(h, 1, 3);
    grid->setColumnStretch(4, 1);
    return panel;
}

QWidget *makeWatermarkPanel(QLineEdit *&text, QComboBox *&pos, QDoubleSpinBox *&opacity,
                            QSpinBox *&fontSize)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchWatermarkPanel"));
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel("水印文字:"));
    text = new QLineEdit;
    text->setPlaceholderText("© 2025");
    row1->addWidget(text, 1);
    row1->addWidget(new QLabel("位置:"));
    pos = new QComboBox;
    pos->addItems({"左上", "右上", "左下", "右下", "居中", "平铺"});
    pos->setCurrentIndex(4);
    row1->addWidget(pos);
    lay->addLayout(row1);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel("不透明度:"));
    opacity = new QDoubleSpinBox;
    opacity->setRange(0.0, 1.0);
    opacity->setSingleStep(0.05);
    opacity->setValue(0.3);
    row2->addWidget(opacity);
    row2->addWidget(new QLabel("字号:"));
    fontSize = new QSpinBox;
    fontSize->setRange(8, 200);
    fontSize->setValue(24);
    row2->addWidget(fontSize);
    row2->addStretch();
    lay->addLayout(row2);
    return panel;
}

QWidget *makeRenamePanel(QLineEdit *&pattern)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchRenamePanel"));
    auto *row = new QHBoxLayout(panel);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel("重命名模式:"));
    pattern = new QLineEdit;
    pattern->setPlaceholderText("{name}_batched_{seq:3}");
    row->addWidget(pattern, 1);
    return panel;
}

QWidget *makeExportPanel(QComboBox *&format, QSpinBox *&quality, QLineEdit *&outputDir,
                         QPushButton *&browseBtn)
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("batchExportPanel"));
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto *fmtRow = new QHBoxLayout;
    fmtRow->addWidget(new QLabel("导出格式:"));
    format = new QComboBox;
    format->addItems({"png", "jpg", "bmp", "webp"});
    fmtRow->addWidget(format);
    fmtRow->addWidget(new QLabel("质量:"));
    quality = new QSpinBox;
    quality->setRange(1, 100);
    quality->setValue(90);
    fmtRow->addWidget(quality);
    fmtRow->addStretch();
    lay->addLayout(fmtRow);
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(new QLabel("输出目录:"));
    outputDir = new QLineEdit;
    outputDir->setObjectName(QStringLiteral("batchOutputDir"));
    outputDir->setPlaceholderText("(留空=原目录)");
    outputRow->addWidget(outputDir, 1);
    browseBtn = new QPushButton("浏览...");
    outputRow->addWidget(browseBtn);
    lay->addLayout(outputRow);
    return panel;
}

} // namespace

BatchDialog::~BatchDialog()
{
    // Bound the QtConcurrent worker: cancel flags are atomic in the processor,
    // so in-flight work stops at its next checkpoint; the stored progress
    // callback is QPointer-guarded and can no longer touch this dialog.
    if (m_activeProcessor)
        m_activeProcessor->requestCancel();
}

BatchDialog::BatchDialog(QWidget *parent)
    : QDialog(parent), m_processor(std::make_unique<mviewer::core::BatchProcessor>())
{
    setWindowTitle("批量处理");
    setMinimumSize(720, 640);
    resize(760, 700);
    setSizeGripEnabled(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);
    buildFileControls(mainLayout);
    buildOperationControls(mainLayout);
    buildParameterControls(mainLayout);
    buildProgressControls(mainLayout);
    connectControls();
    updateParamVisibility();
}

void BatchDialog::buildFileControls(QVBoxLayout *mainLayout)
{
    auto *fileBox = new QGroupBox("文件");
    fileBox->setObjectName(QStringLiteral("batchFileGroup"));
    auto *fileLay = new QVBoxLayout(fileBox);
    fileLay->setSpacing(8);

    m_fileList = new QListWidget;
    m_fileList->setObjectName(QStringLiteral("batchFileList"));
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setMinimumHeight(160);
    fileLay->addWidget(m_fileList, 1);

    auto *fileBtnBar = new QHBoxLayout;
    m_addBtn = new QPushButton("添加文件...");
    m_addDirBtn = new QPushButton("添加目录...");
    m_removeBtn = new QPushButton("移除选中");
    m_chkRecursive = new QCheckBox("递归子目录");
    m_chkRecursive->setToolTip("添加目录时扫描子文件夹中的图片");
    fileBtnBar->addWidget(m_addBtn);
    fileBtnBar->addWidget(m_addDirBtn);
    fileBtnBar->addWidget(m_removeBtn);
    fileBtnBar->addWidget(m_chkRecursive);
    fileBtnBar->addStretch();
    fileLay->addLayout(fileBtnBar);
    mainLayout->addWidget(fileBox, 3);
}

void BatchDialog::buildOperationControls(QVBoxLayout *mainLayout)
{
    auto *opBox = new QGroupBox("操作");
    opBox->setObjectName(QStringLiteral("batchOpGroup"));
    auto *opRow = new QHBoxLayout(opBox);
    m_chkAnalyze = new QCheckBox("分析");
    m_chkResize = new QCheckBox("缩放");
    m_chkCrop = new QCheckBox("裁剪");
    m_chkWatermark = new QCheckBox("水印");
    m_chkRename = new QCheckBox("重命名");
    m_chkExport = new QCheckBox("导出");
    m_chkAnalyze->setObjectName(QStringLiteral("batchChkAnalyze"));
    m_chkResize->setObjectName(QStringLiteral("batchChkResize"));
    m_chkCrop->setObjectName(QStringLiteral("batchChkCrop"));
    m_chkWatermark->setObjectName(QStringLiteral("batchChkWatermark"));
    m_chkRename->setObjectName(QStringLiteral("batchChkRename"));
    m_chkExport->setObjectName(QStringLiteral("batchChkExport"));
    m_chkExport->setChecked(true);
    opRow->addWidget(m_chkAnalyze);
    opRow->addWidget(m_chkResize);
    opRow->addWidget(m_chkCrop);
    opRow->addWidget(m_chkWatermark);
    opRow->addWidget(m_chkRename);
    opRow->addWidget(m_chkExport);
    opRow->addStretch();
    mainLayout->addWidget(opBox);
}

void BatchDialog::buildParameterControls(QVBoxLayout *mainLayout)
{
    auto *paramBox = new QGroupBox("参数");
    paramBox->setObjectName(QStringLiteral("batchParamGroup"));
    auto *paramLay = new QVBoxLayout(paramBox);
    paramLay->setSpacing(8);

    auto *retryRow = new QHBoxLayout;
    retryRow->addWidget(new QLabel("重试次数:"));
    m_retryCount = new QSpinBox;
    m_retryCount->setRange(0, 10);
    m_retryCount->setValue(0);
    retryRow->addWidget(m_retryCount);
    retryRow->addWidget(new QLabel("重试间隔 (ms):"));
    m_retryDelay = new QSpinBox;
    m_retryDelay->setRange(0, 30000);
    m_retryDelay->setSingleStep(100);
    m_retryDelay->setValue(500);
    retryRow->addWidget(m_retryDelay);
    retryRow->addStretch();
    paramLay->addLayout(retryRow);

    m_resizePanel = makeResizePanel(m_resizeMaxEdge);
    m_cropPanel = makeCropPanel(m_cropX, m_cropY, m_cropW, m_cropH);
    m_watermarkPanel = makeWatermarkPanel(m_watermarkText, m_watermarkPos, m_watermarkOpacity,
                                          m_watermarkFontSize);
    m_renamePanel = makeRenamePanel(m_renamePattern);
    m_exportPanel = makeExportPanel(m_exportFormat, m_exportQuality, m_outputDir, m_browseBtn);
    paramLay->addWidget(m_resizePanel);
    paramLay->addWidget(m_cropPanel);
    paramLay->addWidget(m_watermarkPanel);
    paramLay->addWidget(m_renamePanel);
    paramLay->addWidget(m_exportPanel);
    mainLayout->addWidget(paramBox);
}

void BatchDialog::buildProgressControls(QVBoxLayout *mainLayout)
{
    auto *progressBox = new QGroupBox("进度");
    progressBox->setObjectName(QStringLiteral("batchProgressGroup"));
    auto *progressLay = new QVBoxLayout(progressBox);
    progressLay->setSpacing(8);

    m_progress = new QProgressBar;
    m_progress->setObjectName(QStringLiteral("batchProgress"));
    m_progress->setFormat(tr("%p%"));
    progressLay->addWidget(m_progress);
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setObjectName(QStringLiteral("batchStatusLabel"));
    progressLay->addWidget(m_statusLabel);
    m_log = new QTextEdit;
    m_log->setObjectName(QStringLiteral("batchLog"));
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(120);
    progressLay->addWidget(m_log, 1);

    auto *btnBar = new QHBoxLayout;
    m_startBtn = new QPushButton("开始");
    m_startBtn->setObjectName(QStringLiteral("batchStartButton"));
    m_pauseBtn = new QPushButton("暂停");
    m_pauseBtn->setObjectName(QStringLiteral("batchPauseButton"));
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setToolTip(tr("暂停/恢复批处理（当前文件完成后生效）"));
    m_cancelBtn = new QPushButton("取消处理");
    m_cancelBtn->setObjectName(QStringLiteral("batchCancelButton"));
    m_cancelBtn->setEnabled(false);
    m_openOutputBtn = new QPushButton("打开输出目录");
    m_openOutputBtn->setEnabled(false);
    m_openOutputBtn->setToolTip(tr("在资源管理器中打开上一个任务的输出目录"));
    m_closeBtn = new QPushButton("关闭");
    btnBar->addStretch();
    btnBar->addWidget(m_openOutputBtn);
    btnBar->addSpacing(12);
    btnBar->addWidget(m_startBtn);
    btnBar->addWidget(m_pauseBtn);
    btnBar->addWidget(m_cancelBtn);
    btnBar->addWidget(m_closeBtn);
    progressLay->addLayout(btnBar);
    mainLayout->addWidget(progressBox, 2);
}

void BatchDialog::connectControls()
{
    connect(m_addBtn, &QPushButton::clicked, this, &BatchDialog::onAddFiles);
    connect(m_addDirBtn, &QPushButton::clicked, this, &BatchDialog::onAddDir);
    connect(m_removeBtn, &QPushButton::clicked, this, &BatchDialog::onRemoveSelected);
    connect(m_startBtn, &QPushButton::clicked, this, &BatchDialog::onStart);
    connect(m_pauseBtn, &QPushButton::clicked, this, &BatchDialog::onPauseResume);
    connect(m_cancelBtn, &QPushButton::clicked, this, &BatchDialog::onCancel);
    connect(m_openOutputBtn, &QPushButton::clicked, this, &BatchDialog::onOpenOutputDir);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_browseBtn, &QPushButton::clicked, this, &BatchDialog::onBrowseOutputDir);

    const auto syncParams = [this](bool) { updateParamVisibility(); };
    connect(m_chkResize, &QCheckBox::toggled, this, syncParams);
    connect(m_chkCrop, &QCheckBox::toggled, this, syncParams);
    connect(m_chkWatermark, &QCheckBox::toggled, this, syncParams);
    connect(m_chkRename, &QCheckBox::toggled, this, syncParams);
    connect(m_chkExport, &QCheckBox::toggled, this, syncParams);
}

void BatchDialog::updateParamVisibility()
{
    if (m_resizePanel)
        m_resizePanel->setVisible(m_chkResize->isChecked());
    if (m_cropPanel)
        m_cropPanel->setVisible(m_chkCrop->isChecked());
    if (m_watermarkPanel)
        m_watermarkPanel->setVisible(m_chkWatermark->isChecked());
    if (m_renamePanel)
        m_renamePanel->setVisible(m_chkRename->isChecked());
    if (m_exportPanel)
        m_exportPanel->setVisible(m_chkExport->isChecked());
}

void BatchDialog::setInputFiles(const QStringList &paths)
{
    m_fileList->clear();
    m_fileList->addItems(paths);
}

void BatchDialog::onAddFiles()
{
    // M25: the file-picker filter follows the shipped-format SSOT.
    QString filter = "Images (";
    for (const auto &w : mviewer::core::ImageFormats::wildcardFilters())
        filter += QString::fromStdString(w) + " ";
    filter = filter.trimmed() + ")";
    const auto files = QFileDialog::getOpenFileNames(this, "选择文件", {}, filter);
    for (const auto &f : files)
        m_fileList->addItem(f);
}

void BatchDialog::onAddDir() // P2 #⑦
{
    const auto dir = QFileDialog::getExistingDirectory(this, "选择图片目录");
    if (dir.isEmpty())
        return;
    const int added = addSupportedImages(m_fileList, dir, m_chkRecursive->isChecked());
    if (added == 0)
        QMessageBox::information(this, "批量处理", "该目录下没有可识别的图片文件。");
}

void BatchDialog::onRemoveSelected()
{
    auto items = m_fileList->selectedItems();
    for (auto *item : items)
        delete item;
}

void BatchDialog::onBrowseOutputDir()
{
    const auto dir = QFileDialog::getExistingDirectory(this, "选择输出目录");
    if (!dir.isEmpty())
        m_outputDir->setText(dir);
}

void BatchDialog::buildConfig(mviewer::domain::BatchJobConfig &config) const
{
    for (int i = 0; i < m_fileList->count(); ++i)
        config.inputPaths.push_back(m_fileList->item(i)->text().toStdString());

    if (m_chkAnalyze->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Analyze);
    if (m_chkResize->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Resize);
    if (m_chkCrop->isChecked()) // P2 #⑦
        config.operations.push_back(mviewer::domain::BatchOp::Crop);
    if (m_chkWatermark->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Watermark);
    if (m_chkRename->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Rename);
    if (m_chkExport->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Export);

    // P2 #⑦: retry & recursive
    config.retryCount = m_retryCount->value();
    config.retryDelayMs = m_retryDelay->value();
    config.recursiveScan = m_chkRecursive->isChecked();

    config.resizeMaxEdge = m_resizeMaxEdge->value();
    config.cropX = m_cropX->value();
    config.cropY = m_cropY->value();
    config.cropW = m_cropW->value();
    config.cropH = m_cropH->value();
    config.watermarkText = m_watermarkText->text().toStdString();
    config.watermarkPosition = m_watermarkPos->currentIndex();
    config.watermarkOpacity = m_watermarkOpacity->value();
    config.watermarkFontSize = m_watermarkFontSize->value();
    config.renamePattern = m_renamePattern->text().toStdString();
    config.exportFormat = m_exportFormat->currentText().toStdString();
    config.exportQuality = m_exportQuality->value();
    config.outputDir = m_outputDir->text().toStdString();
}

void BatchDialog::onStart()
{
    mviewer::domain::BatchJobConfig config;
    buildConfig(config);

    if (config.inputPaths.empty())
    {
        QMessageBox::warning(this, "批量处理", "请先添加文件。");
        return;
    }

    if (config.operations.empty())
    {
        QMessageBox::warning(this, "批量处理", "请至少选择一个操作。");
        return;
    }

    updateUiState(true);
    m_progress->setRange(0, static_cast<int>(config.inputPaths.size()));
    m_progress->setValue(0);
    m_log->clear();

    // Progress callback runs on the worker thread → post updates to the UI
    // thread via invokeMethod so widget access is always safe.
    m_processor->setProgressCallback(
        [self = QPointer<BatchDialog>(this)](int current, int total, const std::string &path)
        {
            // M24 lifetime hardening: the processor runs on a QtConcurrent
            // worker; the stored callback must not capture raw `this` (a dialog
            // closed mid-batch would leave a dangling pointer). Marshal through
            // qApp and re-check the dialog is still alive before touching it.
            QMetaObject::invokeMethod(
                qApp,
                [self, current, total, path]()
                {
                    if (!self)
                        return;
                    self->m_progress->setRange(0, total);
                    self->m_progress->setValue(current);
                    if (!path.empty())
                    {
                        self->m_statusLabel->setText(
                            QString("处理中 (%1/%2): %3")
                                .arg(current + 1)
                                .arg(total)
                                .arg(
                                    QString::fromUtf8(path.data(), static_cast<int>(path.size()))));
                    }
                });
        });

    // Per-file results are streamed as they finish (same marshalling rule as the
    // progress callback). The final aggregate is still returned, so headless
    // callers keep the complete list.
    m_processor->setFileResultCallback(
        [self = QPointer<BatchDialog>(this)](const mviewer::domain::BatchFileResult &fileResult)
        {
            QMetaObject::invokeMethod(qApp,
                                      [self, fileResult]()
                                      {
                                          if (!self)
                                              return;
                                          self->m_log->append(
                                              BatchDialog::formatResultLine(fileResult));
                                      });
        });

    // Keep a shared_ptr to the processor for cancel control; the background
    // thread also holds a copy via the lambda capture. A fresh processor is
    // created when the job finishes so the dialog can be reused.
    m_activeProcessor = std::shared_ptr<mviewer::core::BatchProcessor>(m_processor.release());

    auto future = QtConcurrent::run([proc = m_activeProcessor, config = std::move(config)]()
                                    { return proc->execute(config); });

    auto *watcher = new QFutureWatcher<mviewer::domain::BatchJobResult>(this);
    connect(watcher, &QFutureWatcher<mviewer::domain::BatchJobResult>::finished, this,
            [this, watcher]() { finishBatch(watcher); });

    watcher->setFuture(future);
}

QString BatchDialog::formatResultLine(const mviewer::domain::BatchFileResult &result)
{
    const auto inputPath =
        QString::fromUtf8(result.inputPath.data(), static_cast<int>(result.inputPath.size()));
    const auto outputPath =
        QString::fromUtf8(result.outputPath.data(), static_cast<int>(result.outputPath.size()));
    const auto errorMessage =
        QString::fromUtf8(result.errorMessage.data(), static_cast<int>(result.errorMessage.size()));
    return result.success ? QString("[OK] %1 → %2").arg(inputPath).arg(outputPath)
                          : QString("[FAIL] %1: %2").arg(inputPath).arg(errorMessage);
}

void BatchDialog::finishBatch(QFutureWatcher<mviewer::domain::BatchJobResult> *watcher)
{
    // Reading the future rethrows anything the worker threw; without this guard
    // the exception escapes into the event loop and the dialog is left in its
    // running state (progress never finishes, the processor is never released).
    mviewer::domain::BatchJobResult result;
    try
    {
        result = watcher->result();
    }
    catch (const std::exception &error)
    {
        m_progress->setValue(m_progress->maximum());
        m_statusLabel->setText(tr("批量处理失败: %1").arg(QString::fromUtf8(error.what())));
        m_processor = std::make_unique<mviewer::core::BatchProcessor>();
        m_activeProcessor.reset();
        updateUiState(false);
        watcher->deleteLater();
        return;
    }
    catch (...)
    {
        m_progress->setValue(m_progress->maximum());
        m_statusLabel->setText(tr("批量处理失败: 未知错误"));
        m_processor = std::make_unique<mviewer::core::BatchProcessor>();
        m_activeProcessor.reset();
        updateUiState(false);
        watcher->deleteLater();
        return;
    }

    m_progress->setValue(m_progress->maximum());
    m_statusLabel->setText(
        QString("完成: %1 成功, %2 失败").arg(result.totalSucceeded).arg(result.totalFailed));

    // Enable "open output dir" if any files were produced and the output
    // directory is known (empty = same-as-source per file).
    m_lastOutputDir = m_outputDir->text().trimmed();
    m_openOutputBtn->setEnabled(!m_lastOutputDir.isEmpty() && result.totalSucceeded > 0 &&
                                QDir(m_lastOutputDir).exists());

    // Log lines were already streamed per file by the file-result callback; the
    // aggregate remains the authoritative count for the status text.

    // Create a fresh processor so the dialog can be reused.
    m_processor = std::make_unique<mviewer::core::BatchProcessor>();
    m_activeProcessor.reset();
    updateUiState(false);
    watcher->deleteLater();
}

void BatchDialog::onCancel()
{
    if (m_activeProcessor)
        m_activeProcessor->requestCancel();
    // If paused, resume so the cancel can take effect.
    if (m_isPaused && m_activeProcessor)
    {
        m_activeProcessor->resume();
        m_isPaused = false;
        m_pauseBtn->setText("暂停");
    }
    m_statusLabel->setText("正在取消...");
}

void BatchDialog::onPauseResume()
{
    if (!m_activeProcessor)
        return;
    if (!m_isPaused)
    {
        m_activeProcessor->requestPause();
        m_isPaused = true;
        m_pauseBtn->setText("恢复");
        m_statusLabel->setText("已暂停（当前文件完成后生效）");
        m_log->append("[PAUSE] 批处理已暂停");
    }
    else
    {
        m_activeProcessor->resume();
        m_isPaused = false;
        m_pauseBtn->setText("暂停");
        m_statusLabel->setText("已恢复处理...");
        m_log->append("[RESUME] 批处理已恢复");
    }
}

void BatchDialog::onOpenOutputDir()
{
    if (m_lastOutputDir.isEmpty() || !QDir(m_lastOutputDir).exists())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastOutputDir));
}

void BatchDialog::updateUiState(bool running)
{
    const bool idle = !running;
    m_startBtn->setEnabled(idle);
    m_pauseBtn->setEnabled(running);
    m_cancelBtn->setEnabled(running);
    m_addBtn->setEnabled(idle);
    m_addDirBtn->setEnabled(idle);
    m_removeBtn->setEnabled(idle);
    m_chkRecursive->setEnabled(idle);
    m_fileList->setEnabled(idle);
    m_browseBtn->setEnabled(idle);
    m_closeBtn->setEnabled(idle);
    m_retryCount->setEnabled(idle);
    m_retryDelay->setEnabled(idle);
    for (QCheckBox *chk :
         {m_chkAnalyze, m_chkResize, m_chkCrop, m_chkWatermark, m_chkRename, m_chkExport})
        chk->setEnabled(idle);
    for (QWidget *panel :
         {m_resizePanel, m_cropPanel, m_watermarkPanel, m_renamePanel, m_exportPanel})
    {
        if (panel)
            panel->setEnabled(idle);
    }
    if (!running)
    {
        m_isPaused = false;
        m_pauseBtn->setText("暂停");
    }
}
