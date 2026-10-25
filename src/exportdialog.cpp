#include "exportdialog.h"
#include "batchrenamepanel.h"
#include "exportmessages.h"

#include "core/export/ExportJob.h"
#include "core/image/ImageTransform.h"
#include "core/image/QtConvert.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
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
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

void ExportDialog::setOutputDir(const QString &dir)
{
    m_outDir = dir;
    if (m_dirEdit)
        m_dirEdit->setText(dir);
}

void ExportDialog::setSources(const QStringList &paths)
{
    m_sources = paths;
    m_renamePanel->setSourceFiles(paths);
}

ExportDialog::ExportDialog(const QStringList &sources, QWidget *parent) : ExportDialog(parent)
{
    setSources(sources);
    if (!sources.isEmpty())
    {
        const QFileInfo fi(sources.first());
        m_outDir = fi.absolutePath();
        m_dirEdit->setText(m_outDir);
    }
}

ExportDialog::ExportDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Export Images"));
    resize(460, 520);
    auto *root = new QVBoxLayout(this);
    buildOutputSection(root);
    buildModeSection(root);
    buildFormatSection(root);
    buildResizeSection(root);
    buildWatermarkSection(root);
    buildCropSection(root);
    buildMetadataSection(root);
    buildRenameSection(root);
    buildContactSection(root);
    buildButtonSection(root);
}
void ExportDialog::buildOutputSection(QVBoxLayout *root)
{
    // ---- output directory ----
    // Wrapped so clipboard mode (no file) can hide the whole row.
    m_outputRow = new QWidget(this);
    m_outputRow->setObjectName(QStringLiteral("exportOutputDirectoryRow"));
    auto *dirRow = new QHBoxLayout(m_outputRow);
    dirRow->setContentsMargins(0, 0, 0, 0);
    m_dirEdit = new QLineEdit(m_outputRow);
    m_dirEdit->setObjectName(QStringLiteral("exportOutputDirectoryEdit"));
    m_browseBtn = new QPushButton(tr("Browse..."), m_outputRow);
    dirRow->addWidget(new QLabel(tr("Output directory:"), m_outputRow));
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    root->addWidget(m_outputRow);
}

void ExportDialog::buildModeSection(QVBoxLayout *root)
{
    // ---- mode ----
    auto *modeBox = new QGroupBox(tr("Export mode"));
    auto *modeLay = new QFormLayout(modeBox);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("exportModeCombo"));
    m_modeCombo->addItem(tr("Convert / Batch"), "convert");
    m_modeCombo->addItem(tr("Contact Sheet"), "contact");
    m_modeCombo->addItem(tr("PDF Document"), "pdf");
    m_modeCombo->addItem(tr("Image Metadata Manifest CSV"), "csv");
    m_modeCombo->addItem(tr("Image metadata manifest JSON"), "json");
    m_modeCombo->addItem(tr("Image Metadata List HTML"), "html");
    m_modeCombo->addItem(tr("Copy to clipboard"), "clipboard");
    modeLay->addRow(tr("Mode:"), m_modeCombo);
    root->addWidget(modeBox);
}

void ExportDialog::buildFormatSection(QVBoxLayout *root)
{
    // ---- format ----
    m_formatGroup = new QGroupBox(tr("Format"));
    auto *fmtBox = m_formatGroup;
    auto *fmtLay = new QFormLayout(fmtBox);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(tr("PNG"), QStringLiteral("png"));
    m_formatCombo->addItem(tr("JPEG"), QStringLiteral("jpeg"));
    m_formatCombo->addItem(tr("BMP"), QStringLiteral("bmp"));
    m_formatCombo->addItem(tr("WebP"), QStringLiteral("webp"));
    m_formatCombo->addItem(tr("TIFF"), QStringLiteral("tiff"));
    m_formatCombo->setCurrentIndex(1); // JPEG
    m_qualitySpin = new QSpinBox(this);
    m_qualitySpin->setRange(1, 100);
    m_qualitySpin->setValue(90);
    // The old "Batch (all images in folder)" checkbox was never read anywhere, so it could
    // not affect what was exported; removed rather than left as a control that
    // silently does nothing.
    fmtLay->addRow(tr("Format:"), m_formatCombo);
    fmtLay->addRow(tr("Quality:"), m_qualitySpin);
    root->addWidget(fmtBox);
}

void ExportDialog::buildResizeSection(QVBoxLayout *root)
{
    // ---- resize ----
    m_resizeGroup = new QGroupBox(tr("Resize"));
    auto *rsBox = m_resizeGroup;
    auto *rsLay = new QFormLayout(rsBox);
    m_resizeCombo = new QComboBox(this);
    m_resizeCombo->setObjectName(QStringLiteral("exportResizeModeCombo"));
    m_resizeCombo->addItem(tr("None"), "none");
    m_resizeCombo->addItem(tr("Fit Long Edge (px)"), "fit");
    m_resizeCombo->addItem(tr("Proportional (%)"), "scale");
    m_resizeSpin = new QSpinBox(this);
    m_resizeSpin->setObjectName(QStringLiteral("exportResizeValueSpin"));
    m_resizeSpin->setRange(1, 100000);
    m_resizeSpin->setValue(1920);
    rsLay->addRow(tr("Method:"), m_resizeCombo);
    rsLay->addRow(tr("Value:"), m_resizeSpin);
    root->addWidget(rsBox);
    connect(m_resizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ExportDialog::syncResizeControls);
    syncResizeControls();
}

void ExportDialog::syncResizeControls()
{
    const QString mode = m_resizeCombo->currentData().toString();
    const bool wasPercent = m_resizeSpin->suffix().contains(QLatin1Char('%'));
    const int current = m_resizeSpin->value();
    if (mode == "scale")
    {
        // The spin boots at the fit-mode long edge (1920). Leaving that number
        // in place would export at 1920%.
        m_resizeSpin->setRange(1, 800);
        m_resizeSpin->setSuffix(QStringLiteral(" %"));
        if (!wasPercent)
            m_resizeSpin->setValue(current > 100 ? 100 : current);
        m_resizeSpin->setEnabled(true);
        return;
    }
    if (mode == "fit")
    {
        m_resizeSpin->setRange(1, 100000);
        m_resizeSpin->setSuffix(QStringLiteral(" px"));
        if (wasPercent)
            m_resizeSpin->setValue(1920);
        m_resizeSpin->setEnabled(true);
        return;
    }
    m_resizeSpin->setEnabled(false);
}

void ExportDialog::buildWatermarkSection(QVBoxLayout *root)
{
    // ---- watermark ----
    m_watermarkGroup = new QGroupBox(tr("Watermark"));
    auto *wmBox = m_watermarkGroup;
    auto *wmLay = new QFormLayout(wmBox);
    m_watermarkEdit = new QLineEdit(this);
    m_wmPosCombo = new QComboBox(this);
    m_wmPosCombo->addItem(tr("Top Left"));
    m_wmPosCombo->addItem(tr("Top right"));
    m_wmPosCombo->addItem(tr("Bottom Left"));
    m_wmPosCombo->addItem(tr("Bottom Right"));
    m_wmPosCombo->addItem(tr("Center"));
    m_wmPosCombo->addItem(tr("Tile"));
    m_wmOpacitySpin = new QSpinBox(this);
    m_wmOpacitySpin->setRange(0, 100);
    m_wmOpacitySpin->setValue(40);
    wmLay->addRow(tr("Text:"), m_watermarkEdit);
    wmLay->addRow(tr("Position:"), m_wmPosCombo);
    wmLay->addRow(tr("Opacity (%):"), m_wmOpacitySpin);
    root->addWidget(wmBox);
}

void ExportDialog::buildCropSection(QVBoxLayout *root)
{
    // ---- crop (P0 #⑦) ----
    m_cropGroup = new QGroupBox(tr("Crop"));
    auto *cropBox = m_cropGroup;
    auto *cropLay = new QFormLayout(cropBox);
    m_cropCheck = new QCheckBox(tr("Enable Cropping"), this);
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
    cropGrid->addWidget(new QLabel(tr("Width")), 1, 0);
    cropGrid->addWidget(m_cropW, 1, 1);
    cropGrid->addWidget(new QLabel(tr("Height")), 1, 2);
    cropGrid->addWidget(m_cropH, 1, 3);
    cropLay->addRow(m_cropCheck);
    cropLay->addRow(cropGrid);
    root->addWidget(cropBox);
}

void ExportDialog::buildMetadataSection(QVBoxLayout *root)
{
    // ---- metadata (P0 #⑦) ----
    m_metadataGroup = new QGroupBox(tr("Metadata"));
    auto *metaBox = m_metadataGroup;
    auto *metaLay = new QFormLayout(metaBox);
    m_stripMetaCheck = new QCheckBox(tr("Strip Metadata (EXIF/ICC)"), this);
    m_stripMetaCheck->setChecked(true);
    m_stripMetaCheck->setToolTip(tr("When re-encoding to original pixels, metadata is excluded by "
                                    "default; this option records your explicit intent."));
    metaLay->addRow(m_stripMetaCheck);
    root->addWidget(metaBox);
}

void ExportDialog::buildRenameSection(QVBoxLayout *root)
{
    // ---- rename ----
    m_renameGroup = new QGroupBox(tr("Batch Rename (leave blank = original name)"));
    auto *rnBox = m_renameGroup;
    auto *rnLay = new QVBoxLayout(rnBox);
    m_renamePanel = new BatchRenamePanel(rnBox);
    m_renamePanel->setSettingsGroup(QStringLiteral("exportRename"));
    m_renameEdit = m_renamePanel->patternEdit();
    m_renameEdit->setPlaceholderText("{name}_{seq:3}");
    rnLay->addWidget(m_renamePanel);
    rnLay->addWidget(new QLabel(tr("Available: {name} {ext} {n} {total} {seq:W}")));
    root->addWidget(rnBox);
    // The preview shows the encoded extension; the last confirmed settings return.
    const auto syncExtension = [this]()
    {
        const QString format = m_formatCombo->currentData().toString();
        m_renamePanel->setTargetExtension(format == QLatin1String("jpeg") ? QStringLiteral("jpg")
                                                                          : format);
    };
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, syncExtension);
    syncExtension();
    m_renamePanel->loadSettings();
}

void ExportDialog::buildContactSection(QVBoxLayout *root)
{
    // ---- contact sheet / pdf ----
    m_contactGroup = new QGroupBox(tr("Contact sheet / PDF options"));
    auto *csBox = m_contactGroup;
    auto *csLay = new QFormLayout(csBox);
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 20);
    m_colsSpin->setValue(4);
    m_thumbSpin = new QSpinBox(this);
    m_thumbSpin->setRange(16, 2000);
    m_thumbSpin->setValue(200);
    csLay->addRow(tr("Columns:"), m_colsSpin);
    csLay->addRow(tr("Thumbnail side length (px):"), m_thumbSpin);
    root->addWidget(csBox);
}

void ExportDialog::buildButtonSection(QVBoxLayout *root)
{
    // ---- buttons ----
    auto *box = new QDialogButtonBox(this);
    m_exportBtn = box->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    box->addButton(QDialogButtonBox::Cancel);
    root->addWidget(box);

    m_statusLabel = new QLabel(tr("Ready"), this);
    root->addWidget(m_statusLabel);

    connect(m_browseBtn, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::onExportClicked);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_renamePanel, &BatchRenamePanel::validityChanged, this,
            &ExportDialog::refreshExportButton);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &ExportDialog::refreshExportButton);
    refreshExportButton();
}

void ExportDialog::syncModeControls()
{
    if (!m_modeCombo)
        return;
    const QString mode = m_modeCombo->currentData().toString();
    // csv/json/html write an image metadata inventory (name, size, channel
    // means). They do not encode, resize, watermark, crop, rename, or build a
    // contact sheet. Clipboard decodes the first source and never writes a file,
    // so the output directory does not apply either. Convert / contact / PDF
    // keep the controls they already showed; contact and PDF still read quality
    // and the contact-sheet options.
    const bool inventory = mode == QLatin1String("csv") || mode == QLatin1String("json") ||
                           mode == QLatin1String("html");
    const bool clipboard = mode == QLatin1String("clipboard");
    const bool hideImageControls = inventory || clipboard;
    if (m_outputRow)
        m_outputRow->setVisible(!clipboard);
    if (m_formatGroup)
        m_formatGroup->setVisible(!hideImageControls);
    if (m_resizeGroup)
        m_resizeGroup->setVisible(!hideImageControls);
    if (m_watermarkGroup)
        m_watermarkGroup->setVisible(!hideImageControls);
    if (m_cropGroup)
        m_cropGroup->setVisible(!hideImageControls);
    if (m_metadataGroup)
        m_metadataGroup->setVisible(!hideImageControls);
    if (m_renameGroup)
        m_renameGroup->setVisible(!hideImageControls);
    if (m_contactGroup)
        m_contactGroup->setVisible(!hideImageControls);
}

void ExportDialog::refreshExportButton()
{
    syncModeControls();
    const bool convert = m_modeCombo->currentData().toString() == QLatin1String("convert");
    if (!m_cancelFlag)
        m_exportBtn->setEnabled(!convert || m_renamePanel->isValid());
}

void ExportDialog::reject()
{
    cancelActiveExport();
    QDialog::reject();
}

void ExportDialog::closeEvent(QCloseEvent *event)
{
    cancelActiveExport();
    QDialog::closeEvent(event);
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
        QFileDialog::getExistingDirectory(this, tr("Select output directory"), m_dirEdit->text());
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
        QMessageBox::warning(this, tr("Export"),
                             tr("Please select a valid output directory first."));
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
        m_statusLabel->setText(tr("No images available to export."));
        return;
    }

    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mviewer::exportjob::Mode::Convert;
    cfg.outDir = m_outDir.toUtf8().toStdString();
    if (files.isEmpty())
        cfg.sourceDirectory = m_outDir.toUtf8().toStdString();
    cfg.format = m_formatCombo->currentData().toString().toStdString();
    cfg.quality = m_qualitySpin->value();
    if (!m_renamePanel->isValid())
    {
        m_statusLabel->setText(
            tr("Invalid rename settings: %1").arg(m_renamePanel->errorMessage()));
        return;
    }
    const auto rename = m_renamePanel->options();
    cfg.renamePattern = rename.pattern;
    cfg.renameFind = rename.find;
    cfg.renameReplace = rename.replace;
    cfg.renameUseRegex = rename.useRegex;
    cfg.renameCaseSensitive = rename.caseSensitive;
    m_renamePanel->saveSettings(); // remembered only when the export is confirmed
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
        cfg.sources.push_back(src.toUtf8().toStdString());
    }

    startExportJob(std::move(cfg));
    return;
}

void ExportDialog::exportUnifiedMode(mviewer::exportjob::Mode mode)
{
    const QStringList files = collectSources();
    if (files.isEmpty() && m_outDir.isEmpty())
    {
        m_statusLabel->setText(tr("No images available to export."));
        return;
    }

    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mode;
    cfg.outDir = m_outDir.toUtf8().toStdString();
    if (files.isEmpty())
        cfg.sourceDirectory = m_outDir.toUtf8().toStdString();
    cfg.quality = m_qualitySpin->value();
    cfg.contactCols = m_colsSpin->value();
    cfg.contactThumb = m_thumbSpin->value();
    for (const QString &f : files)
    {
        const QString source = m_sources.isEmpty() ? QDir(m_outDir).filePath(f) : f;
        cfg.sources.push_back(source.toUtf8().toStdString());
    }
    startExportJob(std::move(cfg));
}

void ExportDialog::startExportJob(mviewer::exportjob::ExportJobConfig cfg)
{
    cancelActiveExport();
    const uint64_t generation = m_exportGeneration;
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    cfg.cancel = m_cancelFlag;
    m_exportBtn->setEnabled(false);
    if (!m_progress)
    {
        m_progress = new QProgressDialog(tr("Exporting..."), tr("Cancel"), 0, 0, this);
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setAutoClose(true);
        m_progress->setMinimumDuration(0);
        connect(m_progress, &QProgressDialog::canceled, this, [this]() { cancelActiveExport(); });
    }
    m_progress->setRange(0, static_cast<int>(cfg.sources.size()));
    m_progress->setValue(0);
    m_progress->show();

    auto self = std::make_shared<QPointer<ExportDialog>>(this);
    auto *watcher = new QFutureWatcher<mviewer::exportjob::ExportJobResult>(this);
    connect(watcher, &QFutureWatcher<mviewer::exportjob::ExportJobResult>::finished, this,
            [self, watcher, generation, mode = cfg.mode]()
            {
                // Reading the future rethrows anything the worker threw; without
                // this guard the exception escapes into the event loop and the
                // dialog is left mid-completion.
                mviewer::exportjob::ExportJobResult result;
                try
                {
                    result = watcher->result();
                }
                catch (const std::exception &error)
                {
                    watcher->deleteLater();
                    ExportDialog *failed = self->data();
                    if (failed && generation == failed->m_exportGeneration)
                    {
                        if (failed->m_progress)
                            failed->m_progress->close();
                        failed->m_exportBtn->setEnabled(true);
                        failed->m_statusLabel->setText(
                            tr("Export failed: %1").arg(QString::fromUtf8(error.what())));
                    }
                    return;
                }
                catch (...)
                {
                    watcher->deleteLater();
                    ExportDialog *failed = self->data();
                    if (failed && generation == failed->m_exportGeneration)
                    {
                        if (failed->m_progress)
                            failed->m_progress->close();
                        failed->m_exportBtn->setEnabled(true);
                        failed->m_statusLabel->setText(tr("Export failed: Unknown error"));
                    }
                    return;
                }
                watcher->deleteLater();
                ExportDialog *dialog = self->data();
                if (!dialog || generation != dialog->m_exportGeneration)
                    return;
                if (dialog->m_progress)
                    dialog->m_progress->close();
                dialog->m_exportBtn->setEnabled(true);
                if (mode == mviewer::exportjob::Mode::Clipboard && !result.clipboardImage.isNull())
                {
                    // Owning conversion: the clipboard outlives `result`, so the
                    // non-owning toQImageRef() alias must never be handed out.
                    QImage clipboard = mvcore::toQImage(result.clipboardImage);
                    if (!clipboard.isNull())
                        QApplication::clipboard()->setImage(clipboard);
                }
                dialog->m_statusLabel->setText(mviewer::ui::exportResultMessageZh(result));
            });
    watcher->setFuture(QtConcurrent::run(
        [cfg, self, generation]() -> mviewer::exportjob::ExportJobResult
        {
            return mviewer::exportjob::run(
                cfg,
                [self, generation](int done, int total, const std::string &)
                {
                    if (!qApp)
                        return;
                    QMetaObject::invokeMethod(qApp,
                                              [self, done, total, generation]()
                                              {
                                                  ExportDialog *dialog = self->data();
                                                  if (!dialog ||
                                                      generation != dialog->m_exportGeneration ||
                                                      !dialog->m_progress)
                                                      return;
                                                  dialog->m_progress->setMaximum(total);
                                                  dialog->m_progress->setValue(done);
                                              });
                });
        }));
}

void ExportDialog::cancelActiveExport()
{
    ++m_exportGeneration;
    if (m_cancelFlag)
        m_cancelFlag->store(true, std::memory_order_release);
    m_cancelFlag.reset();
    if (m_progress)
        m_progress->close();
    if (m_exportBtn)
        m_exportBtn->setEnabled(true);
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
