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
#ifdef Q_OS_WIN
    QString path = QDir::toNativeSeparators(m_currentPath);
    path.replace('\\', '/');
#else
    const QString path = m_currentPath;
#endif
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    int segmentStart = 0;
    if (!parts.isEmpty() && parts.first().endsWith(':'))
    {
        m_segments << parts.first();
        segmentStart = 1;
    }
    else if (path.startsWith('/'))
        m_segments << "/";
    for (int i = segmentStart; i < parts.size(); ++i)
        m_segments << parts.at(i);
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
        const QString partialPath = m_segments.mid(0, i + 1).join('/');
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
    QString built = m_overflow ? m_segments.mid(0, firstVisible).join('/') : QString();
    for (int i = firstVisible; i < m_segments.size(); ++i)
    {
        if (!built.isEmpty())
            built += '/';
        built += m_segments.at(i);
        auto *button = new QToolButton(this);
        button->setText(m_segments.at(i));
        button->setAutoRaise(true);
        button->setToolTip(built);
        button->setMaximumWidth(kMaxButtonWidth);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        button->setProperty("breadcrumbPath", built);
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
