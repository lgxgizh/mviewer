#include "compareworkspace_p.h"
#include "compareworkspace_shortcuts.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QHeaderView>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <iterator>

namespace
{

void fillCompareShortcutTable(QTableWidget *table)
{
    const int rows = static_cast<int>(std::size(mviewer::cw::kCompareShortcuts));
    table->setColumnCount(2);
    table->setRowCount(rows);
    table->setHorizontalHeaderLabels({QString::fromUtf8("快捷键"), QString::fromUtf8("说明")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int row = 0; row < rows; ++row)
    {
        const mviewer::cw::CompareShortcut &entry =
            mviewer::cw::kCompareShortcuts[static_cast<size_t>(row)];
        table->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(entry.keys)));
        const QString detail = QString::fromUtf8(entry.name) + QString::fromUtf8(" — ") +
                               QString::fromUtf8(entry.description);
        table->setItem(row, 1, new QTableWidgetItem(detail));
    }
}

QDialog *createCompareShortcutHelp(QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("compareShortcutHelpDialog"));
    dialog->setWindowTitle(QString::fromUtf8("比较模式快捷键"));
    dialog->setModal(false);
    auto *layout = new QVBoxLayout(dialog);
    auto *table = new QTableWidget(dialog);
    table->setObjectName(QStringLiteral("compareShortcutTable"));
    fillCompareShortcutTable(table);
    layout->addWidget(table);
    dialog->resize(560, 480);
    return dialog;
}

} // namespace

void CompareWorkspace::showShortcutHelp()
{
    if (!m_shortcutHelpDialog)
        m_shortcutHelpDialog = createCompareShortcutHelp(this);
    const bool open = !m_shortcutHelpDialog->isVisible();
    m_shortcutHelpDialog->setVisible(open);
    if (open)
        m_shortcutHelpDialog->raise();
    // Pair guidance stays on the status label; the dialog is the full table.
    const QString tip =
        pairNavTooltip(false, false) + QStringLiteral(" ") + pairNavTooltip(true, false);
    showCompareStatus(tip, 8000);
}
