#include "yuvextensionmapeditor.h"

#include "core/image/ImageFormats.h"
#include "core/image/yuv/YuvFormat.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

namespace yuv = mviewer::core::yuv;

namespace
{

// Offered in the pixel-format column; the combo is editable, so any name the
// YUV dialog shows (e.g. nv12p12le, yuv422p10msb) can be typed as well.
const char *const kCommonPixFmts[] = {
    "nv12", "nv21", "yuv420p", "yvu420p",     "p010le",      "p010be",  "p016le",  "yuv420p10le",
    "nv16", "nv61", "yuv422p", "p210le",      "yuv422p10le", "yuyv422", "uyvy422", "yvyu422",
    "nv24", "nv42", "yuv444p", "yuv444p10le", "gray",        "gray10le"};

QComboBox *makePixFmtCombo(QWidget *parent, const QString &pixFmt)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItem(YuvExtensionMapEditor::tr("Auto-detect"), QString());
    for (const char *name : kCommonPixFmts)
        combo->addItem(QString::fromLatin1(name), QString::fromLatin1(name));
    if (pixFmt.isEmpty())
    {
        combo->setCurrentIndex(0);
    }
    else
    {
        const int index = combo->findData(pixFmt);
        if (index >= 0)
            combo->setCurrentIndex(index);
        else
            combo->setEditText(pixFmt);
    }
    return combo;
}

QString comboPixFmt(const QComboBox *combo)
{
    if (combo == nullptr)
        return {};
    const QString text = combo->currentText().trimmed();
    if (text.isEmpty() || text == combo->itemText(0))
        return {};
    return text.toLower();
}

} // namespace

YuvExtensionMapEditor::YuvExtensionMapEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("yuvExtensionMapEditor"));
    auto *hint = new QLabel(
        tr("Files with these extensions open as raw YUV with the chosen pixel format. A y4m "
           "header, saved per-file parameters and format names in the file name still take "
           "priority."),
        this);
    hint->setWordWrap(true);

    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("yuvExtensionMapTable"));
    m_table->setHorizontalHeaderLabels({tr("Extension"), tr("Pixel format")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *add = new QPushButton(tr("Add"), this);
    add->setObjectName(QStringLiteral("yuvExtensionMapAdd"));
    auto *remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("yuvExtensionMapRemove"));
    auto *defaults = new QPushButton(tr("Restore Defaults"), this);
    defaults->setObjectName(QStringLiteral("yuvExtensionMapDefaults"));
    connect(add, &QPushButton::clicked, this,
            [this]()
            {
                addRow(QString(), QString());
                const int row = m_table->rowCount() - 1;
                m_table->setCurrentCell(row, 0);
                m_table->editItem(m_table->item(row, 0));
            });
    connect(remove, &QPushButton::clicked, this,
            [this]()
            {
                const int row = m_table->currentRow();
                if (row >= 0)
                    m_table->removeRow(row);
            });
    connect(defaults, &QPushButton::clicked, this,
            [this]() { setRules(yuv::defaultExtensionRules()); });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(defaults);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
}

void YuvExtensionMapEditor::setRules(const std::vector<yuv::ExtensionRule> &rules)
{
    m_table->setRowCount(0);
    for (const yuv::ExtensionRule &rule : rules)
        addRow(QString::fromStdString(rule.ext), QString::fromStdString(rule.pixFmt));
}

void YuvExtensionMapEditor::addRow(const QString &ext, const QString &pixFmt)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(ext));
    m_table->setCellWidget(row, 1, makePixFmtCombo(m_table, pixFmt));
}

int YuvExtensionMapEditor::rowCount() const
{
    return m_table->rowCount();
}

std::vector<yuv::ExtensionRule> YuvExtensionMapEditor::rules() const
{
    std::vector<yuv::ExtensionRule> out;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        const QTableWidgetItem *item = m_table->item(row, 0);
        const std::string ext =
            yuv::normalizeExtension(item ? item->text().trimmed().toStdString() : std::string());
        if (ext.empty())
            continue;
        const QString pix = comboPixFmt(qobject_cast<QComboBox *>(m_table->cellWidget(row, 1)));
        yuv::ExtensionRule rule;
        rule.ext = ext;
        if (!pix.isEmpty())
        {
            yuv::Format f;
            if (yuv::applyPixFmtName(pix.toStdString(), f))
                rule.pixFmt = yuv::pixFmtName(f);
        }
        out.push_back(rule);
    }
    return out;
}

QString YuvExtensionMapEditor::validationError() const
{
    const QSet<QString> current = []
    {
        QSet<QString> s;
        for (const std::string &e : yuv::yuvExtensions())
            s.insert(QString::fromStdString(e));
        return s;
    }();
    QSet<QString> seen;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        const QTableWidgetItem *item = m_table->item(row, 0);
        const QString typed = item ? item->text().trimmed() : QString();
        const QString pix = comboPixFmt(qobject_cast<QComboBox *>(m_table->cellWidget(row, 1)));
        if (typed.isEmpty())
        {
            if (!pix.isEmpty())
                return tr("Row %1: enter a file extension.").arg(row + 1);
            continue;
        }
        const QString ext = QString::fromStdString(yuv::normalizeExtension(typed.toStdString()));
        if (ext.isEmpty())
            return tr("Row %1: \"%2\" is not a usable extension (letters, digits and _ only; "
                      ".y4m is always read from its header).")
                .arg(row + 1)
                .arg(typed);
        if (seen.contains(ext))
            return tr("Extension .%1 is listed twice.").arg(ext);
        seen.insert(ext);
        // An image format MViewer already decodes (png, raw, ...) must keep its decoder.
        if (!current.contains(ext) &&
            mviewer::core::ImageFormats::isSupportedSuffix(ext.toStdString()))
            return tr("Extension .%1 already belongs to another image format.").arg(ext);
        yuv::Format f;
        if (!pix.isEmpty() &&
            (!yuv::applyPixFmtName(pix.toStdString(), f) || yuv::pixFmtName(f).empty()))
            return tr("Row %1: unknown pixel format \"%2\".").arg(row + 1).arg(pix);
    }
    return {};
}
