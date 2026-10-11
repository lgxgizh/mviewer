// Gallery empty states: the "no folder open" guide page and the
// "folder has nothing to show" hint. Both are overlays on the thumbnail panel.
#include "ThemeMetrics.h"
#include "mainwindow_p.h"

#include "Theme.h"

#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

namespace
{

using mviewer::ui::Theme;
using mviewer::ui::ThemeRole;
namespace metrics = mviewer::ui::metrics;

// One card look for both hints, from the active theme tokens.
QString hintCardHtml(const QString &title, const QString &body)
{
    return QStringLiteral("<table align='center' cellpadding='0'><tr><td align='center' "
                          "style='padding:%1px %2px;'>"
                          "<div style='color:%3; font-size:%4px; font-weight:600; "
                          "margin-bottom:%5px;'>%6</div>"
                          "<div style='color:%7; font-size:%8px;'>%9</div>"
                          "</td></tr></table>")
        .arg(QString::number(metrics::kSpace16), QString::number(metrics::kSpace16 * 2),
             Theme::themeColor(ThemeRole::TextPrimary), QString::number(metrics::kFontTitlePx),
             QString::number(metrics::kSpace8), title, Theme::themeColor(ThemeRole::TextSecondary),
             QString::number(metrics::kFontBodyPx), body);
}

QString keyCap(const QString &key)
{
    return QStringLiteral("<span style='background-color:%1; color:%2;'>&nbsp;%3&nbsp;</span>")
        .arg(Theme::themeColor(ThemeRole::Bg3), Theme::themeColor(ThemeRole::TextPrimary), key);
}

QLabel *makeHintLabel(QWidget *parent, const QString &objectName, const QString &html)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    label->setText(html);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return label;
}

} // namespace

void MainWindow::buildEmptyStateUi()
{
    // Guide page: shown until the first directory is opened.
    const QString guideBody = tr("%1 打开目录 &nbsp;·&nbsp; 也可将图片或文件夹拖入窗口")
                                  .arg(keyCap(QStringLiteral("Ctrl+O")));
    m_emptyState = makeHintLabel(m_thumbnailPanel, QStringLiteral("emptyStateLabel"),
                                 hintCardHtml(tr("打开一个文件夹以开始浏览"), guideBody));
    m_emptyState->show();

    // Clickable entry point under the guide text; it is a sibling, not a child,
    // because the label ignores the mouse so the gallery keeps drag and drop.
    m_emptyStateOpen = new QPushButton(tr("打开文件夹…"), m_thumbnailPanel);
    m_emptyStateOpen->setObjectName(QStringLiteral("emptyStateOpenButton"));
    m_emptyStateOpen->setProperty("primary", true);
    m_emptyStateOpen->setCursor(Qt::PointingHandCursor);
    m_emptyStateOpen->setFixedHeight(metrics::kControlHeight + metrics::kSpace4);
    m_emptyStateOpen->setToolTip(tr("打开文件夹 (Ctrl+O)"));
    connect(m_emptyStateOpen, &QPushButton::clicked, this,
            [this]
            {
                if (m_actOpenDir && m_actOpenDir->isEnabled())
                    m_actOpenDir->trigger();
            });
    updateEmptyState();

    // Empty-folder hint: a directory is open but nothing is displayable
    // (no image files, or every entry is hidden by filters). Deferred via
    // a short timer so the transient pre-scan zero cannot flash it.
    m_emptyFolderLabel = makeHintLabel(
        m_thumbnailPanel, QStringLiteral("emptyFolderLabel"),
        hintCardHtml(tr("此文件夹中没有可显示的图片"), tr("尝试清除筛选或换一个文件夹")));
    m_emptyFolderLabel->hide();
}

void MainWindow::layoutEmptyState()
{
    if (!m_emptyState || !m_thumbnailPanel)
        return;
    const QRect area = m_thumbnailPanel->rect();
    if (!m_emptyStateOpen)
    {
        m_emptyState->setGeometry(area);
        return;
    }
    // Text block and button are centred together as one group.
    m_emptyStateOpen->adjustSize();
    const int buttonW = qMax(m_emptyStateOpen->sizeHint().width(), 160);
    const int buttonH = m_emptyStateOpen->height();
    const int textH = qMin(area.height(), m_emptyState->heightForWidth(area.width()));
    const int groupH = textH + metrics::kSpace8 + buttonH;
    const int top = area.top() + qMax(0, (area.height() - groupH) / 2);
    m_emptyState->setGeometry(area.left(), top, area.width(), qMax(textH, 1));
    m_emptyStateOpen->setGeometry(area.left() + (area.width() - buttonW) / 2,
                                  top + textH + metrics::kSpace8, buttonW, buttonH);
    m_emptyStateOpen->raise();
}
