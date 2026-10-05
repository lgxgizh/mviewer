#include "breadcrumbbar.h"

#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

BreadcrumbBar::BreadcrumbBar(QWidget *parent) : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(2, 0, 2, 0);
    m_layout->setSpacing(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(28);
}

void BreadcrumbBar::setPath(const QString &path)
{
    if (path == m_currentPath)
        return;
    m_currentPath = path;
    rebuild();
}

void BreadcrumbBar::rebuild()
{
    clearButtons();
    if (m_currentPath.isEmpty())
        return;
    parseSegments();
    if (m_segments.isEmpty())
        return;

    int firstVisible = 0;
    m_overflow = false;
    if (m_segments.size() > m_maxVisible)
    {
        m_overflow = true;
        firstVisible = m_segments.size() - m_maxVisible + 1;
    }
    addOverflowButton(firstVisible);
    addVisibleSegments(firstVisible);
    m_layout->addStretch();
}

void BreadcrumbBar::clearButtons()
{
    QLayoutItem *child = nullptr;
    while ((child = m_layout->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }
}

void BreadcrumbBar::parseSegments()
{
    m_segments.clear();
    QString path = m_currentPath;
    path.replace('\\', '/');
    while (path.endsWith('/') && path.size() > 1 && !(path.size() == 3 && path.at(1) == ':'))
        path.chop(1);

    const bool isUnc = path.startsWith("//");
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty())
    {
        if (path.startsWith('/'))
            m_segments << "/";
        return;
    }

    int segmentStart = 0;
    if (isUnc && parts.size() >= 2)
    {
        m_segments << QString("//%1/%2").arg(parts.at(0), parts.at(1));
        segmentStart = 2;
    }
    else if (parts.first().endsWith(':'))
    {
        m_segments << parts.first();
        segmentStart = 1;
    }
    else if (path.startsWith('/'))
    {
        m_segments << "/";
    }

    for (int i = segmentStart; i < parts.size(); ++i)
        m_segments << parts.at(i);
}

QString BreadcrumbBar::pathForIndex(int index) const
{
    if (index < 0 || index >= m_segments.size())
        return QString();

    const QString &first = m_segments.first();
    if (first == "/")
    {
        if (index == 0)
            return "/";
        QString res;
        for (int i = 1; i <= index; ++i)
            res += "/" + m_segments.at(i);
        return res;
    }

    if (first.endsWith(':'))
    {
        QString res = first + "/";
        if (index == 0)
            return res;
        for (int i = 1; i <= index; ++i)
        {
            if (i > 1)
                res += "/";
            res += m_segments.at(i);
        }
        return res;
    }

    if (first.startsWith("//"))
    {
        QString res = first;
        for (int i = 1; i <= index; ++i)
            res += "/" + m_segments.at(i);
        return res;
    }

    QStringList sub = m_segments.mid(0, index + 1);
    return sub.join('/');
}

void BreadcrumbBar::addOverflowButton(int firstVisible)
{
    if (!m_overflow)
        return;
    auto *button = new QToolButton(this);
    button->setText("...");
    button->setAutoRaise(true);
    button->setToolTip("Show more path segments");
    button->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(button);
    for (int i = 0; i < firstVisible; ++i)
    {
        const QString partialPath = pathForIndex(i);
        QAction *action = menu->addAction(m_segments.at(i));
        action->setData(partialPath);
        connect(action, &QAction::triggered, this,
                [this, action]() { emit pathSelected(action->data().toString()); });
    }
    button->setMenu(menu);
    m_layout->addWidget(button);
    auto *arrow = new QLabel(">", this);
    arrow->setFixedWidth(kArrowSize);
    arrow->setAlignment(Qt::AlignCenter);
    arrow->setStyleSheet("color: #888;");
    m_layout->addWidget(arrow);
}

void BreadcrumbBar::addVisibleSegments(int firstVisible)
{
    for (int i = firstVisible; i < m_segments.size(); ++i)
    {
        const QString segmentPath = pathForIndex(i);
        auto *button = new QToolButton(this);
        button->setText(m_segments.at(i));
        button->setAutoRaise(true);
        button->setToolTip(segmentPath);
        button->setMaximumWidth(kMaxButtonWidth);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        button->setProperty("breadcrumbPath", segmentPath);
        connect(button, &QToolButton::clicked, this, &BreadcrumbBar::onSegmentClicked);
        button->setStyleSheet("QToolButton { border: 1px solid transparent; border-radius: 3px;"
                              " padding: 1px 4px; color: #444; font-size: 11px; }"
                              "QToolButton:hover { border-color: #c0c0c0; background: #f0f0f0;"
                              " color: #0078d7; }");
        m_layout->addWidget(button);
        if (i < m_segments.size() - 1)
        {
            auto *arrow = new QLabel(">", this);
            arrow->setFixedWidth(kArrowSize);
            arrow->setAlignment(Qt::AlignCenter);
            arrow->setStyleSheet("color: #888;");
            m_layout->addWidget(arrow);
        }
    }
}

void BreadcrumbBar::onSegmentClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    QString path = btn->property("breadcrumbPath").toString();
    if (!path.isEmpty())
        emit pathSelected(path);
}
