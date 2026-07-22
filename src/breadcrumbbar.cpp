#include "breadcrumbbar.h"

#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

BreadcrumbBar::BreadcrumbBar(QWidget *parent)
    : QWidget(parent)
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
    // Clear existing buttons
    QLayoutItem *child;
    while ((child = m_layout->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }

    if (m_currentPath.isEmpty())
        return;

    // Split path into segments
    m_segments.clear();
#ifdef Q_OS_WIN
    // Windows: "D:/a/b/c" → ["D:", "a", "b", "c"]
    QString path = QDir::toNativeSeparators(m_currentPath);
    path.replace('\\', '/');
#else
    QString path = m_currentPath;
#endif

    // Split by '/' but preserve the drive/root
    QStringList parts = path.split('/', Qt::SkipEmptyParts);

    // Detect drive letter (e.g. "D:" → first segment)
    int segStart = 0;
    if (!parts.isEmpty() && parts.first().endsWith(':'))
    {
        m_segments << parts.first();
        segStart = 1;
    }
    else if (path.startsWith('/'))
    {
        m_segments << "/";
        segStart = 0;
    }

    for (int i = segStart; i < parts.size(); ++i)
        m_segments << parts.at(i);

    if (m_segments.isEmpty())
        return;

    // Overflow handling: if too many segments, collapse early ones into "..."
    int firstVisible = 0;
    m_overflow = false;
    if (m_segments.size() > m_maxVisible)
    {
        m_overflow = true;
        firstVisible = m_segments.size() - m_maxVisible + 1;  // +1 for the "..." button
    }

    // Overflow button
    if (m_overflow)
    {
        auto *btn = new QToolButton(this);
        btn->setText("...");
        btn->setAutoRaise(true);
        btn->setToolTip("Show more path segments");
        btn->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(btn);
        for (int i = 0; i < firstVisible; ++i)
        {
            QString partialPath;
            for (int j = 0; j <= i; ++j)
            {
                if (!partialPath.isEmpty())
                    partialPath += '/';
                partialPath += m_segments.at(j);
            }
            QAction *act = menu->addAction(m_segments.at(i));
            act->setData(partialPath);
            connect(act, &QAction::triggered, this, [this, act]() {
                emit pathSelected(act->data().toString());
            });
        }
        btn->setMenu(menu);
        m_layout->addWidget(btn);

        // Arrow
        auto *arrow = new QLabel(">", this);
        arrow->setFixedWidth(12);
        arrow->setAlignment(Qt::AlignCenter);
        QFont af = arrow->font();
        af.setPointSize(8);
        arrow->setFont(af);
        arrow->setStyleSheet("color: #888;");
        m_layout->addWidget(arrow);
    }

    // Build path string incrementally for data
    QString built;
    for (int i = 0; i < firstVisible && !m_overflow; ++i)
    {
        if (!built.isEmpty())
            built += '/';
        built += m_segments.at(i);
    }

    // Visible segments
    for (int i = firstVisible; i < m_segments.size(); ++i)
    {
        if (!built.isEmpty())
            built += '/';
        built += m_segments.at(i);

        auto *btn = new QToolButton(this);
        btn->setText(m_segments.at(i));
        btn->setAutoRaise(true);
        btn->setToolTip(built);
        btn->setMaximumWidth(kMaxButtonWidth);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        btn->setProperty("breadcrumbPath", built);
        connect(btn, &QToolButton::clicked, this, &BreadcrumbBar::onSegmentClicked);

        // Style: make it look like a link/navigation element
        btn->setStyleSheet(
            "QToolButton {"
            "  border: 1px solid transparent;"
            "  border-radius: 3px;"
            "  padding: 1px 4px;"
            "  color: #444;"
            "  font-size: 11px;"
            "}"
            "QToolButton:hover {"
            "  border-color: #c0c0c0;"
            "  background: #f0f0f0;"
            "  color: #0078d7;"
            "}");

        m_layout->addWidget(btn);

        // Arrow separator (except after the last segment)
        if (i < m_segments.size() - 1)
        {
            auto *arrow = new QLabel(">", this);
            arrow->setFixedWidth(12);
            arrow->setAlignment(Qt::AlignCenter);
            QFont af = arrow->font();
            af.setPointSize(8);
            arrow->setFont(af);
            arrow->setStyleSheet("color: #888;");
            m_layout->addWidget(arrow);
        }
    }

    m_layout->addStretch();
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
