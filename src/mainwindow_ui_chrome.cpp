#include "ThemeMetrics.h"
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
    m_actToggleNavigation = new QAction(tr("Sidebar"), this);
    m_actToggleNavigation->setObjectName(QStringLiteral("toggleNavigationPanelAction"));
    m_actToggleNavigation->setCheckable(true);
    m_actToggleNavigation->setChecked(true);
    m_actToggleNavigation->setToolTip(tr("Show or hide the folder and preview sidebars"));
    if (auto *bar = findChild<QToolBar *>(QStringLiteral("browserToolBar")))
    {
        bar->setIconSize(QSize(16, 16));
        bar->setFixedHeight(mviewer::ui::metrics::kBarHeight);
        auto *button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("toggleNavigationPanelButton"));
        button->setDefaultAction(m_actToggleNavigation);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(mviewer::ui::metrics::kControlHeight);
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
            widget->setFixedHeight(mviewer::ui::metrics::kControlHeight);
    }
}

void MainWindow::placeGalleryCompareButton(QWidget *sortBar)
{
    auto *root = sortBar ? qobject_cast<QVBoxLayout *>(sortBar->layout()) : nullptr;
    if (!root)
        return;
    QHBoxLayout *row = nullptr;
    for (int i = 0; i < root->count() && !row; ++i)
    {
        QLayoutItem *item = root->itemAt(i);
        if (item && item->layout())
            row = qobject_cast<QHBoxLayout *>(item->layout());
    }
    dockCompareButton(m_thumbnailPanel, sortBar, row, m_typeFilterCombo);
}

// Moves a pane's floating Compare button into its toolbar row. Left floating
// on the list viewport it would sit under the Details header ("Name").
void MainWindow::dockCompareButton(ThumbnailPanel *panel, QWidget *host, QHBoxLayout *row,
                                   QWidget *after)
{
    if (!panel || !host || !row)
        return;
    auto *button = panel->findChild<QPushButton *>(QStringLiteral("compareSelectionButton"));
    if (!button)
        return;
    button->setParent(host);
    button->setFixedHeight(mviewer::ui::metrics::kControlHeight);
    const int at = after ? row->indexOf(after) : -1;
    if (at >= 0)
        row->insertWidget(at + 1, button);
    else
        row->addWidget(button);
    panel->refreshCompareButton();
}

void MainWindow::setBrowseHiddenFormatGroups(const QStringList &groups)
{
    QStringList normalized = groups;
    normalized.removeDuplicates();
    normalized.sort();
    m_hiddenFormatGroups = normalized;
    mviewer::browseformats::saveHiddenGroups(normalized);
    for (FormatFilterCombo *combo : {m_typeFilterCombo, m_secondaryTypeFilterCombo})
    {
        if (combo)
            combo->setHiddenGroups(normalized);
    }
    for (ThumbnailPanel *panel : {m_thumbnailPanel, m_secondaryThumbnailPanel})
    {
        if (panel)
            panel->setHiddenFormatGroups(normalized);
    }
}

QStringList MainWindow::browseHiddenFormatGroups() const
{
    return m_hiddenFormatGroups;
}
