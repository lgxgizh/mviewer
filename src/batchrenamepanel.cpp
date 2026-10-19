#include "batchrenamepanel.h"
#include "ThemeMetrics.h"

#include "Theme.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace
{

constexpr int kPreviewLines = 3;

QString previewText(const std::vector<mviewer::core::BatchRenameResult> &results)
{
    QStringList lines;
    for (const auto &result : results)
    {
        if (lines.size() == kPreviewLines)
            break;
        lines << QStringLiteral("%1 → %2").arg(
            QFileInfo(QString::fromStdString(result.originalPath)).fileName(),
            QString::fromStdString(result.newName));
    }
    QString text = BatchRenamePanel::tr("Preview: ") + lines.join(QStringLiteral("\n      "));
    if (results.size() > static_cast<size_t>(kPreviewLines))
        text += BatchRenamePanel::tr("\n      … (%1 files total)").arg(results.size());
    return text;
}

} // namespace

BatchRenamePanel::BatchRenamePanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("batchRenamePanel"));
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(mviewer::ui::metrics::kSpace8);

    auto *patternRow = new QHBoxLayout;
    patternRow->addWidget(new QLabel(tr("Rename pattern:")));
    m_patternEdit = new QLineEdit;
    m_patternEdit->setObjectName(QStringLiteral("batchRenamePattern"));
    m_patternEdit->setPlaceholderText(QStringLiteral("{name}_batched_{seq:3}"));
    m_patternEdit->setToolTip(tr("Available: {name} {ext} {n} {total} {seq:W};"
                                 "{name} 是查找替换之后的文件名；留空=保持文件名"));
    patternRow->addWidget(m_patternEdit, 1);
    mainLay->addLayout(patternRow);

    buildFindReplaceRows();

    m_errorLabel = new QLabel;
    m_errorLabel->setObjectName(QStringLiteral("batchRenameError"));
    m_errorLabel->setStyleSheet(
        QStringLiteral("color:%1; font-weight: bold;")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Danger)));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    mainLay->addWidget(m_errorLabel);

    m_previewLabel = new QLabel;
    m_previewLabel->setObjectName(QStringLiteral("batchRenamePreview"));
    m_previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLay->addWidget(m_previewLabel);

    for (QLineEdit *edit : {m_patternEdit, m_findEdit, m_replaceEdit})
        connect(edit, &QLineEdit::textChanged, this, &BatchRenamePanel::updatePreview);
    for (QCheckBox *check : {m_useRegexCheck, m_caseSensitiveCheck})
        connect(check, &QCheckBox::toggled, this, &BatchRenamePanel::updatePreview);
    updatePreview();
}

void BatchRenamePanel::buildFindReplaceRows()
{
    auto *mainLay = qobject_cast<QVBoxLayout *>(layout());
    auto *findRow = new QHBoxLayout;
    findRow->addWidget(new QLabel(tr("Find:")));
    m_findEdit = new QLineEdit;
    m_findEdit->setObjectName(QStringLiteral("batchRenameFind"));
    m_findEdit->setPlaceholderText(
        tr("Find in the file name (without extension); leave empty for no replacement"));
    findRow->addWidget(m_findEdit, 1);
    findRow->addWidget(new QLabel(tr("Replace with:")));
    m_replaceEdit = new QLineEdit;
    m_replaceEdit->setObjectName(QStringLiteral("batchRenameReplace"));
    m_replaceEdit->setPlaceholderText(tr("Leave empty to delete the match"));
    m_replaceEdit->setToolTip(tr("In regex mode, use \\1, \\2, … to reference capture groups"));
    findRow->addWidget(m_replaceEdit, 1);
    mainLay->addLayout(findRow);

    auto *optionsRow = new QHBoxLayout;
    m_useRegexCheck = new QCheckBox(tr("Use regular expressions"));
    m_useRegexCheck->setObjectName(QStringLiteral("batchRenameRegex"));
    m_useRegexCheck->setToolTip(tr("Example: find IMG_(\\d+) and replace with photo_\\1"));
    m_caseSensitiveCheck = new QCheckBox(tr("Case sensitive"));
    m_caseSensitiveCheck->setObjectName(QStringLiteral("batchRenameCase"));
    optionsRow->addWidget(m_useRegexCheck);
    optionsRow->addWidget(m_caseSensitiveCheck);
    optionsRow->addStretch();
    mainLay->addLayout(optionsRow);
}

mviewer::core::BatchRenameOptions BatchRenamePanel::options() const
{
    mviewer::core::BatchRenameOptions opts;
    opts.pattern = m_patternEdit->text().toStdString();
    opts.find = m_findEdit->text().toStdString();
    opts.replace = m_replaceEdit->text().toStdString();
    opts.useRegex = m_useRegexCheck->isChecked();
    opts.caseSensitive = m_caseSensitiveCheck->isChecked();
    return opts;
}

void BatchRenamePanel::setOptions(const mviewer::core::BatchRenameOptions &options)
{
    {
        const QSignalBlocker blockPattern(m_patternEdit);
        const QSignalBlocker blockFind(m_findEdit);
        const QSignalBlocker blockReplace(m_replaceEdit);
        const QSignalBlocker blockRegex(m_useRegexCheck);
        const QSignalBlocker blockCase(m_caseSensitiveCheck);
        m_patternEdit->setText(QString::fromStdString(options.pattern));
        m_findEdit->setText(QString::fromStdString(options.find));
        m_replaceEdit->setText(QString::fromStdString(options.replace));
        m_useRegexCheck->setChecked(options.useRegex);
        m_caseSensitiveCheck->setChecked(options.caseSensitive);
    }
    updatePreview();
}

void BatchRenamePanel::setSourceFiles(const QStringList &files)
{
    m_sourceFiles = files;
    updatePreview();
}

void BatchRenamePanel::setTargetExtension(const QString &extension)
{
    m_targetExtension = extension;
    updatePreview();
}

void BatchRenamePanel::setSettingsGroup(const QString &group)
{
    m_settingsGroup = group;
}

bool BatchRenamePanel::isValid() const
{
    return m_isValid;
}

QString BatchRenamePanel::errorMessage() const
{
    return m_errorMessage;
}

QLineEdit *BatchRenamePanel::patternEdit() const
{
    return m_patternEdit;
}

void BatchRenamePanel::loadSettings()
{
    setOptions(mviewer::core::loadBatchRenameSettings(m_settingsGroup.toStdString()));
}

void BatchRenamePanel::saveSettings() const
{
    mviewer::core::saveBatchRenameSettings(options(), m_settingsGroup.toStdString());
}

void BatchRenamePanel::updatePreview()
{
    std::vector<std::string> files;
    files.reserve(static_cast<size_t>(m_sourceFiles.size()));
    for (const QString &file : m_sourceFiles)
        files.push_back(file.toStdString());
    const bool sample = files.empty();
    if (sample)
        files.emplace_back("example.jpg");

    std::string error;
    const auto results = mviewer::core::validateBatchRename(files, options(), &error,
                                                            m_targetExtension.toStdString());
    const bool wasValid = m_isValid;
    m_isValid = error.empty();
    m_errorMessage = QString::fromStdString(error);
    m_errorLabel->setText(m_errorMessage);
    m_errorLabel->setVisible(!m_isValid);
    QString text = previewText(results);
    if (sample)
        text += tr("(Example)");
    m_previewLabel->setText(text);
    if (wasValid != m_isValid)
        emit validityChanged(m_isValid);
}
