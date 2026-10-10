#include "mainwindow_p.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

void MainWindow::installNavigationCollapse()
{
    m_actToggleNavigation = new QAction(tr("侧栏"), this);
    m_actToggleNavigation->setObjectName(QStringLiteral("toggleNavigationPanelAction"));
    m_actToggleNavigation->setCheckable(true);
    m_actToggleNavigation->setChecked(true);
    m_actToggleNavigation->setToolTip(tr("显示或隐藏文件夹与预览侧栏"));
    if (auto *bar = findChild<QToolBar *>(QStringLiteral("browserToolBar")))
    {
        bar->setIconSize(QSize(16, 16));
        bar->setFixedHeight(40);
        auto *button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("toggleNavigationPanelButton"));
        button->setDefaultAction(m_actToggleNavigation);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(28);
        QAction *first = bar->actions().isEmpty() ? nullptr : bar->actions().constFirst();
        bar->insertWidget(first, button);
    }
    connect(m_actToggleNavigation, &QAction::toggled, this,
            [this](bool shown)
            {
                if (m_focusBrowse)
                {
                    const QSignalBlocker blocker(m_actToggleNavigation);
                    m_actToggleNavigation->setChecked(false);
                    return;
                }
                if (m_navigationWidget)
                    m_navigationWidget->setVisible(shown);
                QSettings settings;
                settings.setValue(QStringLiteral("navigationPanelCollapsed"), !shown);
                settings.sync();
            });
    QTimer::singleShot(
        0, this,
        [this]()
        {
            if (!m_navigationWidget || m_focusBrowse)
                return;
            const bool collapsed =
                QSettings().value(QStringLiteral("navigationPanelCollapsed"), false).toBool();
            m_navigationWidget->setVisible(!collapsed);
            if (!m_actToggleNavigation)
                return;
            const QSignalBlocker blocker(m_actToggleNavigation);
            m_actToggleNavigation->setChecked(!collapsed);
        });
}

void MainWindow::polishGalleryToolbar(QWidget *sortBar)
{
    if (!sortBar)
        return;
    const auto widgets = sortBar->findChildren<QWidget *>();
    for (QWidget *widget : widgets)
    {
        if (qobject_cast<QComboBox *>(widget) || qobject_cast<QPushButton *>(widget) ||
            qobject_cast<QLineEdit *>(widget))
            widget->setFixedHeight(28);
    }
}

void MainWindow::placeGalleryCompareButton(QWidget *sortBar)
{
    if (!sortBar || !m_thumbnailPanel)
        return;
    auto *button =
        m_thumbnailPanel->findChild<QPushButton *>(QStringLiteral("compareSelectionButton"));
    auto *root = qobject_cast<QVBoxLayout *>(sortBar->layout());
    if (!button || !root)
        return;
    QHBoxLayout *row = nullptr;
    for (int i = 0; i < root->count(); ++i)
    {
        QLayoutItem *item = root->itemAt(i);
        if (item && item->layout())
            row = qobject_cast<QHBoxLayout *>(item->layout());
        if (row)
            break;
    }
    if (!row)
        return;
    button->setParent(sortBar);
    button->setText(QStringLiteral("比较 (P)"));
    button->setFixedHeight(28);
    button->adjustSize();
    row->addWidget(button);
}
