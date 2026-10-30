// Unified browse toolbar: navigation, folder title, and the controls that used
// to sit on the gallery sort row (view segments, format filter, Compare, search).
#include "mainwindow_p.h"

#include "ShortcutHelper.h"
#include "ThemeIcons.h"
#include "ThemeMetrics.h"
#include "widgets/ViewModeSegment.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace
{

// Keeps the trailing cluster (view segment, filter, Compare, search) pinned to
// the right edge of the toolbar. QToolBar overflows items from the end into its
// extension menu, so hosting the cluster as ordinary items let the search field
// vanish on narrower windows. The bar reserves the cluster width as a right
// contents margin; only the action buttons can overflow.
class TrailingClusterPin final : public QObject
{
  public:
    TrailingClusterPin(QToolBar *bar, QWidget *cluster)
        : QObject(bar), m_bar(bar), m_cluster(cluster)
    {
        bar->installEventFilter(this);
        cluster->installEventFilter(this);
    }

    void place()
    {
        if (!m_bar || !m_cluster)
            return;
        const QSize hint = m_cluster->sizeHint();
        constexpr int kEdge = 8;
        const int reserve = hint.width() + kEdge;
        if (m_bar->contentsMargins() != QMargins(0, 0, reserve, 0))
            m_bar->setContentsMargins(0, 0, reserve, 0);
        const int y = qMax(0, (m_bar->height() - hint.height()) / 2);
        m_cluster->setGeometry(m_bar->width() - reserve, y, hint.width(), hint.height());
        m_cluster->raise();
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::Resize || type == QEvent::Show || type == QEvent::LayoutRequest ||
            type == QEvent::StyleChange || type == QEvent::FontChange)
        {
            if (watched == m_bar || watched == m_cluster)
                place();
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    QToolBar *m_bar;
    QWidget *m_cluster;
};

// Folder title and path share one narrow slot. sizeHint follows the full
// string (clamped); the painted text elides so a long path cannot widen the bar.
class ElidingLabel final : public QLabel
{
  public:
    ElidingLabel(Qt::TextElideMode mode, QWidget *parent) : QLabel(parent), m_mode(mode)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    void setFullText(const QString &text)
    {
        if (m_full == text)
            return;
        m_full = text;
        updateGeometry();
        updateElide();
    }

    QSize sizeHint() const override
    {
        const int textW = fontMetrics().horizontalAdvance(m_full);
        return QSize(qBound(72, textW + 8, 220), QLabel::sizeHint().height());
    }

    QSize minimumSizeHint() const override
    {
        return QSize(48, QLabel::minimumSizeHint().height());
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElide();
    }

  private:
    void updateElide()
    {
        const int width = contentsRect().width();
        const QString shown = width <= 0 ? m_full : fontMetrics().elidedText(m_full, m_mode, width);
        if (text() != shown)
            QLabel::setText(shown);
    }

    QString m_full;
    Qt::TextElideMode m_mode = Qt::ElideRight;
};

class ClickToEdit final : public QObject
{
  public:
    ClickToEdit(QObject *parent, std::function<void()> handler)
        : QObject(parent), m_handler(std::move(handler))
    {
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            const auto *mouse = static_cast<const QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && m_handler)
                m_handler();
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    std::function<void()> m_handler;
};

QString actTip(const QString &label, const QAction *action)
{
    return mviewer::ui::appendShortcutHint(label, mviewer::ui::actionShortcutHint(action));
}

QString folderDisplayName(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString name = QFileInfo(path).fileName();
    if (!name.isEmpty())
        return name;
    const QString native = QDir::toNativeSeparators(QDir(path).absolutePath());
    return native.isEmpty() ? path : native;
}

void addBrowserAction(QToolBar *bar, QAction *action, const char *iconName, const QString &tip)
{
    if (!bar || !action)
        return;
    action->setIcon(mviewer::ui::toolbarIcon(iconName));
    if (!tip.isEmpty())
        action->setToolTip(tip);
    bar->addAction(action);
}

void addGap(QToolBar *bar)
{
    auto *gap = new QWidget(bar);
    gap->setFixedWidth(mviewer::ui::metrics::kSpace8);
    gap->setAttribute(Qt::WA_TransparentForMouseEvents);
    bar->addWidget(gap);
}

ViewModeSegment::Mode segmentMode(int data, const QIcon &icon, const QList<int> &matches)
{
    ViewModeSegment::Mode mode;
    mode.data = data;
    mode.icon = icon;
    mode.matches = matches;
    return mode;
}

void detachFromLayout(QWidget *widget)
{
    if (!widget)
        return;
    if (QWidget *parent = widget->parentWidget())
    {
        if (QLayout *layout = parent->layout())
            layout->removeWidget(widget);
    }
}

} // namespace

QWidget *MainWindow::makeBrowserLocation(QWidget *parent)
{
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("browserLocation"));
    host->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // kBarHeight includes the toolbar hairline, so the title block sits inside it.
    host->setFixedHeight(mviewer::ui::metrics::kBarHeight - 4);
    host->setMinimumWidth(72);
    host->setMaximumWidth(280);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(mviewer::ui::metrics::kSpace8, 1, mviewer::ui::metrics::kSpace8, 1);
    layout->setSpacing(0);

    auto *title = new ElidingLabel(Qt::ElideRight, host);
    title->setObjectName(QStringLiteral("folderTitleLabel"));
    title->setProperty("role", QStringLiteral("title"));
    title->setFullText(tr("MViewer"));
    title->setToolTip(tr("Click to edit the folder path"));
    title->setCursor(Qt::PointingHandCursor);

    auto *caption = new ElidingLabel(Qt::ElideMiddle, host);
    caption->setObjectName(QStringLiteral("folderPathCaption"));
    caption->setProperty("role", QStringLiteral("caption"));
    caption->setToolTip(tr("Click to edit the folder path"));
    caption->setCursor(Qt::PointingHandCursor);

    auto *click = new ClickToEdit(host, [this]() { focusAddressBar(); });
    title->installEventFilter(click);
    caption->installEventFilter(click);
    layout->addWidget(title);
    layout->addWidget(caption);
    return host;
}

void MainWindow::addTrailingBrowserActions(QToolBar *bar)
{
    addBrowserAction(bar, m_actOpenDir, "open", actTip(tr("Open Directory"), m_actOpenDir));
    addBrowserAction(bar, m_actRefresh, "refresh",
                     actTip(tr("Refresh directory and thumbnails"), m_actRefresh));
    addBrowserAction(
        bar, m_actOpenCurrentFolder, "browse",
        actTip(tr("Open the current folder in the file manager"), m_actOpenCurrentFolder));
    addGap(bar);
    addBrowserAction(bar, m_actAddFavorite, "favorite",
                     actTip(tr("Favorite Current Directory"), m_actAddFavorite));
    addBrowserAction(bar, m_actToggleAnalysis, "analysis",
                     actTip(tr("Analysis panel"), m_actToggleAnalysis));
    addBrowserAction(bar, m_actToggleSearch, "search",
                     actTip(tr("Global search"), m_actToggleSearch));
    addBrowserAction(bar, m_actBrowseWorkspace, "browse_workspace", {});
    addGap(bar);
    addBrowserAction(bar, m_actRotateCCW, "rotate_ccw", {});
    addBrowserAction(bar, m_actRotateCW, "rotate_cw", {});
    addBrowserAction(bar, m_actSplitBrowse, "split_browse",
                     actTip(tr("Split Browse View (Ctrl+Shift+S)"), m_actSplitBrowse));
    if (m_actUsmCompare)
        m_actUsmCompare->setObjectName(QStringLiteral("usmCompareAction"));
    addBrowserAction(
        bar, m_actUsmCompare, "usm",
        actTip(tr("USM sharpen before/after comparison and export (Ctrl+U)"), m_actUsmCompare));
}

void MainWindow::buildUnifiedToolbar()
{
    auto *bar = new QToolBar(tr("Browse toolbar"), this);
    addToolBar(Qt::TopToolBarArea, bar);
    bar->setObjectName(QStringLiteral("browserToolBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->setIconSize(QSize(mviewer::ui::kToolbarIconPx, mviewer::ui::kToolbarIconPx));
    bar->setFixedHeight(mviewer::ui::metrics::kBarHeight);

    addBrowserAction(bar, m_actDirBack, "back", actTip(tr("Previous directory"), m_actDirBack));
    addBrowserAction(bar, m_actDirForward, "forward",
                     actTip(tr("Next directory"), m_actDirForward));
    addBrowserAction(bar, m_actDirUp, "up", actTip(tr("Parent directory"), m_actDirUp));
    addGap(bar);
    bar->addWidget(makeBrowserLocation(bar));
    addGap(bar);
    addTrailingBrowserActions(bar);

    auto *spacer = new QWidget(bar);
    spacer->setObjectName(QStringLiteral("browserToolbarSpacer"));
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
    bar->addWidget(spacer);
    installNavigationCollapse();
}

void MainWindow::addComparePill(QWidget *cluster)
{
    if (!cluster || !m_actCompare)
        return;
    m_actCompare->setIcon(mviewer::ui::toolbarIcon("compare"));
    m_actCompare->setIconText(tr("Compare"));
    m_actCompare->setToolTip(actTip(tr("Select 2–8 images to compare"), m_actCompare));
    auto *button = new QToolButton(cluster);
    button->setObjectName(QStringLiteral("compareToolbarButton"));
    button->setDefaultAction(m_actCompare);
    button->setAutoRaise(false);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(mviewer::ui::kToolbarIconPx, mviewer::ui::kToolbarIconPx));
    button->setProperty("pill", true);
    button->setProperty("primary", true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(mviewer::ui::metrics::kControlHeight);
    cluster->layout()->addWidget(button);
}

void MainWindow::attachUnifiedToolbarControls()
{
    auto *bar = findChild<QToolBar *>(QStringLiteral("browserToolBar"));
    if (!bar || bar->property("unifiedControls").toBool())
        return;
    bar->setProperty("unifiedControls", true);
    auto *cluster = new QWidget(bar);
    cluster->setObjectName(QStringLiteral("browserToolbarTrailing"));
    auto *row = new QHBoxLayout(cluster);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    if (m_viewModeCombo)
    {
        detachFromLayout(m_viewModeCombo);
        m_viewModeCombo->setParent(bar);
        m_viewModeCombo->hide();
        auto *segment = new ViewModeSegment(cluster);
        segment->setObjectName(QStringLiteral("viewModeSegment"));
        segment->setProperty("segmented", true);
        segment->setFixedHeight(mviewer::ui::metrics::kControlHeight);
        using Mode = ThumbnailPanel::ViewMode;
        const auto icon = [](const char *id) { return mviewer::ui::toolbarIcon(id); };
        segment->setModes({
            segmentMode(int(Mode::Thumbnail), icon("view_grid"),
                        {int(Mode::LargeIcon), int(Mode::SmallIcon), int(Mode::Compact)}),
            segmentMode(int(Mode::List), icon("view_list"), {int(Mode::Details)}),
            segmentMode(int(Mode::Filmstrip), icon("view_filmstrip"), {}),
        });
        segment->bind(m_viewModeCombo);
        row->addWidget(segment);
        syncViewModeSegmentToolTips();
    }
    if (m_typeFilterCombo)
    {
        detachFromLayout(m_typeFilterCombo);
        m_typeFilterCombo->setParent(cluster);
        m_typeFilterCombo->setProperty("pill", true);
        m_typeFilterCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        m_typeFilterCombo->setMaximumWidth(168);
        row->addWidget(m_typeFilterCombo);
        m_typeFilterCombo->show();
    }
    addComparePill(cluster);
    if (m_searchEdit)
    {
        detachFromLayout(m_searchEdit);
        m_searchEdit->setParent(cluster);
        m_searchEdit->setFixedWidth(176);
        row->addWidget(m_searchEdit);
        m_searchEdit->show();
    }
    auto *pin = new TrailingClusterPin(bar, cluster);
    cluster->show();
    pin->place();
}

void MainWindow::syncViewModeSegmentToolTips()
{
    auto *segment = findChild<ViewModeSegment *>(QStringLiteral("viewModeSegment"));
    if (!segment || !m_viewModeCombo)
        return;
    const int primaries[] = {int(ThumbnailPanel::Thumbnail), int(ThumbnailPanel::List),
                             int(ThumbnailPanel::Filmstrip)};
    for (int data : primaries)
    {
        const int index = m_viewModeCombo->findData(data);
        if (index >= 0)
            segment->setModeToolTip(data, m_viewModeCombo->itemText(index));
    }
}

void MainWindow::refreshBrowserLocation(const QString &path)
{
    if (m_breadcrumb)
        m_breadcrumb->setPath(path);
    const QString native = QDir::toNativeSeparators(path);
    if (m_pathEdit)
        m_pathEdit->setText(native);
    const QString name = folderDisplayName(path);
    const QString click = tr("Click to edit the folder path");
    auto *title =
        dynamic_cast<ElidingLabel *>(findChild<QLabel *>(QStringLiteral("folderTitleLabel")));
    auto *caption =
        dynamic_cast<ElidingLabel *>(findChild<QLabel *>(QStringLiteral("folderPathCaption")));
    if (title)
    {
        title->setFullText(name.isEmpty() ? tr("MViewer") : name);
        title->setToolTip(name.isEmpty() ? click : QStringLiteral("%1\n%2").arg(name, click));
    }
    if (caption)
    {
        caption->setFullText(native);
        caption->setToolTip(native.isEmpty() ? click : QStringLiteral("%1\n%2").arg(native, click));
    }
}
