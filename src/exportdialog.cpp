#include "exportdialog.h"

#include "core/export/ExportJob.h"
#include "core/image/ImageTransform.h"
#include "core/image/QtConvert.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>

void ExportDialog::setOutputDir(const QString &dir)
{
    m_outDir = dir;
    if (m_dirEdit)
        m_dirEdit->setText(dir);
}

ExportDialog::ExportDialog(const QStringList &sources, QWidget *parent) : ExportDialog(parent)
{
    m_sources = sources;
    if (!sources.isEmpty())
    {
        const QFileInfo fi(sources.first());
        m_outDir = fi.absolutePath();
        m_dirEdit->setText(m_outDir);
    }
}

ExportDialog::ExportDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("导出图片"));
    resize(460, 520);

    auto *root = new QVBoxLayout(this);

    // ---- output directory ----
    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    m_dirEdit->setObjectName(QStringLiteral("exportOutputDirectoryEdit"));
    m_browseBtn = new QPushButton(tr("浏览..."), this);
    dirRow->addWidget(new QLabel(tr("输出目录:")));
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    root->addLayout(dirRow);

    // ---- mode ----
    auto *modeBox = new QGroupBox(tr("导出模式"));
    auto *modeLay = new QFormLayout(modeBox);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("exportModeCombo"));
    m_modeCombo->addItem(tr("转换 / 批量"), "convert");
    m_modeCombo->addItem(tr("联系表 (Contact Sheet)"), "contact");
    m_modeCombo->addItem(tr("PDF 文档"), "pdf");
    m_modeCombo->addItem(tr("CSV 报告"), "csv");
    m_modeCombo->addItem(tr("JSON 报告"), "json");
    m_modeCombo->addItem(tr("HTML 报告"), "html");
    m_modeCombo->addItem(tr("复制到剪贴板"), "clipboard");
    modeLay->addRow(tr("模式:"), m_modeCombo);
    root->addWidget(modeBox);

    // ---- format ----
    auto *fmtBox = new QGroupBox(tr("格式"));
    auto *fmtLay = new QFormLayout(fmtBox);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem("PNG", "png");
    m_formatCombo->addItem("JPEG", "jpeg");
    m_formatCombo->addItem("BMP", "bmp");
    m_formatCombo->addItem("WebP", "webp");
    m_formatCombo->addItem("TIFF", "tiff");
    m_formatCombo->setCurrentIndex(1); // JPEG
    m_qualitySpin = new QSpinBox(this);
    m_qualitySpin->setRange(1, 100);
    m_qualitySpin->setValue(90);
    m_batchCheck = new QCheckBox(tr("批量(目录内全部图片)"), this);
    fmtLay->addRow(tr("格式:"), m_formatCombo);
    fmtLay->addRow(tr("质量:"), m_qualitySpin);
    fmtLay->addRow(m_batchCheck);
    root->addWidget(fmtBox);

    // ---- resize ----
    auto *rsBox = new QGroupBox(tr("缩放"));
    auto *rsLay = new QFormLayout(rsBox);
    m_resizeCombo = new QComboBox(this);
    m_resizeCombo->addItem(tr("无"), "none");
    m_resizeCombo->addItem(tr("适应长边 (px)"), "fit");
    m_resizeCombo->addItem(tr("按比例 (%)"), "scale");
    m_resizeSpin = new QSpinBox(this);
    m_resizeSpin->setRange(1, 100000);
    m_resizeSpin->setValue(1920);
    rsLay->addRow(tr("方式:"), m_resizeCombo);
    rsLay->addRow(tr("数值:"), m_resizeSpin);
    root->addWidget(rsBox);

    // ---- watermark ----
    auto *wmBox = new QGroupBox(tr("水印"));
    auto *wmLay = new QFormLayout(wmBox);
    m_watermarkEdit = new QLineEdit(this);
    m_wmPosCombo = new QComboBox(this);
    m_wmPosCombo->addItem(tr("左上"));
    m_wmPosCombo->addItem(tr("右上"));
    m_wmPosCombo->addItem(tr("左下"));
    m_wmPosCombo->addItem(tr("右下"));
    m_wmPosCombo->addItem(tr("居中"));
    m_wmPosCombo->addItem(tr("平铺"));
    m_wmOpacitySpin = new QSpinBox(this);
    m_wmOpacitySpin->setRange(0, 100);
    m_wmOpacitySpin->setValue(40);
    wmLay->addRow(tr("文字:"), m_watermarkEdit);
    wmLay->addRow(tr("位置:"), m_wmPosCombo);
    wmLay->addRow(tr("不透明度(%):"), m_wmOpacitySpin);
    root->addWidget(wmBox);

    // ---- crop (P0 #⑦) ----
    auto *cropBox = new QGroupBox(tr("裁剪"));
    auto *cropLay = new QFormLayout(cropBox);
    m_cropCheck = new QCheckBox(tr("启用裁剪"), this);
    m_cropX = new QSpinBox(this);
    m_cropY = new QSpinBox(this);
    m_cropW = new QSpinBox(this);
    m_cropH = new QSpinBox(this);
    m_cropX->setRange(0, 100000);
    m_cropY->setRange(0, 100000);
    m_cropW->setRange(1, 100000);
    m_cropH->setRange(1, 100000);
    auto *cropGrid = new QGridLayout();
    cropGrid->addWidget(new QLabel(tr("X")), 0, 0);
    cropGrid->addWidget(m_cropX, 0, 1);
    cropGrid->addWidget(new QLabel(tr("Y")), 0, 2);
    cropGrid->addWidget(m_cropY, 0, 3);
    cropGrid->addWidget(new QLabel(tr("宽")), 1, 0);
    cropGrid->addWidget(m_cropW, 1, 1);
    cropGrid->addWidget(new QLabel(tr("高")), 1, 2);
    cropGrid->addWidget(m_cropH, 1, 3);
    cropLay->addRow(m_cropCheck);
    cropLay->addRow(cropGrid);
    root->addWidget(cropBox);

    // ---- metadata (P0 #⑦) ----
    auto *metaBox = new QGroupBox(tr("元数据"));
    auto *metaLay = new QFormLayout(metaBox);
    m_stripMetaCheck = new QCheckBox(tr("剥离元数据 (EXIF/ICC)"), this);
    m_stripMetaCheck->setChecked(true);
    m_stripMetaCheck->setToolTip(
        tr("重新编码为原始像素时已默认不包含元数据；此选项记录你的明确意图。"));
    metaLay->addRow(m_stripMetaCheck);
    root->addWidget(metaBox);

    // ---- rename ----
    auto *rnBox = new QGroupBox(tr("批量重命名 (留空=原名)"));
    auto *rnLay = new QFormLayout(rnBox);
    m_renameEdit = new QLineEdit(this);
    m_renameEdit->setPlaceholderText("{name}_{seq:3}");
    rnLay->addRow(tr("模式:"), m_renameEdit);
    rnLay->addRow(new QLabel(tr("可用: {name} {ext} {n} {total} {seq:W}")));
    root->addWidget(rnBox);

    // ---- contact sheet / pdf ----
    auto *csBox = new QGroupBox(tr("联系表 / PDF 选项"));
    auto *csLay = new QFormLayout(csBox);
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 20);
    m_colsSpin->setValue(4);
    m_thumbSpin = new QSpinBox(this);
    m_thumbSpin->setRange(16, 2000);
    m_thumbSpin->setValue(200);
    csLay->addRow(tr("列数:"), m_colsSpin);
    csLay->addRow(tr("缩略图边长(px):"), m_thumbSpin);
    root->addWidget(csBox);

    // ---- buttons ----
    auto *box = new QDialogButtonBox(this);
    m_exportBtn = box->addButton(tr("导出"), QDialogButtonBox::AcceptRole);
    box->addButton(QDialogButtonBox::Cancel);
    root->addWidget(box);

    m_statusLabel = new QLabel(tr("就绪"), this);
    root->addWidget(m_statusLabel);

    connect(m_browseBtn, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::onExportClicked);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QStringList ExportDialog::collectSources() const
{
    if (!m_sources.isEmpty())
        return m_sources;
    if (!m_path.isEmpty())
        return {m_path};
    return {};
}

ImageData ExportDialog::applyResize(const ImageData &d) const
{
    const QString mode = m_resizeCombo->currentData().toString();
    if (mode == "fit")
        return mviewer::core::resizeToFit(d, m_resizeSpin->value(), m_resizeSpin->value());
    if (mode == "scale")
        return mviewer::core::resizeByFactor(d, m_resizeSpin->value() / 100.0);
    return d;
}

ImageData ExportDialog::applyWatermark(const ImageData &d) const
{
    const QString t = m_watermarkEdit->text();
    if (t.isEmpty())
        return d;
    const auto pos = static_cast<mviewer::core::WatermarkPosition>(m_wmPosCombo->currentIndex());
    return mviewer::core::addTextWatermark(d, t.toStdString(), pos,
                                           m_wmOpacitySpin->value() / 100.0, 32);
}

void ExportDialog::onBrowse()
{
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("选择输出目录"), m_dirEdit->text());
    if (!dir.isEmpty())
    {
        m_outDir = dir;
        m_dirEdit->setText(dir);
    }
}

void ExportDialog::onExportClicked()
{
    const QString mode = m_modeCombo->currentData().toString();
    if (mode == "clipboard")
    {
        exportUnifiedMode(mviewer::exportjob::Mode::Clipboard);
        return;
    }

    QString outDir = m_dirEdit->text();
    if (outDir.isEmpty() || !QDir(outDir).exists())
    {
        onBrowse();
        outDir = m_dirEdit->text();
    }
    if (outDir.isEmpty() || !QDir(outDir).exists())
    {
        QMessageBox::warning(this, tr("导出"), tr("请先选择有效的输出目录。"));
        return;
    }

    const QDir validatedDir(outDir);
    m_outDir = validatedDir.canonicalPath();
    if (m_outDir.isEmpty())
        m_outDir = validatedDir.absolutePath();
    m_dirEdit->setText(m_outDir);

    if (mode == "contact")
        exportUnifiedMode(mviewer::exportjob::Mode::ContactSheet);
    else if (mode == "pdf")
        exportUnifiedMode(mviewer::exportjob::Mode::Pdf);
    else if (mode == "csv")
        exportUnifiedMode(mviewer::exportjob::Mode::Csv);
    else if (mode == "json")
        exportUnifiedMode(mviewer::exportjob::Mode::Json);
    else if (mode == "html")
        exportUnifiedMode(mviewer::exportjob::Mode::HtmlReport);
    else
        exportConvertBatch();
}

void ExportDialog::exportConvertBatch()
{
    // M21: route Convert through the unified ExportJob runner.
    const QStringList files = collectSources();
    if (files.isEmpty() && m_outDir.isEmpty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }

    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mviewer::exportjob::Mode::Convert;
    cfg.outDir = m_outDir.toStdString();
    if (files.isEmpty())
        cfg.sourceDirectory = m_outDir.toStdString();
    cfg.format = m_formatCombo->currentData().toString().toStdString();
    cfg.quality = m_qualitySpin->value();
    cfg.renamePattern = m_renameEdit->text().toStdString();
    cfg.watermarkText = m_watermarkEdit->text().toStdString();
    cfg.watermarkPos = m_wmPosCombo->currentIndex();
    cfg.watermarkOpacity = m_wmOpacitySpin->value();
    // P0 #⑦: crop + strip metadata
    cfg.cropEnabled = m_cropCheck->isChecked();
    cfg.cropX = m_cropX->value();
    cfg.cropY = m_cropY->value();
    cfg.cropW = m_cropW->value();
    cfg.cropH = m_cropH->value();
    cfg.stripMetadata = m_stripMetaCheck->isChecked();
    const QString resizeMode = m_resizeCombo->currentData().toString();
    if (resizeMode == "fit")
    {
        cfg.resizeMode = mviewer::exportjob::ResizeMode::Fit;
        cfg.resizeValue = m_resizeSpin->value();
    }
    else if (resizeMode == "scale")
    {
        cfg.resizeMode = mviewer::exportjob::ResizeMode::Scale;
        cfg.resizeValue = m_resizeSpin->value();
    }
    for (const QString &f : files)
    {
        const QString src = m_sources.isEmpty() ? (m_outDir + "/" + f) : f;
        cfg.sources.push_back(src.toStdString());
    }

    // M24 (D#3/D#7): run the batch off the UI thread with progress + cancel.
    // The dialog may be closed mid-run; every UI touch is QPointer-guarded and
    // marshaled through qApp. The cancel token bounds the worker's lifetime.
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    cfg.cancel = m_cancelFlag;
    m_exportBtn->setEnabled(false);
    if (!m_progress)
    {
        m_progress = new QProgressDialog(tr("正在导出..."), tr("取消"), 0, 0, this);
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setAutoClose(true);
        m_progress->setMinimumDuration(0);
        connect(m_progress, &QProgressDialog::canceled, this,
                [this]()
                {
                    if (m_cancelFlag)
                        m_cancelFlag->store(true, std::memory_order_relaxed);
                });
    }
    m_progress->setRange(0, static_cast<int>(cfg.sources.size()));
    m_progress->setValue(0);
    m_progress->show();

    const QPointer<ExportDialog> self(this);
    auto *watcher = new QFutureWatcher<mviewer::exportjob::ExportJobResult>(this);
    connect(watcher, &QFutureWatcher<mviewer::exportjob::ExportJobResult>::finished, this,
            [this, watcher]()
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (m_progress)
                    m_progress->close();
                m_exportBtn->setEnabled(true);
                m_statusLabel->setText(QString::fromStdString(result.message));
            });
    watcher->setFuture(QtConcurrent::run(
        [cfg, self]() -> mviewer::exportjob::ExportJobResult
        {
            return mviewer::exportjob::run(
                cfg,
                [self](int done, int total, const std::string &)
                {
                    if (!qApp)
                        return;
                    QMetaObject::invokeMethod(
                        qApp,
                        [self, done, total]()
                        {
                            if (!self || !self->m_progress)
                                return;
                            self->m_progress->setMaximum(total);
                            self->m_progress->setValue(done);
                        });
                });
        }));
}

void ExportDialog::exportUnifiedMode(mviewer::exportjob::Mode mode)
{
    const QStringList files = collectSources();
    if (files.isEmpty() && m_outDir.isEmpty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }

    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mode;
    cfg.outDir = m_outDir.toStdString();
    if (files.isEmpty())
        cfg.sourceDirectory = m_outDir.toStdString();
    cfg.quality = m_qualitySpin->value();
    cfg.contactCols = m_colsSpin->value();
    cfg.contactThumb = m_thumbSpin->value();
    for (const QString &f : files)
    {
        const QString source = m_sources.isEmpty() ? QDir(m_outDir).filePath(f) : f;
        cfg.sources.push_back(source.toStdString());
    }
    startExportJob(std::move(cfg));
}

void ExportDialog::startExportJob(mviewer::exportjob::ExportJobConfig cfg)
{
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    cfg.cancel = m_cancelFlag;
    m_exportBtn->setEnabled(false);
    if (!m_progress)
    {
        m_progress = new QProgressDialog(tr("正在导出..."), tr("取消"), 0, 0, this);
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setAutoClose(true);
        m_progress->setMinimumDuration(0);
        connect(m_progress, &QProgressDialog::canceled, this,
                [this]()
                {
                    if (m_cancelFlag)
                        m_cancelFlag->store(true, std::memory_order_relaxed);
                });
    }
    m_progress->setRange(0, static_cast<int>(cfg.sources.size()));
    m_progress->setValue(0);
    m_progress->show();

    const QPointer<ExportDialog> self(this);
    auto *watcher = new QFutureWatcher<mviewer::exportjob::ExportJobResult>(this);
    connect(watcher, &QFutureWatcher<mviewer::exportjob::ExportJobResult>::finished, this,
            [this, watcher, mode = cfg.mode]()
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (m_progress)
                    m_progress->close();
                m_exportBtn->setEnabled(true);
                if (mode == mviewer::exportjob::Mode::Clipboard &&
                    !result.clipboardImage.isNull())
                {
                    QImage clipboard = mvcore::toQImageRef(result.clipboardImage);
                    if (clipboard.isNull())
                        clipboard = mvcore::toQImage(result.clipboardImage);
                    if (!clipboard.isNull())
                        QApplication::clipboard()->setImage(clipboard);
                }
                m_statusLabel->setText(QString::fromStdString(result.message));
            });
    watcher->setFuture(QtConcurrent::run(
        [cfg, self]() -> mviewer::exportjob::ExportJobResult
        {
            return mviewer::exportjob::run(
                cfg,
                [self](int done, int total, const std::string &)
                {
                    if (!qApp)
                        return;
                    QMetaObject::invokeMethod(
                        qApp,
                        [self, done, total]()
                        {
                            if (!self || !self->m_progress)
                                return;
                            self->m_progress->setMaximum(total);
                            self->m_progress->setValue(done);
                        });
                });
        }));
}

void ExportDialog::exportContactSheet()
{
    exportUnifiedMode(mviewer::exportjob::Mode::ContactSheet);
}

void ExportDialog::exportPdf()
{
    exportUnifiedMode(mviewer::exportjob::Mode::Pdf);
}

void ExportDialog::exportCsv()
{
    exportUnifiedMode(mviewer::exportjob::Mode::Csv);
}

void ExportDialog::exportJson()
{
    exportUnifiedMode(mviewer::exportjob::Mode::Json);
}

void ExportDialog::exportHtmlReport()
{
    exportUnifiedMode(mviewer::exportjob::Mode::HtmlReport);
}

void ExportDialog::exportClipboard()
{
    exportUnifiedMode(mviewer::exportjob::Mode::Clipboard);
}
