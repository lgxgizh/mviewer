#include "formatfiltercombo.h"

#include "browseformats.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStyleOptionComboBox>
#include <QStylePainter>

namespace
{
constexpr int kGroupIdRole = Qt::UserRole + 1;
}

FormatFilterCombo::FormatFilterCombo(QWidget *parent) : QComboBox(parent)
{
    m_model = new QStandardItemModel(this);
    setModel(m_model);
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    view()->viewport()->installEventFilter(this);
    view()->installEventFilter(this);
    rebuildItems();
}

void FormatFilterCombo::rebuildItems()
{
    m_model->clear();
    const QStringList ids = mviewer::browseformats::availableGroupIds();
    for (const QString &id : ids)
    {
        auto *item = new QStandardItem(mviewer::browseformats::groupLabel(id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setData(id, kGroupIdRole);
        item->setData(m_hidden.contains(id) ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        item->setToolTip(mviewer::browseformats::groupExtensions(id).join(QStringLiteral(", ")));
        m_model->appendRow(item);
    }
    setToolTip(tr("File types shown in both browse panes (multi-select)"));
    updateGeometry();
    update();
    refreshAccessibility();
}

void FormatFilterCombo::retranslate()
{
    rebuildItems();
}

void FormatFilterCombo::setHiddenGroups(const QStringList &hidden)
{
    m_hidden = hidden;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        QStandardItem *item = m_model->item(row);
        const QString id = item->data(kGroupIdRole).toString();
        item->setData(m_hidden.contains(id) ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }
    update();
    refreshAccessibility();
}

QStringList FormatFilterCombo::hiddenGroups() const
{
    return m_hidden;
}

void FormatFilterCombo::setGroupVisible(const QString &id, bool visible)
{
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        if (m_model->item(row)->data(kGroupIdRole).toString() == id &&
            m_hidden.contains(id) == visible)
        {
            toggleRow(row);
            return;
        }
    }
}

void FormatFilterCombo::toggleRow(int row)
{
    QStandardItem *item = m_model->item(row);
    if (!item)
        return;
    const QString id = item->data(kGroupIdRole).toString();
    if (m_hidden.contains(id))
        m_hidden.removeAll(id);
    else
        m_hidden.append(id);
    item->setData(m_hidden.contains(id) ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    update();
    refreshAccessibility();
    emit hiddenGroupsChanged(m_hidden);
}

QString FormatFilterCombo::summaryText() const
{
    const int total = m_model->rowCount();
    int shown = 0;
    for (int row = 0; row < total; ++row)
        if (!m_hidden.contains(m_model->item(row)->data(kGroupIdRole).toString()))
            ++shown;
    if (shown == total)
        return tr("All types");
    return tr("Types: %1/%2").arg(shown).arg(total);
}

void FormatFilterCombo::refreshAccessibility()
{
    setAccessibleName(tr("File types"));
    QStringList hiddenLabels;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        const QStandardItem *item = m_model->item(row);
        if (!item || !m_hidden.contains(item->data(kGroupIdRole).toString()))
            continue;
        hiddenLabels.append(item->text());
    }
    QString description = summaryText();
    if (!hiddenLabels.isEmpty())
    {
        description += QLatin1Char(' ');
        description += tr("Hidden: %1").arg(hiddenLabels.join(QStringLiteral(", ")));
    }
    setAccessibleDescription(description);
}

bool FormatFilterCombo::eventFilter(QObject *watched, QEvent *event)
{
    // Toggle on release and swallow the event so the popup stays open for
    // checking several groups in a row.
    if (watched == view()->viewport() && event->type() == QEvent::MouseButtonRelease)
    {
        const auto *me = static_cast<QMouseEvent *>(event);
        const QModelIndex idx = view()->indexAt(me->position().toPoint());
        if (idx.isValid())
            toggleRow(idx.row());
        return true;
    }
    if (watched == view() && event->type() == QEvent::KeyPress)
    {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Return ||
            ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Select)
        {
            const QModelIndex idx = view()->currentIndex();
            if (idx.isValid())
                toggleRow(idx.row());
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void FormatFilterCombo::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = summaryText();
    opt.currentIcon = QIcon();
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}
