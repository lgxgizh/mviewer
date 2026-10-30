#include "ThemeMetrics.h"
#include "compareworkspace_p.h"

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace
{

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
        widget->setFixedHeight(mviewer::ui::metrics::kControlHeight);
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

void repolish(QWidget *widget)
{
    if (!widget)
        return;
    if (QStyle *style = widget->style())
    {
        style->unpolish(widget);
        style->polish(widget);
    }
}

// Inner height of a segment inside the kControlHeight track.
constexpr int kSegmentInset = 2;

void styleSegment(QCheckBox *box)
{
    if (!box)
        return;
    box->setCursor(Qt::PointingHandCursor);
    box->setFixedHeight(mviewer::ui::metrics::kControlHeight - kSegmentInset * 2);
    box->setProperty("segmented", true);
    repolish(box);
}

// Joined macOS-style segmented track for the compare modes. The checkboxes stay
// the controls (shortcuts, exclusivity and tests use them); only their parent
// and look change.
QWidget *makeModeSegment(QWidget *parent)
{
    auto *segment = new QWidget(parent);
    segment->setObjectName(QStringLiteral("compareModeSegment"));
    segment->setAttribute(Qt::WA_StyledBackground, true);
    segment->setFixedHeight(mviewer::ui::metrics::kControlHeight);
    segment->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(segment);
    layout->setContentsMargins(kSegmentInset, kSegmentInset, kSegmentInset, kSegmentInset);
    layout->setSpacing(0);
    return segment;
}

void makeCapsule(QToolButton *button)
{
    if (!button)
        return;
    button->setProperty("pill", true);
    button->setProperty("capsule", true);
    button->setCursor(Qt::PointingHandCursor);
    repolish(button);
}

// One toolbar band. Wide windows get a single row
// [modes][view]  ...  [tools][nav]; narrower ones keep the two-row split so no
// control is clipped. Bars keep their objectNames and parents' layouts; only
// the row they sit in changes, and only when the band width crosses the fit
// point (with hysteresis), so mode toggles never resize the canvas.
class CompareToolbarBand final : public QWidget
{
  public:
    explicit CompareToolbarBand(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("compareToolbarBand"));
        setAttribute(Qt::WA_StyledBackground, true);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4,
                                 mviewer::ui::metrics::kSpace8, mviewer::ui::metrics::kSpace4);
        root->setSpacing(mviewer::ui::metrics::kSpace4);

        m_row1 = new QWidget(this);
        m_row1Lay = new QHBoxLayout(m_row1);
        m_row1Lay->setContentsMargins(0, 0, 0, 0);
        m_row1Lay->setSpacing(kGap);
        m_mode = makeBar(m_row1, "compareModeToolbar");
        m_nav = makeBar(m_row1, "compareNavTail");
        m_row1Lay->addWidget(m_mode);
        m_row1Lay->addStretch(1);
        m_row1Lay->addWidget(m_nav);

        m_row2 = new QWidget(this);
        m_row2Lay = new QHBoxLayout(m_row2);
        m_row2Lay->setContentsMargins(0, 0, 0, 0);
        m_row2Lay->setSpacing(kGap);
        m_view = makeBar(m_row2, "compareViewToolbar");
        m_tool = makeBar(m_row2, "compareToolToolbar");
        m_row2Lay->addWidget(m_view);
        m_row2Lay->addStretch(1);
        m_row2Lay->addWidget(m_tool);

        root->addWidget(m_row1);
        root->addWidget(m_row2);
    }

    QWidget *modeBar() const
    {
        return m_mode;
    }
    QWidget *viewBar() const
    {
        return m_view;
    }
    QWidget *toolBar() const
    {
        return m_tool;
    }
    bool singleRow() const
    {
        return m_single;
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        reflow();
    }

    bool event(QEvent *event) override
    {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::LayoutRequest && !m_pending)
        {
            m_pending = true;
            QTimer::singleShot(0, this,
                               [this]()
                               {
                                   m_pending = false;
                                   reflow();
                               });
        }
        return handled;
    }

  private:
    static constexpr int kGap = 8;
    static constexpr int kHysteresis = 32;

    int oneRowWidth() const
    {
        int need = 0;
        int bars = 0;
        for (const QWidget *bar : {m_mode, m_view, m_tool, m_nav})
        {
            if (bar && !bar->isHidden())
            {
                need += bar->sizeHint().width();
                ++bars;
            }
        }
        const QMargins margins = layout() ? layout()->contentsMargins() : QMargins();
        return need + kGap * bars + kGap * 2 + margins.left() + margins.right();
    }

    void reflow()
    {
        if (!m_view || !m_tool || width() <= 0)
            return;
        const int need = oneRowWidth();
        const bool single = m_single ? need <= width() : need + kHysteresis <= width();
        if (single == m_single)
            return;
        m_single = single;
        lift(m_view);
        lift(m_tool);
        if (single)
        {
            // [mode][view] stretch [tool][nav]
            m_row1Lay->insertWidget(1, m_view);
            m_row1Lay->insertWidget(m_row1Lay->indexOf(m_nav), m_tool);
            m_row2->hide();
        }
        else
        {
            m_row2Lay->insertWidget(0, m_view);
            m_row2Lay->addWidget(m_tool);
            m_row2->show();
        }
        m_view->show();
        m_tool->show();
    }

    QWidget *m_row1 = nullptr;
    QWidget *m_row2 = nullptr;
    QHBoxLayout *m_row1Lay = nullptr;
    QHBoxLayout *m_row2Lay = nullptr;
    QWidget *m_mode = nullptr;
    QWidget *m_view = nullptr;
    QWidget *m_tool = nullptr;
    QWidget *m_nav = nullptr;
    bool m_single = false;
    bool m_pending = false;
};

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
    button->setFixedHeight(mviewer::ui::metrics::kControlHeight);
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
    auto *band = new CompareToolbarBand(this);
    band->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    modeLayout = qobject_cast<QHBoxLayout *>(band->modeBar()->layout());
    viewLayout = qobject_cast<QHBoxLayout *>(band->viewBar()->layout());
    toolLayout = qobject_cast<QHBoxLayout *>(band->toolBar()->layout());
    toolActionsLayout = toolLayout;
    return band;
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
    if (!m_metricLabel->property("tabularDigits").toBool())
    {
        // System UI font with tabular figures so PSNR/SSIM digits do not jitter
        // as values change (replaces the old monospace accent readout).
        QFont font = m_metricLabel->font();
        font.setPixelSize(mviewer::ui::metrics::kFontCaptionPx);
        font.setWeight(QFont::DemiBold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        font.setFeature(QFont::Tag("tnum"), 1);
#endif
        m_metricLabel->setFont(font);
        m_metricLabel->setProperty("tabularDigits", true);
    }
    m_metricLabel->setToolTip(text);
    m_metricLabel->setText(chip);
}

void CompareWorkspace::installCompareMenus(QWidget *viewBar, QWidget *toolBar, QWidget *&syncButton,
                                           QWidget *&adjustButton, QWidget *&moreButton)
{
    auto *sync = makePopup(viewBar, "compareSyncButton", tr("Sync"),
                           tr("Sync zoom (Z), sync drag (D), sync rotate (Alt+R), sync crosshair"
                              " (R), pixel link (L), Uniform Pixel Scale"));
    bindMirror(sync->menu(), m_syncZoomChk, "syncZoomAction");
    bindMirror(sync->menu(), m_syncDragChk, "syncDragAction");
    bindMirror(sync->menu(), m_syncRotateChk, "syncRotateAction");
    bindMirror(sync->menu(), m_crosshairChk, "syncCrosshairAction");
    bindMirror(sync->menu(), m_pixelLinkChk, "pixelLinkAction");
    bindMirror(sync->menu(), m_uniformScaleChk, "uniformScaleAction");
    syncButton = sync;

    auto *adjust = makePopup(viewBar, "compareAdjustButton", tr("Adjust"),
                             tr("Brightness, contrast, gamma, gain, USM sharpening, rotation and "
                                "flip (preview only, does not overwrite the original file);"
                                " adjusted result can be exported"));
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

    auto *more = makePopup(toolBar, "compareMoreButton", tr("⋯ More"),
                           tr("Save layout, load layout, align, lock reference, markers, and other "
                              "comparison options"));
    auto *morePanel = new QWidget(more);
    auto *moreLay = new QVBoxLayout(morePanel);
    moreLay->setContentsMargins(8, 8, 8, 8);
    moreLay->setSpacing(mviewer::ui::metrics::kSpace8);
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
        if (label && label->text().startsWith(tr("Layout")))
            layoutLabel = label;
    }

    clearLayout(modeLay);
    clearLayout(viewLay);
    clearLayout(toolLay);
    clearLayout(navLay);

    const QList<QCheckBox *> modes = {m_blinkChk,   m_splitChk,   m_swipeChk,
                                      m_overlayChk, m_checkerChk, m_diffHighlightChk};
    QWidget *modeBar = findChild<QWidget *>(QStringLiteral("compareModeToolbar"));
    QWidget *segment = makeModeSegment(modeBar);
    auto *segmentLay = qobject_cast<QHBoxLayout *>(segment->layout());
    for (QCheckBox *box : modes)
    {
        if (!box)
            continue;
        lift(box);
        styleSegment(box);
        segmentLay->addWidget(box);
        box->setVisible(true);
    }
    modeLay->addWidget(segment);
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
        m_layoutCombo->setToolTip(tr("Layout: Auto / 1×N / 2×2 … (Ctrl+2 / Ctrl+4 / Ctrl+8)"));
        m_layoutCombo->setFixedHeight(mviewer::ui::metrics::kControlHeight);
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

    makeCapsule(qobject_cast<QToolButton *>(syncButton));
    makeCapsule(qobject_cast<QToolButton *>(adjustButton));
    makeCapsule(qobject_cast<QToolButton *>(moreButton));
    makeCapsule(findChild<QToolButton *>(QStringLiteral("compareSnapshotButton")));

    place(navLay, m_temporaryCompareButton, true);
    place(navLay, m_exitBtn, true);
    if (m_exitBtn)
    {
        m_exitBtn->setProperty("primary", true);
        repolish(m_exitBtn);
    }
    syncContextualCompareControls();
}
