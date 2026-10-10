#include "compareworkspace_p.h"

#include "Theme.h"
#include "ThemeTokens.h"

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>

namespace
{

QString themeColor(mviewer::ui::ThemeRole role)
{
    return mviewer::ui::Theme::themeColor(role);
}

void lift(QWidget *widget)
{
    if (!widget)
        return;
    if (QWidget *parent = widget->parentWidget())
    {
        if (QLayout *layout = parent->layout())
            layout->removeWidget(widget);
    }
}

void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0))
        delete item;
}

void tighten(QWidget *widget)
{
    if (!widget)
        return;
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QComboBox *>(widget) ||
        qobject_cast<QSpinBox *>(widget) || qobject_cast<QSlider *>(widget))
        widget->setFixedHeight(28);
}

void place(QLayout *layout, QWidget *widget, bool showIt)
{
    if (!layout || !widget)
        return;
    lift(widget);
    tighten(widget);
    layout->addWidget(widget);
    widget->setVisible(showIt);
}

void placeKept(QLayout *layout, QWidget *widget)
{
    if (!widget)
        return;
    const bool showIt = !widget->isHidden();
    place(layout, widget, showIt);
}

QHBoxLayout *barLayout(QWidget *root, const char *name)
{
    QWidget *bar = root ? root->findChild<QWidget *>(QString::fromLatin1(name)) : nullptr;
    return bar ? qobject_cast<QHBoxLayout *>(bar->layout()) : nullptr;
}

QWidget *makeBar(QWidget *parent, const char *name)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(QString::fromLatin1(name));
    bar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    return bar;
}

void styleSegment(QCheckBox *box)
{
    if (!box)
        return;
    const int radius = mviewer::ui::makeThemeTokens(true).radiusControl;
    box->setCursor(Qt::PointingHandCursor);
    box->setFixedHeight(28);
    box->setStyleSheet(QStringLiteral("QCheckBox{spacing:0px;padding:2px 6px;border:1px solid %1;"
                                      "border-radius:%2px;background:%3;}"
                                      "QCheckBox::indicator{width:0px;height:0px;}"
                                      "QCheckBox:checked{background:%4;border-color:%5;}"
                                      "QCheckBox:disabled{color:%6;}")
                           .arg(themeColor(mviewer::ui::ThemeRole::Border))
                           .arg(radius)
                           .arg(themeColor(mviewer::ui::ThemeRole::Bg2))
                           .arg(mviewer::ui::Theme::themeRgba(mviewer::ui::ThemeRole::Accent, 71))
                           .arg(themeColor(mviewer::ui::ThemeRole::Accent))
                           .arg(themeColor(mviewer::ui::ThemeRole::TextDisabled)));
}

void bindMirror(QMenu *menu, QCheckBox *box, const char *name)
{
    if (!menu || !box)
        return;
    auto *action = menu->addAction(box->text());
    action->setObjectName(QString::fromLatin1(name));
    action->setCheckable(true);
    action->setToolTip(box->toolTip().isEmpty() ? box->text() : box->toolTip());
    action->setChecked(box->isChecked());
    QObject::connect(action, &QAction::toggled, box,
                     [box, action](bool on)
                     {
                         if (!box->isEnabled())
                         {
                             const QSignalBlocker blocker(action);
                             action->setChecked(box->isChecked());
                             return;
                         }
                         box->setChecked(on);
                     });
    QObject::connect(box, &QCheckBox::toggled, action,
                     [action](bool on) { action->setChecked(on); });
    lift(box);
    box->hide();
}

QToolButton *makePopup(QWidget *parent, const char *name, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QString::fromLatin1(name));
    button->setText(text);
    button->setToolTip(tip);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFixedHeight(28);
    button->setMenu(new QMenu(button));
    return button;
}

void showWithMenu(QMenu *menu, QWidget *panel)
{
    if (!menu || !panel)
        return;
    QObject::connect(menu, &QMenu::aboutToShow, panel,
                     [menu, panel]()
                     {
                         QTimer::singleShot(0, panel,
                                            [menu, panel]()
                                            {
                                                if (menu->isVisible())
                                                    panel->setVisible(true);
                                            });
                     });
    QObject::connect(menu, &QMenu::aboutToHide, panel,
                     [menu, panel]()
                     {
                         panel->hide();
                         QTimer::singleShot(0, panel,
                                            [menu, panel]()
                                            {
                                                if (!menu->isVisible())
                                                    panel->hide();
                                            });
                     });
}

// QWidgetAction::setDefaultWidget() orphans the panel until the menu first
// opens; parent it to the menu now so its controls stay in the workspace tree.
// The panel must not be hidden before setDefaultWidget(): a hidden default
// widget makes the action invisible, and Qt then force-disables the panel.
void keepFindable(QMenu *menu, QWidget *panel)
{
    if (!menu || !panel || panel->parentWidget())
        return;
    panel->setParent(menu);
    panel->hide();
}

void park(QVBoxLayout *layout, QWidget *widget, bool showIt)
{
    if (!layout || !widget)
        return;
    const bool visible = showIt || !widget->isHidden();
    lift(widget);
    tighten(widget);
    layout->addWidget(widget);
    widget->setVisible(showIt ? true : visible);
}

} // namespace

QWidget *CompareWorkspace::buildToolbarContainer(QHBoxLayout *&modeLayout, QHBoxLayout *&viewLayout,
                                                 QHBoxLayout *&toolLayout,
                                                 QHBoxLayout *&toolActionsLayout)
{
    auto *container = new QWidget(this);
    container->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    auto *root = new QVBoxLayout(container);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto *row1 = new QWidget(container);
    auto *row1Lay = new QHBoxLayout(row1);
    row1Lay->setContentsMargins(0, 0, 0, 0);
    row1Lay->setSpacing(8);
    QWidget *modeBar = makeBar(row1, "compareModeToolbar");
    QWidget *navBar = makeBar(row1, "compareNavTail");
    row1Lay->addWidget(modeBar);
    row1Lay->addStretch(1);
    row1Lay->addWidget(navBar);

    auto *row2 = new QWidget(container);
    auto *row2Lay = new QHBoxLayout(row2);
    row2Lay->setContentsMargins(0, 0, 0, 0);
    row2Lay->setSpacing(8);
    QWidget *viewBar = makeBar(row2, "compareViewToolbar");
    QWidget *toolBar = makeBar(row2, "compareToolToolbar");
    row2Lay->addWidget(viewBar);
    row2Lay->addStretch(1);
    row2Lay->addWidget(toolBar);

    root->addWidget(row1);
    root->addWidget(row2);
    modeLayout = qobject_cast<QHBoxLayout *>(modeBar->layout());
    viewLayout = qobject_cast<QHBoxLayout *>(viewBar->layout());
    toolLayout = qobject_cast<QHBoxLayout *>(toolBar->layout());
    toolActionsLayout = toolLayout;
    return container;
}

void CompareWorkspace::presentDiffMetrics(const QString &text)
{
    if (!m_metricLabel)
        return;
    // Keep the full metric text in the label (reports and tests read it); a
    // fixed maximum width clips the chip visually instead of eliding text, so
    // threshold-dependent tail values never collapse into identical strings
    // on platforms with wider fonts.
    QString chip = text;
    chip.replace(QLatin1Char('\n'), QStringLiteral(" · "));
    m_metricLabel->setWordWrap(false);
    m_metricLabel->setMaximumWidth(480);
    m_metricLabel->setToolTip(text);
    m_metricLabel->setText(chip);
}

void CompareWorkspace::installCompareMenus(QWidget *viewBar, QWidget *toolBar, QWidget *&syncButton,
                                           QWidget *&adjustButton, QWidget *&moreButton)
{
    auto *sync = makePopup(viewBar, "compareSyncButton", tr("同步"),
                           tr("同步缩放 (Z)、同步拖动 (D)、同步旋转 (Alt+R)、同步准星 "
                              "(R)、像素连线 (L)、统一像素倍率"));
    bindMirror(sync->menu(), m_syncZoomChk, "syncZoomAction");
    bindMirror(sync->menu(), m_syncDragChk, "syncDragAction");
    bindMirror(sync->menu(), m_syncRotateChk, "syncRotateAction");
    bindMirror(sync->menu(), m_crosshairChk, "syncCrosshairAction");
    bindMirror(sync->menu(), m_pixelLinkChk, "pixelLinkAction");
    bindMirror(sync->menu(), m_uniformScaleChk, "uniformScaleAction");
    syncButton = sync;

    auto *adjust = makePopup(viewBar, "compareAdjustButton", tr("调整"),
                             tr("亮度、对比度、伽马、增益、旋转与翻转（仅预览，不覆盖原文件）"));
    if (m_editPanel && adjust->menu())
    {
        lift(m_editPanel);
        m_editPanel->setParent(adjust);
        m_editPanel->setMinimumWidth(260);
        auto *adjustAction = new QWidgetAction(adjust);
        adjustAction->setDefaultWidget(m_editPanel);
        adjust->menu()->addAction(adjustAction);
        keepFindable(adjust->menu(), m_editPanel);
        showWithMenu(adjust->menu(), m_editPanel);
    }
    adjustButton = adjust;

    auto *more = makePopup(toolBar, "compareMoreButton", tr("⋯ 更多"),
                           tr("存储布局、读取布局、对齐、锁定基准、标记和其他比较选项"));
    auto *morePanel = new QWidget(more);
    auto *moreLay = new QVBoxLayout(morePanel);
    moreLay->setContentsMargins(8, 8, 8, 8);
    moreLay->setSpacing(6);
    park(moreLay, m_savePresetBtn, true);
    park(moreLay, m_loadPresetBtn, true);
    park(moreLay, m_autoAlignChk, true);
    park(moreLay, m_focusBtn, true);
    park(moreLay, m_focusLabel, true);
    park(moreLay, m_clearLinksBtn, true);
    park(moreLay, m_linkInfoLabel, true);
    park(moreLay, m_paneHistOverlayChk, true);
    park(moreLay, m_filenameOverlayChk, true);
    park(moreLay, m_swapBtn, true);
    park(moreLay, m_diffOverlayChk, true);
    park(moreLay, m_frameLabel, false);
    park(moreLay, m_frameSpin, false);
    if (more->menu())
    {
        auto *moreAction = new QWidgetAction(more);
        moreAction->setDefaultWidget(morePanel);
        more->menu()->addAction(moreAction);
        keepFindable(more->menu(), morePanel);
        showWithMenu(more->menu(), morePanel);
    }
    moreButton = more;
}

void CompareWorkspace::regroupCompareChrome()
{
    QHBoxLayout *modeLay = barLayout(this, "compareModeToolbar");
    QHBoxLayout *viewLay = barLayout(this, "compareViewToolbar");
    QHBoxLayout *toolLay = barLayout(this, "compareToolToolbar");
    QHBoxLayout *navLay = barLayout(this, "compareNavTail");
    QWidget *viewBar = findChild<QWidget *>(QStringLiteral("compareViewToolbar"));
    QWidget *toolBar = findChild<QWidget *>(QStringLiteral("compareToolToolbar"));
    if (!modeLay || !viewLay || !toolLay || !navLay || !viewBar || !toolBar)
        return;

    QWidget *syncButton = nullptr;
    QWidget *adjustButton = nullptr;
    QWidget *moreButton = nullptr;
    installCompareMenus(viewBar, toolBar, syncButton, adjustButton, moreButton);

    QLabel *layoutLabel = nullptr;
    const auto labels = viewBar->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
    for (QLabel *label : labels)
    {
        if (label && label->text().startsWith(tr("布局")))
            layoutLabel = label;
    }

    clearLayout(modeLay);
    clearLayout(viewLay);
    clearLayout(toolLay);
    clearLayout(navLay);

    const QList<QCheckBox *> modes = {m_blinkChk,   m_splitChk,   m_swipeChk,
                                      m_overlayChk, m_checkerChk, m_diffHighlightChk};
    for (QCheckBox *box : modes)
    {
        styleSegment(box);
        place(modeLay, box, true);
    }
    placeKept(modeLay, m_overlayAlphaSlider);
    placeKept(modeLay, m_overlayAlphaLabel);
    placeKept(modeLay, m_checkerSizeSlider);
    placeKept(modeLay, m_checkerSizeLabel);
    placeKept(modeLay, findChild<QLabel *>(QStringLiteral("diffThresholdCaption")));
    placeKept(modeLay, m_thresholdSlider);
    placeKept(modeLay, m_thresholdLabel);
    placeKept(modeLay, m_autoThresholdBtn);
    placeKept(modeLay, findChild<QLabel *>(QStringLiteral("diffGainCaption")));
    placeKept(modeLay, m_diffGainCombo);

    if (m_layoutCombo)
    {
        m_layoutCombo->setToolTip(tr("布局：自动 / 1×N / 2×2 …（Ctrl+2 / Ctrl+4 / Ctrl+8）"));
        m_layoutCombo->setFixedHeight(28);
    }
    place(viewLay, syncButton, true);
    place(viewLay, findChild<QPushButton *>(QStringLiteral("fitWindowButton")), true);
    place(viewLay, layoutLabel, true);
    place(viewLay, m_layoutCombo, true);
    placeKept(viewLay, findChild<QLabel *>(QStringLiteral("compareColumnsCaption")));
    placeKept(viewLay, m_gridColsSpin);
    place(viewLay, m_layoutStatusLabel, true);
    place(viewLay, m_channelCombo, true);
    place(viewLay, adjustButton, true);

    place(toolLay, findChild<QToolButton *>(QStringLiteral("compareSnapshotButton")), true);
    place(toolLay, m_analyzeBtn, true);
    place(toolLay, m_exportReportBtn, true);
    place(toolLay, moreButton, true);
    place(toolLay, m_sideChk, true);

    place(navLay, m_prevPairBtn, true);
    place(navLay, m_nextPairBtn, true);
    place(navLay, m_temporaryCompareButton, true);
    place(navLay, m_exitBtn, true);
    if (m_exitBtn)
    {
        m_exitBtn->setProperty("primary", true);
        if (m_exitBtn->style())
        {
            m_exitBtn->style()->unpolish(m_exitBtn);
            m_exitBtn->style()->polish(m_exitBtn);
        }
    }
    syncContextualCompareControls();
}
