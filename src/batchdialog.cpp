#include "batchdialog.h"
#include "batchdialog_panels.h"
#include "batchrenamepanel.h"

#include "core/image/ImageFormats.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

namespace
{

QString batchFinishText(const mviewer::domain::BatchJobResult &result)
{
    const QString counts =
        QStringLiteral("%1 成功, %2 失败").arg(result.totalSucceeded).arg(result.totalFailed);
    if (!result.cancelled && result.totalFailed <= 0)
        return QStringLiteral("完成: %1").arg(counts);

    QString names;
    int shown = 0;
    for (const auto &file : result.fileResults)
    {
        if (file.success || file.inputPath.empty())
            continue;
        if (shown >= 3)
        {
            names += QStringLiteral("…");
            break;
        }
        if (!names.isEmpty())
            names += QStringLiteral(", ");
        const QString full =
            QString::fromUtf8(file.inputPath.data(), static_cast<int>(file.inputPath.size()));
        const int slash = full.lastIndexOf(QLatin1Char('/'));
        const int back = full.lastIndexOf(QLatin1Char('\\'));
        const int cut = slash > back ? slash : back;
        names += cut >= 0 ? full.mid(cut + 1) : full;
        ++shown;
    }

    if (result.cancelled)
    {
        QString text = QStringLiteral("已取消: %1, %2 未处理").arg(counts).arg(result.totalSkipped);
        if (!names.isEmpty())
            text += QStringLiteral("（%1）").arg(names);
        return text;
    }
    return QStringLiteral("完成: %1（%2）").arg(counts, names);
}

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

QString directoryOfFirstOutput(const mviewer::domain::BatchJobResult &result)
{
    for (const auto &file : result.fileResults)
    {
        if (!file.success || file.outputPath.empty())
            continue;
        const QString path =
            QString::fromUtf8(file.outputPath.data(), static_cast<int>(file.outputPath.size()));
        return QFileInfo(path).absolutePath();
    }
    return {};
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
    const QSettings settings;
    m_overwriteExisting->setChecked(
        settings.value(QStringLiteral("batch/overwriteExisting"), false).toBool());
    m_renamePanel->loadSettings();
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

    m_resizePanel = batchdialog_detail::makeResizePanel(m_resizeMaxEdge);
    m_cropPanel = batchdialog_detail::makeCropPanel(m_cropX, m_cropY, m_cropW, m_cropH);
    m_watermarkPanel = batchdialog_detail::makeWatermarkPanel(
        m_watermarkText, m_watermarkPos, m_watermarkOpacity, m_watermarkFontSize);
    m_renamePanel = new BatchRenamePanel;
    m_exportPanel = batchdialog_detail::makeExportPanel(
        m_exportFormat, m_exportQuality, m_outputDir, m_browseBtn, m_overwriteExisting);
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

    // Rename settings: live preview uses the export format; an invalid rename
    // (bad regex, illegal or duplicate names) disables 开始 while 重命名 is on.
    connect(m_chkRename, &QCheckBox::toggled, this, &BatchDialog::refreshStartButton);
    connect(m_renamePanel, &BatchRenamePanel::validityChanged, this,
            &BatchDialog::refreshStartButton);
    connect(m_exportFormat, &QComboBox::currentTextChanged, m_renamePanel,
            &BatchRenamePanel::setTargetExtension);
    m_renamePanel->setTargetExtension(m_exportFormat->currentText());
}

bool BatchDialog::renameReady() const
{
    return !m_chkRename->isChecked() || m_renamePanel->isValid();
}

void BatchDialog::refreshStartButton()
{
    if (!m_activeProcessor)
        m_startBtn->setEnabled(renameReady());
}

void BatchDialog::syncFilesToRenamePanel()
{
    QStringList files;
    files.reserve(m_fileList->count());
    for (int i = 0; i < m_fileList->count(); ++i)
        files.append(m_fileList->item(i)->text());
    m_renamePanel->setSourceFiles(files);
}

void BatchDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // Each time the dialog opens it restores the last confirmed rename settings.
    if (!event->spontaneous() && !m_activeProcessor)
        m_renamePanel->loadSettings();
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
    syncFilesToRenamePanel();
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
    syncFilesToRenamePanel();
}

void BatchDialog::onAddDir() // P2 #⑦
{
    const auto dir = QFileDialog::getExistingDirectory(this, "选择图片目录");
    if (dir.isEmpty())
        return;
    const int added = addSupportedImages(m_fileList, dir, m_chkRecursive->isChecked());
    syncFilesToRenamePanel();
    if (added == 0)
        QMessageBox::information(this, "批量处理", "该目录下没有可识别的图片文件。");
}

void BatchDialog::onRemoveSelected()
{
    auto items = m_fileList->selectedItems();
    for (auto *item : items)
        delete item;
    syncFilesToRenamePanel();
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
    if (m_chkRename->isChecked())
    {
        const auto rename = m_renamePanel->options();
        config.renamePattern = rename.pattern;
        config.renameFind = rename.find;
        config.renameReplace = rename.replace;
        config.renameUseRegex = rename.useRegex;
        config.renameCaseSensitive = rename.caseSensitive;
    }
    config.exportFormat = m_exportFormat->currentText().toStdString();
    config.exportQuality = m_exportQuality->value();
    config.outputDir = m_outputDir->text().trimmed().toStdString();
    config.overwriteExisting = m_overwriteExisting->isChecked();
}

bool BatchDialog::validateStart(const mviewer::domain::BatchJobConfig &config)
{
    if (config.inputPaths.empty())
    {
        QMessageBox::warning(this, "批量处理", "请先添加文件。");
        return false;
    }
    if (config.operations.empty())
    {
        QMessageBox::warning(this, "批量处理", "请至少选择一个操作。");
        return false;
    }
    const bool writesNothing = m_chkRename->isChecked() || m_chkResize->isChecked() ||
                               m_chkCrop->isChecked() || m_chkWatermark->isChecked();
    if (!m_chkExport->isChecked() && writesNothing)
    {
        QMessageBox::warning(
            this, "批量处理",
            "未勾选「导出」时，重命名/缩放/裁剪/水印不会生成任何文件。请勾选「导出」。");
        return false;
    }
    if (m_chkRename->isChecked() && !m_renamePanel->isValid())
    {
        QMessageBox::warning(
            this, "批量处理",
            QStringLiteral("重命名设置无效：%1").arg(m_renamePanel->errorMessage()));
        return false;
    }
    return true;
}

void BatchDialog::onStart()
{
    mviewer::domain::BatchJobConfig config;
    buildConfig(config);
    if (!validateStart(config))
        return;

    if (m_chkRename->isChecked())
        m_renamePanel->saveSettings(); // remembered only when the run is confirmed
    QSettings settings;
    settings.setValue(QStringLiteral("batch/overwriteExisting"), m_overwriteExisting->isChecked());

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
    if (!result.success)
        return QString("[FAIL] %1: %2").arg(inputPath).arg(errorMessage);
    if (outputPath.isEmpty())
        return QString("[OK] %1（仅分析，未写出文件）").arg(inputPath);
    return QString("[OK] %1 → %2").arg(inputPath).arg(outputPath);
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
    m_statusLabel->setText(batchFinishText(result));
    if (result.cancelled)
        m_log->append(QStringLiteral("[CANCEL] 未处理 %1 个文件").arg(result.totalSkipped));
    else if (result.totalFailed > 0)
        m_log->append(QStringLiteral("[SUMMARY] %1 个文件失败").arg(result.totalFailed));

    // Enable "open output dir" if any files were produced. An empty field means
    // each file was written beside its source; open the first one that landed.
    m_lastOutputDir = m_outputDir->text().trimmed();
    if (m_lastOutputDir.isEmpty())
        m_lastOutputDir = directoryOfFirstOutput(result);
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
    m_startBtn->setEnabled(idle && renameReady());
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
    for (QWidget *panel : {m_resizePanel, m_cropPanel, m_watermarkPanel,
                           static_cast<QWidget *>(m_renamePanel), m_exportPanel})
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
