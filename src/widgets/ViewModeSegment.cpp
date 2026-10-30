#include "widgets/ViewModeSegment.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

ViewModeSegment::ViewModeSegment(QWidget *parent) : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(0);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(false);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}

void ViewModeSegment::setModes(const QList<Mode> &modes)
{
    m_modes = modes;
    rebuild();
    syncFromCombo();
}

void ViewModeSegment::bind(QComboBox *combo)
{
    if (m_combo)
        disconnect(m_combo, nullptr, this, nullptr);
    m_combo = combo;
    if (!m_combo)
        return;
    connect(m_combo, &QObject::destroyed, this, [this]() { m_combo = nullptr; });
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { syncFromCombo(); });
    syncFromCombo();
}

void ViewModeSegment::setModeToolTip(int data, const QString &toolTip)
{
    for (Mode &mode : m_modes)
    {
        if (mode.data == data)
            mode.toolTip = toolTip;
    }
    if (QAbstractButton *button = buttonForData(data))
        button->setToolTip(toolTip);
}

QAbstractButton *ViewModeSegment::buttonForData(int data) const
{
    if (!m_group)
        return nullptr;
    for (QAbstractButton *button : m_group->buttons())
    {
        if (button && button->property("modeData").toInt() == data)
            return button;
    }
    return nullptr;
}

int ViewModeSegment::checkedData() const
{
    if (!m_group)
        return -1;
    for (QAbstractButton *button : m_group->buttons())
    {
        if (button && button->isChecked())
            return button->property("modeData").toInt();
    }
    return -1;
}

void ViewModeSegment::rebuild()
{
    if (!m_layout || !m_group)
        return;
    while (QLayoutItem *item = m_layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            if (auto *button = qobject_cast<QAbstractButton *>(widget))
                m_group->removeButton(button);
            delete widget;
        }
        delete item;
    }
    for (const Mode &mode : m_modes)
    {
        auto *button = new QPushButton(this);
        button->setObjectName(QStringLiteral("viewModeSegment_%1").arg(mode.data));
        button->setProperty("modeData", mode.data);
        button->setIcon(mode.icon);
        button->setIconSize(QSize(16, 16));
        button->setToolTip(mode.toolTip);
        button->setCheckable(true);
        button->setAutoExclusive(false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(28, 24);
        m_group->addButton(button);
        m_layout->addWidget(button);
        const int data = mode.data;
        connect(button, &QPushButton::clicked, this, [this, data]() { selectData(data); });
    }
}

bool ViewModeSegment::modeMatches(const Mode &mode, int current) const
{
    if (mode.data == current)
        return true;
    for (int extra : mode.matches)
    {
        if (extra == current)
            return true;
    }
    return false;
}

void ViewModeSegment::selectData(int data)
{
    if (m_combo)
    {
        const int index = m_combo->findData(data);
        if (index >= 0)
            m_combo->setCurrentIndex(index);
    }
    syncFromCombo();
}

void ViewModeSegment::syncFromCombo()
{
    if (!m_group)
        return;
    const bool haveCombo = m_combo && m_combo->currentIndex() >= 0;
    const int current = haveCombo ? m_combo->currentData().toInt() : -1;
    for (QAbstractButton *button : m_group->buttons())
    {
        if (!button)
            continue;
        const int data = button->property("modeData").toInt();
        bool on = false;
        if (haveCombo)
        {
            for (const Mode &mode : m_modes)
            {
                if (mode.data == data && modeMatches(mode, current))
                    on = true;
            }
        }
        const QSignalBlocker blocker(button);
        button->setChecked(on);
    }
}
