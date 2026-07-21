#include "exportdialog.h"

#include "core/image/Encoder.h"
#include "core/image/ImageTransform.h"
#include "core/image/QtConvert.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <vector>

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
    m_browseBtn = new QPushButton(tr("浏览..."), this);
    dirRow->addWidget(new QLabel(tr("输出目录:")));
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    root->addLayout(dirRow);

    // ---- mode ----
    auto *modeBox = new QGroupBox(tr("导出模式"));
    auto *modeLay = new QFormLayout(modeBox);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("转换 / 批量"), "convert");
    m_modeCombo->addItem(tr("联系表 (Contact Sheet)"), "contact");
    m_modeCombo->addItem(tr("PDF 文档"), "pdf");
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
    // Legacy: batch over the output directory.
    if (!m_outDir.isEmpty())
        return QDir(m_outDir).entryList({"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp"},
                                        QDir::Files, QDir::Name);
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
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出目录"),
                                                          m_dirEdit->text());
    if (!dir.isEmpty())
    {
        m_outDir = dir;
        m_dirEdit->setText(dir);
    }
}

void ExportDialog::onExportClicked()
{
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

    const QString mode = m_modeCombo->currentData().toString();
    if (mode == "contact")
        exportContactSheet();
    else if (mode == "pdf")
        exportPdf();
    else
        exportConvertBatch();
}

void ExportDialog::exportConvertBatch()
{
    const QString fmt = m_formatCombo->currentData().toString();
    const int quality = m_qualitySpin->value();
    const QString ext = "." + (fmt == "jpeg" ? QString("jpg") : fmt);

    const QStringList files = collectSources();
    if (files.isEmpty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }

    QDir out(m_outDir);
    const int total = files.size();
    int done = 0;
    for (int i = 0; i < files.size(); ++i)
    {
        const QString src = m_sources.isEmpty() ? (m_outDir + "/" + files[i]) : files[i];
        QImage img(src);
        if (img.isNull())
            continue;
        ImageData data = applyWatermark(applyResize(mvcore::fromQImage(img)));

        QFileInfo fi(src);
        QString base = QString::fromStdString(mviewer::core::applyRenamePattern(
            m_renameEdit->text().toStdString(), fi.baseName().toStdString(),
            fi.suffix().toStdString(), i, total));
        if (base.isEmpty())
            base = fi.baseName();
        const QString dst = out.absoluteFilePath(base + ext);
        if (Encoder::encode(data, dst.toStdString(), Encoder::Params{quality}))
            ++done;
    }
    m_statusLabel->setText(tr("完成 %1 / %2").arg(done).arg(total));
}

void ExportDialog::exportContactSheet()
{
    const QStringList files = collectSources();
    if (files.isEmpty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }
    std::vector<ImageData> imgs;
    for (const QString &f : files)
    {
        const QString src = m_sources.isEmpty() ? (m_outDir + "/" + f) : f;
        QImage img(src);
        if (!img.isNull())
            imgs.push_back(mvcore::fromQImage(img));
    }
    if (imgs.empty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }
    const ImageData sheet =
        mviewer::core::makeContactSheet(imgs, m_colsSpin->value(), m_thumbSpin->value());
    const QString dst = QDir(m_outDir).absoluteFilePath("contact_sheet.png");
    if (Encoder::encode(sheet, dst.toStdString(), Encoder::Params{100}))
        m_statusLabel->setText(tr("联系表已生成: %1").arg(dst));
    else
        m_statusLabel->setText(tr("联系表生成失败。"));
}

void ExportDialog::exportPdf()
{
    const QStringList files = collectSources();
    if (files.isEmpty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }
    std::vector<ImageData> imgs;
    for (const QString &f : files)
    {
        const QString src = m_sources.isEmpty() ? (m_outDir + "/" + f) : f;
        QImage img(src);
        if (!img.isNull())
            imgs.push_back(mvcore::fromQImage(img));
    }
    if (imgs.empty())
    {
        m_statusLabel->setText(tr("没有可导出的图片。"));
        return;
    }
    const QString dst = QDir(m_outDir).absoluteFilePath("export.pdf");
    if (mviewer::core::writePdf(dst.toStdString(), imgs, m_qualitySpin->value()))
        m_statusLabel->setText(tr("PDF 已生成: %1").arg(dst));
    else
        m_statusLabel->setText(tr("PDF 生成失败。"));
}
