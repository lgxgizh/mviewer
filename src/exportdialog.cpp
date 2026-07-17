#include "exportdialog.h"

#include "core/image/Encoder.h"
#include "core/image/QtConvert.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

ExportDialog::ExportDialog(const QStringList &sourceImages, QWidget *parent)
    : QDialog(parent), m_sourceImages(sourceImages)
{
    setupUi();
    setWindowTitle(tr("导出图片"));
    setMinimumWidth(480);
}

void ExportDialog::setupUi()
{
    auto *mainLay = new QVBoxLayout(this);

    // 源文件信息
    mainLay->addWidget(new QLabel(tr("源文件: %1 张图片").arg(m_sourceImages.size())));

    // 批量选项
    m_batchCheck = new QCheckBox(tr("批量导出到目录"));
    m_batchCheck->setChecked(m_sourceImages.size() > 1);
    mainLay->addWidget(m_batchCheck);

    // 输出路径
    auto *outLay = new QHBoxLayout;
    outLay->addWidget(new QLabel(tr("输出:")));
    m_outputEdit = new QLineEdit;
    m_outputEdit->setPlaceholderText(tr("选择文件或目录"));
    outLay->addWidget(m_outputEdit, 1);
    auto *browseBtn = new QPushButton(tr("浏览..."));
    outLay->addWidget(browseBtn);
    mainLay->addLayout(outLay);

    // 格式
    auto *fmtLay = new QHBoxLayout;
    fmtLay->addWidget(new QLabel(tr("格式:")));
    m_formatCombo = new QComboBox;
    m_formatCombo->addItem("PNG", "png");
    m_formatCombo->addItem("JPEG", "jpg");
    m_formatCombo->addItem("BMP", "bmp");
    m_formatCombo->addItem("WebP", "webp");
    fmtLay->addWidget(m_formatCombo);
    fmtLay->addStretch(1);
    mainLay->addLayout(fmtLay);

    // 质量
    auto *qualLay = new QHBoxLayout;
    qualLay->addWidget(new QLabel(tr("质量:")));
    m_qualitySpin = new QSpinBox;
    m_qualitySpin->setRange(1, 100);
    m_qualitySpin->setValue(90);
    qualLay->addWidget(m_qualitySpin);
    qualLay->addStretch(1);
    mainLay->addLayout(qualLay);

    // 进度条
    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    mainLay->addWidget(m_progress);

    m_statusLabel = new QLabel;
    mainLay->addWidget(m_statusLabel);

    // 按钮
    auto *btnLay = new QHBoxLayout;
    btnLay->addStretch(1);
    m_exportBtn = new QPushButton(tr("导出"));
    auto *cancelBtn = new QPushButton(tr("取消"));
    btnLay->addWidget(m_exportBtn);
    btnLay->addWidget(cancelBtn);
    mainLay->addLayout(btnLay);

    // 信号
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::onBrowseOutput);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ExportDialog::onFormatChanged);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::onExportClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    onFormatChanged(0);
}

void ExportDialog::onBrowseOutput()
{
    if (m_batchCheck->isChecked())
    {
        QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出目录"));
        if (!dir.isEmpty())
            m_outputEdit->setText(dir);
    }
    else
    {
        QString filter = tr("图像文件 (*.png *.jpg *.bmp *.webp)");
        QString path = QFileDialog::getSaveFileName(this, tr("保存为"), QString(), filter);
        if (!path.isEmpty())
            m_outputEdit->setText(path);
    }
}

void ExportDialog::onFormatChanged(int index)
{
    QString fmt = m_formatCombo->itemData(index).toString();
    m_qualitySpin->setEnabled(fmt == "jpg" || fmt == "webp");
}

void ExportDialog::onExportClicked()
{
    if (m_outputEdit->text().isEmpty())
    {
        QMessageBox::warning(this, tr("警告"), tr("请选择输出路径"));
        return;
    }
    if (m_batchCheck->isChecked())
    {
        exportBatch();
    }
    else
    {
        if (m_sourceImages.isEmpty())
            return;
        exportSingle(m_sourceImages.first(), m_outputEdit->text());
    }
}

void ExportDialog::exportSingle(const QString &src, const QString &dst)
{
    // 加载图片
    QImage img(src);
    if (img.isNull())
    {
        QMessageBox::critical(this, tr("错误"), tr("无法加载图片: %1").arg(src));
        return;
    }
    img = img.convertToFormat(QImage::Format_RGB32);

    Encoder::Params params;
    params.quality = m_qualitySpin->value();

    ImageData data = mvcore::fromQImage(img);
    if (Encoder::encode(data, dst.toStdString(), params))
    {
        m_statusLabel->setText(tr("导出成功: %1").arg(dst));
        accept();
    }
    else
    {
        QMessageBox::critical(this, tr("错误"), tr("导出失败"));
    }
}

void ExportDialog::exportBatch()
{
    QString outDir = m_outputEdit->text();
    QDir dir(outDir);
    if (!dir.exists())
        dir.mkpath(outDir);

    m_progress->setVisible(true);
    m_progress->setRange(0, m_sourceImages.size());
    m_progress->setValue(0);

    Encoder::Params params;
    params.quality = m_qualitySpin->value();
    QString fmt = m_formatCombo->itemData(m_formatCombo->currentIndex()).toString();
    QString suffix = "." + fmt;

    int success = 0;
    int fail = 0;
    for (int i = 0; i < m_sourceImages.size(); ++i)
    {
        const QString &src = m_sourceImages[i];
        QFileInfo fi(src);
        QString dst = dir.absoluteFilePath(fi.baseName() + suffix);

        QImage img(src);
        if (!img.isNull())
        {
            // Use Encoder (core API), not QImage::save
            ImageData data = mvcore::fromQImage(img.convertToFormat(QImage::Format_RGB32));
            if (Encoder::encode(data, dst.toStdString(), params))
            {
                ++success;
            }
            else
            {
                ++fail;
            }
        }
        else
        {
            ++fail;
        }
        m_progress->setValue(i + 1);
        m_statusLabel->setText(tr("Processing... %1/%2").arg(i + 1).arg(m_sourceImages.size()));
    }

    m_statusLabel->setText(tr("Done: success %1, fail %2").arg(success).arg(fail));
    if (fail == 0)
        accept();
}
