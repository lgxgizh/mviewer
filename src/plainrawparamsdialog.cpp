#include "plainrawparamsdialog.h"

#include "core/image/rawplain/PlainRawFile.h"
#include "plainrawparamseditor.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstddef>
#include <cstdint>

namespace
{

using mviewer::core::rawplain::DumpKind;
using mviewer::core::rawplain::Packing;
using mviewer::core::rawplain::Params;
using mviewer::core::rawplain::SizeCheck;

struct DialogSeed
{
    Params params;
    bool sizeTrusted = true;
};

DialogSeed initialForDialog(const mviewer::core::rawplain::PlainRawFileInfo &info, DumpKind kind)
{
    DialogSeed seed;
    seed.params = info.resolved.params;
    if (seed.params.width != 0 && seed.params.height != 0)
    {
        return seed;
    }
    const auto guessed =
        mviewer::core::rawplain::guessCommonSize(seed.params, info.fileSize, kind);
    if (guessed.has_value())
    {
        seed.params = *guessed;
        return seed;
    }
    if (seed.params.width == 0)
    {
        seed.params.width = 640;
    }
    if (seed.params.height == 0)
    {
        seed.params.height = 480;
    }
    seed.sizeTrusted = false;
    return seed;
}

QString bytesText(int64_t value)
{
    return QString::number(static_cast<qlonglong>(value));
}

} // namespace

QString plainRawPackingLabel(Packing packing)
{
    switch (packing)
    {
    case Packing::Bits8:
        return QCoreApplication::translate("PlainRawParamsDialog", "8-bit");
    case Packing::Lsb16:
        return QCoreApplication::translate("PlainRawParamsDialog", "16-bit container, LSB aligned");
    case Packing::Msb16:
        return QCoreApplication::translate("PlainRawParamsDialog", "16-bit container, MSB aligned");
    case Packing::Mipi10:
        return QCoreApplication::translate("PlainRawParamsDialog", "MIPI RAW10 (packed)");
    case Packing::Mipi12:
        return QCoreApplication::translate("PlainRawParamsDialog", "MIPI RAW12 (packed)");
    case Packing::Bits1:
        return QCoreApplication::translate("PlainRawParamsDialog", "1-bit (8 pixels per byte)");
    }
    return {};
}

PlainRawParamsDialog::PlainRawParamsDialog(const QString &fileName, int64_t fileSize,
                                           const Params &initial, bool hasOverride,
                                           QWidget *parent, DumpKind kind)
    : QDialog(parent), m_fileSize(fileSize), m_kind(kind)
{
    if (kind == DumpKind::Gray)
    {
        setWindowTitle(tr("Gray Parameters - %1").arg(fileName));
    }
    else
    {
        setWindowTitle(tr("RAW Parameters - %1").arg(fileName));
    }
    auto *layout = new QVBoxLayout(this);
    QString intro = tr("Plain sensor dump: set how the bytes are stored.");
    if (kind == DumpKind::Gray)
    {
        intro = tr("Headerless grayscale: set how the bytes are stored.");
    }
    layout->addWidget(new QLabel(intro, this));

    m_editor = new PlainRawParamsEditor(PlainRawParamsEditor::Mode::File, this, kind);
    m_editor->setParams(initial);
    layout->addWidget(m_editor);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setObjectName(QStringLiteral("plainRawSummary"));
    layout->addWidget(m_summary);

    auto *group = new QGroupBox(tr("Sizes that match this file"), this);
    auto *groupLayout = new QVBoxLayout(group);
    m_guessList = new QListWidget(group);
    m_guessList->setObjectName(QStringLiteral("plainRawGuesses"));
    groupLayout->addWidget(m_guessList);
    layout->addWidget(group);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QPushButton *useDefaultsButton =
        m_buttons->addButton(tr("Use Defaults"), QDialogButtonBox::ResetRole);
    useDefaultsButton->setEnabled(hasOverride);
    connect(useDefaultsButton, &QPushButton::clicked, this, &PlainRawParamsDialog::useDefaults);
    layout->addWidget(m_buttons);

    connect(m_editor, &PlainRawParamsEditor::sizeEdited, this, [this]() { m_sizeEdited = true; });
    connect(m_editor, &PlainRawParamsEditor::paramsChanged, this,
            &PlainRawParamsDialog::updateSummary);
    connect(m_guessList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { applyGuess(m_guessList->row(item)); });
    updateSummary();
}

Params PlainRawParamsDialog::params() const
{
    return m_editor->params();
}

bool PlainRawParamsDialog::resetRequested() const
{
    return m_reset;
}

bool PlainRawParamsDialog::largerSizeConfirmed() const
{
    return m_sizeTrusted || m_sizeEdited;
}

void PlainRawParamsDialog::setSizeTrusted(bool trusted)
{
    m_sizeTrusted = trusted;
    updateSummary();
}

bool PlainRawParamsDialog::canAccept() const
{
    if (!m_editor->validationError().isEmpty())
    {
        return false;
    }
    const SizeCheck check = mviewer::core::rawplain::checkFileSize(m_editor->params(), m_fileSize);
    if (check == SizeCheck::Exact)
    {
        return true;
    }
    if (check == SizeCheck::Larger)
    {
        return largerSizeConfirmed();
    }
    return false;
}

QString PlainRawParamsDialog::summaryText() const
{
    const QString error = m_editor->validationError();
    if (!error.isEmpty())
    {
        return error;
    }
    const Params current = m_editor->params();
    const SizeCheck check = mviewer::core::rawplain::checkFileSize(current, m_fileSize);
    if (check == SizeCheck::Invalid)
    {
        return error;
    }
    const int64_t expected = mviewer::core::rawplain::frameBytes(current);
    const QString storage = plainRawPackingLabel(current.packing);
    const QString expectedText = bytesText(expected);
    if (check == SizeCheck::Exact)
    {
        return tr("%1 x %2, %3: %4 bytes expected, the file matches.")
            .arg(current.width)
            .arg(current.height)
            .arg(storage)
            .arg(expectedText);
    }
    if (check == SizeCheck::Larger)
    {
        const int64_t extra = m_fileSize - expected;
        QString text =
            tr("%1 x %2, %3: %4 bytes expected, the file has %5 bytes (%6 extra bytes ignored).")
                .arg(current.width)
                .arg(current.height)
                .arg(storage)
                .arg(expectedText)
                .arg(bytesText(m_fileSize))
                .arg(bytesText(extra));
        if (!largerSizeConfirmed())
        {
            text += QStringLiteral(" ");
            text += tr("Check the size, or edit it to confirm.");
        }
        return text;
    }
    return tr("%1 x %2, %3: %4 bytes expected, the file has only %5 bytes. Pick a size below or "
              "fix width, height or storage.")
        .arg(current.width)
        .arg(current.height)
        .arg(storage)
        .arg(expectedText)
        .arg(bytesText(m_fileSize));
}

int PlainRawParamsDialog::guessCount() const
{
    return static_cast<int>(m_guesses.size());
}

void PlainRawParamsDialog::applyGuess(int index)
{
    if (index < 0)
    {
        return;
    }
    const auto position = static_cast<std::size_t>(index);
    if (position >= m_guesses.size())
    {
        return;
    }
    m_editor->setParams(m_guesses[position]);
    updateSummary();
}

void PlainRawParamsDialog::updateSummary()
{
    m_summary->setText(summaryText());
    const Params current = m_editor->params();
    const std::vector<Params> found =
        mviewer::core::rawplain::guessFromFileSize(current, m_fileSize, m_kind);
    m_guesses.clear();
    m_guessList->clear();
    for (const Params &guess : found)
    {
        if (guess == current)
        {
            continue;
        }
        m_guesses.push_back(guess);
        const QString text =
            tr("%1-bit, %2").arg(guess.bitDepth).arg(plainRawPackingLabel(guess.packing));
        m_guessList->addItem(text);
    }
    const bool any = !m_guesses.empty();
    m_guessList->setVisible(any);
    if (QWidget *group = m_guessList->parentWidget())
    {
        group->setVisible(any);
    }
    if (QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok))
    {
        ok->setEnabled(canAccept());
    }
}

void PlainRawParamsDialog::useDefaults()
{
    m_reset = true;
    accept();
}

bool editPlainRawParamsForPath(QWidget *parent, const QString &path)
{
    namespace raw = mviewer::core::rawplain;
    const std::string utf8 = path.toUtf8().toStdString();
    const std::string key = raw::plainRawParamKey(utf8);
    const raw::DumpKind kind = raw::dumpKindForPath(utf8);
    raw::PlainRawFileInfo info = raw::resolvePlainRawFile(utf8);
    if (info.fileSize < 0)
    {
        const QString title =
            kind == raw::DumpKind::Gray
                ? QCoreApplication::translate("PlainRawParamsDialog", "Gray Parameters")
                : QCoreApplication::translate("PlainRawParamsDialog", "RAW Parameters");
        QMessageBox::warning(
            parent, title,
            QCoreApplication::translate("PlainRawParamsDialog", "Cannot open %1.").arg(path));
        return false;
    }
    const bool hasOverride = raw::PlainRawParamStore::instance().override(key).has_value();
    if (!hasOverride && info.resolved.known)
    {
        std::vector<std::uint16_t> scratch;
        raw::readPlainRawSamples(utf8, info, scratch, nullptr);
    }
    const DialogSeed seed = initialForDialog(info, kind);
    const QString fileName = QFileInfo(path).fileName();
    PlainRawParamsDialog dialog(fileName.isEmpty() ? path : fileName, info.fileSize, seed.params,
                                hasOverride, parent, kind);
    dialog.setSizeTrusted(seed.sizeTrusted);
    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }
    if (dialog.resetRequested())
    {
        raw::PlainRawParamStore::instance().clearOverride(key);
    }
    else
    {
        raw::PlainRawParamStore::instance().setOverride(key, dialog.params());
    }
    return true;
}
