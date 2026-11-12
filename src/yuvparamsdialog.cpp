#include "yuvparamsdialog.h"

#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvSource.h"
#include "yuvparamsettings.h"
#include "yuvpixfmtpicker.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cstdint>
#include <optional>

namespace
{

QString yuvParamsTitle()
{
    return QCoreApplication::translate("YuvParamsDialog", "YUV Parameters");
}

void selectMatrix(QComboBox *box, mviewer::core::yuv::Matrix matrix)
{
    const int wanted = static_cast<int>(matrix);
    const int index = box->findData(wanted);
    if (index >= 0)
    {
        box->setCurrentIndex(index);
    }
}

void selectChromaUpsample(QComboBox *box)
{
    const auto up = mviewer::ui::loadYuvChromaUpsampleSetting();
    const int index = up == mviewer::core::yuv::ChromaUpsample::Nearest ? 1 : 0;
    box->setCurrentIndex(index);
}

} // namespace

YuvParamsDialog::YuvParamsDialog(const QString &fileName, int64_t fileSize,
                                 const mviewer::core::yuv::Format &initial,
                                 const std::vector<std::pair<int, int>> &sizeCandidates,
                                 bool hasSaved, QWidget *parent)
    : QDialog(parent), m_initial(initial), m_fileSize(fileSize)
{
    setWindowTitle(tr("YUV Parameters - %1").arg(fileName));
    auto *form = new QFormLayout;
    addFields(form, sizeCandidates, hasSaved);
    wireSignals();
    setLayout(form);
    updateSummary();
}

void YuvParamsDialog::addFields(QFormLayout *form,
                                const std::vector<std::pair<int, int>> &sizeCandidates,
                                bool hasSaved)
{
    if (!sizeCandidates.empty())
    {
        m_preset = new QComboBox(this);
        m_preset->addItem(tr("Custom"));
        for (const std::pair<int, int> &candidate : sizeCandidates)
        {
            m_preset->addItem(QString::number(candidate.first) + QLatin1Char('x') +
                              QString::number(candidate.second));
        }
        form->addRow(tr("Size preset:"), m_preset);
    }

    m_width = new QSpinBox(this);
    m_height = new QSpinBox(this);
    m_width->setRange(2, 16384);
    m_height->setRange(2, 16384);
    m_width->setValue(m_initial.width);
    m_height->setValue(m_initial.height);
    form->addRow(tr("Width:"), m_width);
    form->addRow(tr("Height:"), m_height);

    auto *pixGroup = new QGroupBox(tr("Pixel format"), this);
    auto *pixLayout = new QVBoxLayout(pixGroup);
    m_picker = new YuvPixFmtPicker(m_initial, pixGroup);
    pixLayout->addWidget(m_picker);
    form->addRow(pixGroup);

    m_matrix = new QComboBox(this);
    m_matrix->addItem(tr("BT.601"), static_cast<int>(mviewer::core::yuv::Matrix::BT601));
    m_matrix->addItem(tr("BT.709"), static_cast<int>(mviewer::core::yuv::Matrix::BT709));
    m_matrix->addItem(tr("BT.2020"), static_cast<int>(mviewer::core::yuv::Matrix::BT2020));
    selectMatrix(m_matrix, m_initial.matrix);
    form->addRow(tr("Matrix:"), m_matrix);

    m_chroma = new QComboBox(this);
    m_chroma->addItem(tr("Bilinear"));
    m_chroma->addItem(tr("Nearest (sample-accurate)"));
    selectChromaUpsample(m_chroma);
    form->addRow(tr("Chroma upsampling:"), m_chroma);

    m_range = new QComboBox(this);
    m_range->addItem(tr("Limited (TV)"));
    m_range->addItem(tr("Full (PC)"));
    m_range->setCurrentIndex(m_initial.range == mviewer::core::yuv::Range::Full ? 1 : 0);
    form->addRow(tr("Range:"), m_range);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    form->addRow(m_summary);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &YuvParamsDialog::acceptWithChroma);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (hasSaved)
    {
        QPushButton *forget = m_buttons->addButton(tr("Forget Saved"), QDialogButtonBox::ResetRole);
        connect(forget, &QPushButton::clicked, this, &YuvParamsDialog::forgetSaved);
    }
    form->addRow(m_buttons);
}

void YuvParamsDialog::wireSignals()
{
    if (m_preset)
    {
        connect(m_preset, &QComboBox::currentIndexChanged, this, &YuvParamsDialog::applySizePreset);
    }
    connect(m_width, &QSpinBox::valueChanged, this, &YuvParamsDialog::updateSummary);
    connect(m_height, &QSpinBox::valueChanged, this, &YuvParamsDialog::updateSummary);
    connect(m_picker, &YuvPixFmtPicker::changed, this, &YuvParamsDialog::updateSummary);
    connect(m_matrix, &QComboBox::currentIndexChanged, this, &YuvParamsDialog::updateSummary);
    connect(m_range, &QComboBox::currentIndexChanged, this, &YuvParamsDialog::updateSummary);
    connect(m_chroma, &QComboBox::currentIndexChanged, this, &YuvParamsDialog::applyChromaUpsample);
}

void YuvParamsDialog::applyChromaUpsample()
{
    if (m_chroma == nullptr)
    {
        return;
    }
    using mviewer::core::yuv::ChromaUpsample;
    const ChromaUpsample up =
        m_chroma->currentIndex() == 1 ? ChromaUpsample::Nearest : ChromaUpsample::Bilinear;
    mviewer::ui::saveYuvChromaUpsampleSetting(up);
}

void YuvParamsDialog::acceptWithChroma()
{
    applyChromaUpsample();
    accept();
}

mviewer::core::yuv::Format YuvParamsDialog::format() const
{
    mviewer::core::yuv::Format out = m_picker->apply(m_initial);
    out.width = m_width->value();
    out.height = m_height->value();
    const int matrixValue = m_matrix->currentData().toInt();
    out.matrix = static_cast<mviewer::core::yuv::Matrix>(static_cast<std::uint8_t>(matrixValue));
    out.range = m_range->currentIndex() == 1 ? mviewer::core::yuv::Range::Full
                                             : mviewer::core::yuv::Range::Limited;
    return out;
}

bool YuvParamsDialog::resetRequested() const
{
    return m_reset;
}

bool YuvParamsDialog::canAccept() const
{
    const mviewer::core::yuv::Format current = format();
    if (!mviewer::core::yuv::isValid(current))
    {
        return false;
    }
    const auto layout = mviewer::core::yuv::computeFrameLayout(current);
    if (!layout)
    {
        return false;
    }
    const mviewer::core::yuv::FrameCount count =
        mviewer::core::yuv::computeFrameCount(m_fileSize, 0, 0, layout->frameBytes);
    return count.frames >= 1;
}

QString YuvParamsDialog::summaryText() const
{
    const mviewer::core::yuv::Format current = format();
    std::string why;
    const auto layout = mviewer::core::yuv::computeFrameLayout(current, &why);
    if (!layout)
    {
        return tr("Invalid parameters: %1").arg(QString::fromStdString(why));
    }
    const mviewer::core::yuv::FrameCount count =
        mviewer::core::yuv::computeFrameCount(m_fileSize, 0, 0, layout->frameBytes);
    QString text = tr("Frame: %1 bytes, frames: %2, trailing: %3 bytes")
                       .arg(QString::number(static_cast<qlonglong>(layout->frameBytes)),
                            QString::number(static_cast<qlonglong>(count.frames)),
                            QString::number(static_cast<qlonglong>(count.trailingBytes)));
    if (count.frames == 0)
    {
        text += QLatin1Char(' ');
        text += tr("The file is smaller than one frame.");
    }
    return text;
}

void YuvParamsDialog::setFrameSize(int w, int h)
{
    m_width->setValue(w);
    m_height->setValue(h);
}

void YuvParamsDialog::setPixFmt(const QString &name)
{
    mviewer::core::yuv::Format f = format();
    if (!mviewer::core::yuv::applyPixFmtName(name.trimmed().toStdString(), f))
    {
        return;
    }
    if (m_picker->setFormat(f))
    {
        updateSummary();
    }
}

void YuvParamsDialog::updateSummary()
{
    m_summary->setText(summaryText());
    QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok);
    if (ok)
    {
        ok->setEnabled(canAccept());
    }
}

void YuvParamsDialog::applySizePreset(int index)
{
    if (!m_preset || index <= 0)
    {
        return;
    }
    const QString text = m_preset->itemText(index);
    const int split = text.indexOf(QLatin1Char('x'));
    if (split <= 0)
    {
        return;
    }
    bool widthOk = false;
    bool heightOk = false;
    const int width = text.left(split).toInt(&widthOk);
    const int height = text.mid(split + 1).toInt(&heightOk);
    if (!widthOk || !heightOk)
    {
        return;
    }
    setFrameSize(width, height);
}

void YuvParamsDialog::forgetSaved()
{
    m_reset = true;
    accept();
}

bool editYuvParamsForPath(QWidget *parent, const QString &path)
{
    mviewer::core::yuv::OpenedYuvFile opened;
    if (!mviewer::core::yuv::openYuvFile(path.toStdString(), opened))
    {
        QMessageBox::warning(
            parent, yuvParamsTitle(),
            QCoreApplication::translate("YuvParamsDialog", "Cannot open %1.").arg(path));
        return false;
    }
    if (opened.info.isY4m)
    {
        QMessageBox::information(
            parent, yuvParamsTitle(),
            QCoreApplication::translate("YuvParamsDialog",
                                        "Y4M files carry their parameters in the file header."));
        return false;
    }

    mviewer::core::yuv::Format initial = opened.info.format;
    const std::string key = mviewer::core::yuv::yuvParamKey(path.toStdString());
    const std::optional<mviewer::core::yuv::Format> saved =
        mviewer::core::yuv::YuvParamStore::instance().get(key);
    if (saved)
    {
        initial = *saved;
    }
    else if (initial.width == 0 || initial.height == 0)
    {
        initial.width = 352;
        initial.height = 288;
    }

    const QString fileName = QFileInfo(path).fileName();
    YuvParamsDialog dialog(fileName.isEmpty() ? path : fileName, opened.fileSize, initial,
                           opened.info.inference.sizeCandidates, saved.has_value(), parent);
    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }
    if (dialog.resetRequested())
    {
        mviewer::ui::clearYuvParam(key);
    }
    else
    {
        mviewer::ui::saveYuvParam(key, dialog.format());
    }
    return true;
}
