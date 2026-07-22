#include "batchdialog.h"

#include <QCheckBox>
#include <QComboBox>
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
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

BatchDialog::BatchDialog(QWidget *parent)
    : QDialog(parent)
    , m_processor(std::make_unique<mviewer::core::BatchProcessor>())
{
    setWindowTitle("批量处理");
    setMinimumSize(640, 600);

    auto *mainLayout = new QVBoxLayout(this);

    // ── file list ──────────────────────────────────────────────────
    auto *fileGroup = new QVBoxLayout;
    fileGroup->addWidget(new QLabel("文件列表:"));
    m_fileList = new QListWidget;
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileGroup->addWidget(m_fileList);

    auto *fileBtnBar = new QHBoxLayout;
    m_addBtn = new QPushButton("添加文件...");
    m_removeBtn = new QPushButton("移除选中");
    fileBtnBar->addWidget(m_addBtn);
    fileBtnBar->addWidget(m_removeBtn);
    fileBtnBar->addStretch();
    fileGroup->addLayout(fileBtnBar);
    mainLayout->addLayout(fileGroup);

    // ── operations ─────────────────────────────────────────────────
    auto *opGroup = new QHBoxLayout;
    m_chkAnalyze = new QCheckBox("分析");
    m_chkResize = new QCheckBox("缩放");
    m_chkWatermark = new QCheckBox("水印");
    m_chkRename = new QCheckBox("重命名");
    m_chkExport = new QCheckBox("导出");
    m_chkExport->setChecked(true);
    opGroup->addWidget(m_chkAnalyze);
    opGroup->addWidget(m_chkResize);
    opGroup->addWidget(m_chkWatermark);
    opGroup->addWidget(m_chkRename);
    opGroup->addWidget(m_chkExport);
    opGroup->addStretch();
    mainLayout->addLayout(opGroup);

    // ── params grid ────────────────────────────────────────────────
    auto *paramLayout = new QVBoxLayout;

    // resize
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("缩放最大边:"));
        m_resizeMaxEdge = new QSpinBox;
        m_resizeMaxEdge->setRange(64, 32768);
        m_resizeMaxEdge->setValue(1920);
        row->addWidget(m_resizeMaxEdge);
        row->addStretch();
        paramLayout->addLayout(row);
    }
    // watermark
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("水印文字:"));
        m_watermarkText = new QLineEdit;
        m_watermarkText->setPlaceholderText("© 2025");
        row->addWidget(m_watermarkText);
        m_watermarkPos = new QComboBox;
        m_watermarkPos->addItems({"左上", "右上", "左下", "右下", "居中", "平铺"});
        m_watermarkPos->setCurrentIndex(4);
        row->addWidget(m_watermarkPos);
        m_watermarkOpacity = new QDoubleSpinBox;
        m_watermarkOpacity->setRange(0.0, 1.0);
        m_watermarkOpacity->setSingleStep(0.05);
        m_watermarkOpacity->setValue(0.3);
        row->addWidget(m_watermarkOpacity);
        m_watermarkFontSize = new QSpinBox;
        m_watermarkFontSize->setRange(8, 200);
        m_watermarkFontSize->setValue(24);
        row->addWidget(m_watermarkFontSize);
        paramLayout->addLayout(row);
    }
    // rename
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("重命名模式:"));
        m_renamePattern = new QLineEdit;
        m_renamePattern->setPlaceholderText("{name}_batched_{seq:3}");
        row->addWidget(m_renamePattern);
        paramLayout->addLayout(row);
    }
    // export
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("导出格式:"));
        m_exportFormat = new QComboBox;
        m_exportFormat->addItems({"png", "jpg", "bmp", "webp"});
        row->addWidget(m_exportFormat);
        row->addWidget(new QLabel("质量:"));
        m_exportQuality = new QSpinBox;
        m_exportQuality->setRange(1, 100);
        m_exportQuality->setValue(90);
        row->addWidget(m_exportQuality);
        paramLayout->addLayout(row);
    }
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("输出目录:"));
        m_outputDir = new QLineEdit;
        m_outputDir->setPlaceholderText("(留空=原目录)");
        row->addWidget(m_outputDir);
        m_browseBtn = new QPushButton("浏览...");
        row->addWidget(m_browseBtn);
        paramLayout->addLayout(row);
    }
    mainLayout->addLayout(paramLayout);

    // ── progress + log ────────────────────────────────────────────
    m_progress = new QProgressBar;
    mainLayout->addWidget(m_progress);

    m_statusLabel = new QLabel("就绪");
    mainLayout->addWidget(m_statusLabel);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(150);
    mainLayout->addWidget(m_log);

    // ── buttons ───────────────────────────────────────────────────
    auto *btnBar = new QHBoxLayout;
    m_startBtn = new QPushButton("开始");
    m_cancelBtn = new QPushButton("取消处理");
    m_cancelBtn->setEnabled(false);
    m_closeBtn = new QPushButton("关闭");
    btnBar->addStretch();
    btnBar->addWidget(m_startBtn);
    btnBar->addWidget(m_cancelBtn);
    btnBar->addWidget(m_closeBtn);
    mainLayout->addLayout(btnBar);

    // ── connections ────────────────────────────────────────────────
    connect(m_addBtn, &QPushButton::clicked, this, &BatchDialog::onAddFiles);
    connect(m_removeBtn, &QPushButton::clicked, this, &BatchDialog::onRemoveSelected);
    connect(m_startBtn, &QPushButton::clicked, this, &BatchDialog::onStart);
    connect(m_cancelBtn, &QPushButton::clicked, this, &BatchDialog::onCancel);
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
    const auto files = QFileDialog::getOpenFileNames(
        this, "选择文件", {},
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)");
    for (const auto &f : files)
        m_fileList->addItem(f);
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
    if (m_chkWatermark->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Watermark);
    if (m_chkRename->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Rename);
    if (m_chkExport->isChecked())
        config.operations.push_back(mviewer::domain::BatchOp::Export);

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
        [this](int current, int total, const std::string &path)
        {
            QMetaObject::invokeMethod(
                this,
                [this, current, total, path]()
                {
                    m_progress->setValue(current);
                    if (!path.empty())
                    {
                        m_statusLabel->setText(
                            QString("处理中 (%1/%2): %3")
                                .arg(current + 1)
                                .arg(total)
                                .arg(QString::fromStdString(path)));
                    }
                },
                Qt::QueuedConnection);
        });

    // Move processor ownership into a shared_ptr for the background thread;
    // a fresh processor is created when the job finishes so the dialog can
    // be reused.
    std::shared_ptr<mviewer::core::BatchProcessor> proc = std::move(m_processor);

    auto future = QtConcurrent::run([proc, config = std::move(config)]()
                                    { return proc->execute(config); });

    auto *watcher = new QFutureWatcher<mviewer::domain::BatchJobResult>(this);
    connect(watcher, &QFutureWatcher<mviewer::domain::BatchJobResult>::finished,
            this,
            [this, watcher]()
            {
                auto result = watcher->result();

                m_progress->setValue(m_progress->maximum());
                m_statusLabel->setText(
                    QString("完成: %1 成功, %2 失败")
                        .arg(result.totalSucceeded)
                        .arg(result.totalFailed));

                // Log results.
                for (const auto &r : result.fileResults)
                {
                    QString line = r.success
                                       ? QString("[OK] %1 → %2")
                                             .arg(QString::fromStdString(r.inputPath))
                                             .arg(QString::fromStdString(r.outputPath))
                                       : QString("[FAIL] %1: %2")
                                             .arg(QString::fromStdString(r.inputPath))
                                             .arg(QString::fromStdString(r.errorMessage));
                    m_log->append(line);
                }

                // Create a fresh processor so the dialog can be reused.
                m_processor = std::make_unique<mviewer::core::BatchProcessor>();
                updateUiState(false);
                watcher->deleteLater();
            });

    watcher->setFuture(future);
}

void BatchDialog::onCancel()
{
    m_processor->requestCancel();
    m_statusLabel->setText("正在取消...");
}

void BatchDialog::updateUiState(bool running)
{
    m_startBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_addBtn->setEnabled(!running);
    m_removeBtn->setEnabled(!running);
    m_closeBtn->setEnabled(!running);
}
