#include "batchdialog.h"

#include "core/image/ImageFormats.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

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
    setMinimumSize(640, 600);
    QVBoxLayout mainLayout(this);
    buildFileControls(mainLayout);
    buildOperationControls(mainLayout);
    buildParameterControls(mainLayout);
    buildProgressControls(mainLayout);
    connectControls();
}

void BatchDialog::buildFileControls(QVBoxLayout &mainLayout)
{
    auto *fileGroup = new QVBoxLayout;
    fileGroup->addWidget(new QLabel("文件列表:"));
    m_fileList = new QListWidget;
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileGroup->addWidget(m_fileList);

    auto *fileBtnBar = new QHBoxLayout;
    m_addBtn = new QPushButton("添加文件...");
    m_addDirBtn = new QPushButton("添加目录...");
    m_removeBtn = new QPushButton("移除选中");
    m_chkRecursive = new QCheckBox("递归子目录");
    fileBtnBar->addWidget(m_addBtn);
    fileBtnBar->addWidget(m_addDirBtn);
    fileBtnBar->addWidget(m_removeBtn);
    fileBtnBar->addWidget(m_chkRecursive);
    fileBtnBar->addStretch();
    fileGroup->addLayout(fileBtnBar);
    mainLayout.addLayout(fileGroup);

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
    mainLayout.addLayout(retryRow);
}

void BatchDialog::buildOperationControls(QVBoxLayout &mainLayout)
{
    auto *opGroup = new QHBoxLayout;
    m_chkAnalyze = new QCheckBox("分析");
    m_chkResize = new QCheckBox("缩放");
    m_chkCrop = new QCheckBox("裁剪");
    m_chkWatermark = new QCheckBox("水印");
    m_chkRename = new QCheckBox("重命名");
    m_chkExport = new QCheckBox("导出");
    m_chkExport->setChecked(true);
    opGroup->addWidget(m_chkAnalyze);
    opGroup->addWidget(m_chkResize);
    opGroup->addWidget(m_chkCrop);
    opGroup->addWidget(m_chkWatermark);
    opGroup->addWidget(m_chkRename);
    opGroup->addWidget(m_chkExport);
    opGroup->addStretch();
    mainLayout.addLayout(opGroup);
}

void BatchDialog::buildParameterControls(QVBoxLayout &mainLayout)
{
    auto *paramLayout = new QVBoxLayout;
    auto *resizeRow = new QHBoxLayout;
    resizeRow->addWidget(new QLabel("缩放最大边:"));
    m_resizeMaxEdge = new QSpinBox;
    m_resizeMaxEdge->setRange(64, 32768);
    m_resizeMaxEdge->setValue(1920);
    resizeRow->addWidget(m_resizeMaxEdge);
    resizeRow->addStretch();
    paramLayout->addLayout(resizeRow);

    auto *watermarkRow = new QHBoxLayout;
    watermarkRow->addWidget(new QLabel("水印文字:"));
    m_watermarkText = new QLineEdit;
    m_watermarkText->setPlaceholderText("© 2025");
    watermarkRow->addWidget(m_watermarkText);
    m_watermarkPos = new QComboBox;
    m_watermarkPos->addItems({"左上", "右上", "左下", "右下", "居中", "平铺"});
    m_watermarkPos->setCurrentIndex(4);
    watermarkRow->addWidget(m_watermarkPos);
    m_watermarkOpacity = new QDoubleSpinBox;
    m_watermarkOpacity->setRange(0.0, 1.0);
    m_watermarkOpacity->setSingleStep(0.05);
    m_watermarkOpacity->setValue(0.3);
    watermarkRow->addWidget(m_watermarkOpacity);
    m_watermarkFontSize = new QSpinBox;
    m_watermarkFontSize->setRange(8, 200);
    m_watermarkFontSize->setValue(24);
    watermarkRow->addWidget(m_watermarkFontSize);
    paramLayout->addLayout(watermarkRow);

    auto *renameRow = new QHBoxLayout;
    renameRow->addWidget(new QLabel("重命名模式:"));
    m_renamePattern = new QLineEdit;
    m_renamePattern->setPlaceholderText("{name}_batched_{seq:3}");
    renameRow->addWidget(m_renamePattern);
    paramLayout->addLayout(renameRow);

    auto *exportRow = new QHBoxLayout;
    exportRow->addWidget(new QLabel("导出格式:"));
    m_exportFormat = new QComboBox;
    m_exportFormat->addItems({"png", "jpg", "bmp", "webp"});
    exportRow->addWidget(m_exportFormat);
    exportRow->addWidget(new QLabel("质量:"));
    m_exportQuality = new QSpinBox;
    m_exportQuality->setRange(1, 100);
    m_exportQuality->setValue(90);
    exportRow->addWidget(m_exportQuality);
    paramLayout->addLayout(exportRow);

    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(new QLabel("输出目录:"));
    m_outputDir = new QLineEdit;
    m_outputDir->setPlaceholderText("(留空=原目录)");
    outputRow->addWidget(m_outputDir);
    m_browseBtn = new QPushButton("浏览...");
    outputRow->addWidget(m_browseBtn);
    paramLayout->addLayout(outputRow);
    mainLayout.addLayout(paramLayout);
}

void BatchDialog::buildProgressControls(QVBoxLayout &mainLayout)
{
    m_progress = new QProgressBar;
    m_progress->setFormat(tr("%p%"));
    mainLayout.addWidget(m_progress);
    m_statusLabel = new QLabel("就绪");
    mainLayout.addWidget(m_statusLabel);
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(150);
    mainLayout.addWidget(m_log);

    auto *btnBar = new QHBoxLayout;
    m_startBtn = new QPushButton("开始");
    m_pauseBtn = new QPushButton("暂停");
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setToolTip(tr("暂停/恢复批处理（当前文件完成后生效）"));
    m_cancelBtn = new QPushButton("取消处理");
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
    mainLayout.addLayout(btnBar);
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
    if (!dir.isEmpty())
        m_fileList->addItem(dir);
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
                    self->m_progress->setValue(current);
                    if (!path.empty())
                    {
                        self->m_statusLabel->setText(QString("处理中 (%1/%2): %3")
                                                         .arg(current + 1)
                                                         .arg(total)
                                                         .arg(QString::fromStdString(path)));
                    }
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
            [this, watcher]()
            {
                auto result = watcher->result();

                m_progress->setValue(m_progress->maximum());
                m_statusLabel->setText(QString("完成: %1 成功, %2 失败")
                                           .arg(result.totalSucceeded)
                                           .arg(result.totalFailed));

                // Enable "open output dir" if any files were produced and the
                // output directory is known (empty = same-as-source per file).
                m_lastOutputDir = m_outputDir->text().trimmed();
                m_openOutputBtn->setEnabled(!m_lastOutputDir.isEmpty() &&
                                            result.totalSucceeded > 0 &&
                                            QDir(m_lastOutputDir).exists());

                // Log results.
                for (const auto &r : result.fileResults)
                {
                    QString line = r.success ? QString("[OK] %1 → %2")
                                                   .arg(QString::fromStdString(r.inputPath))
                                                   .arg(QString::fromStdString(r.outputPath))
                                             : QString("[FAIL] %1: %2")
                                                   .arg(QString::fromStdString(r.inputPath))
                                                   .arg(QString::fromStdString(r.errorMessage));
                    m_log->append(line);
                }

                // Create a fresh processor so the dialog can be reused.
                m_processor = std::make_unique<mviewer::core::BatchProcessor>();
                m_activeProcessor.reset();
                updateUiState(false);
                watcher->deleteLater();
            });

    watcher->setFuture(future);
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
    m_startBtn->setEnabled(!running);
    m_pauseBtn->setEnabled(running);
    m_cancelBtn->setEnabled(running);
    m_addBtn->setEnabled(!running);
    m_removeBtn->setEnabled(!running);
    m_closeBtn->setEnabled(!running);
    if (!running)
    {
        m_isPaused = false;
        m_pauseBtn->setText("暂停");
    }
}
