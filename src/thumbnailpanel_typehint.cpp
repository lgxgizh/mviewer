// ThumbnailPanel browse type filter: in-place re-partitioning of the scanned
// listing when the hidden format groups change, and the empty-folder hint that
// names files hidden only by the type filter ("3 RAW files hidden ... [Show]").
#include "browseformats.h"
#include "selectionmodel.h"
#include "thumbnailpanel_p.h"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QPair>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
// Centered card over the gallery viewport. Not part of any layout; it follows
// the viewport's size through an event filter.
class TypeHiddenHint final : public QFrame
{
  public:
    explicit TypeHiddenHint(QWidget *viewport) : QFrame(viewport)
    {
        setObjectName(QStringLiteral("typeHiddenHint"));
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(16, 12, 16, 12);
        lay->setSpacing(8);
        m_label = new QLabel(this);
        m_label->setObjectName(QStringLiteral("typeHiddenHintLabel"));
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setWordWrap(true);
        lay->addWidget(m_label);
        m_button = new QPushButton(this);
        m_button->setObjectName(QStringLiteral("typeHiddenHintShow"));
        lay->addWidget(m_button, 0, Qt::AlignHCenter);
        viewport->installEventFilter(this);
    }

    QPushButton *button() const
    {
        return m_button;
    }

    void setTexts(const QString &label, const QString &button)
    {
        m_label->setText(label);
        m_button->setText(button);
        relayout();
    }

    void relayout()
    {
        const QWidget *host = parentWidget();
        if (!host)
            return;
        const int w = qBound(160, host->width() - 24, 380);
        setFixedWidth(w);
        adjustSize();
        move(qMax(0, (host->width() - width()) / 2), qMax(0, (host->height() - height()) / 3));
        raise();
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show))
            relayout();
        return QFrame::eventFilter(watched, event);
    }

  private:
    QLabel *m_label = nullptr;
    QPushButton *m_button = nullptr;
};

// Scroll anchor candidates, best first: the current item when it is on
// screen, then the visible rows from the top. The first one that survives the
// type change pins the viewport, so hiding rows above it does not move it.
QList<QPair<QString, int>> typeFilterAnchors(const QListView *view, const QStringList &paths,
                                             const QString &firstPath, int firstOffset,
                                             bool horizontal)
{
    QList<QPair<QString, int>> out;
    const QRect port = view->viewport()->rect();
    auto offsetOf = [&](const QRect &r)
    { return horizontal ? r.left() - port.left() : r.top() - port.top(); };
    const QModelIndex current = view->currentIndex();
    if (current.isValid() && current.row() < paths.size())
    {
        const QRect r = view->visualRect(current);
        if (r.isValid() && r.intersects(port))
            out.append(qMakePair(paths.at(current.row()), offsetOf(r)));
    }
    if (firstPath.isEmpty())
        return out;
    out.append(qMakePair(firstPath, firstOffset));
    const int first = static_cast<int>(paths.indexOf(firstPath));
    if (first < 0)
        return out;
    for (int row = first + 1; row < paths.size() && row < first + 512; ++row)
    {
        const QRect r = view->visualRect(view->model()->index(row, 0));
        if (!r.isValid() || !r.intersects(port))
            break;
        out.append(qMakePair(paths.at(row), offsetOf(r)));
    }
    return out;
}
} // namespace

void ThumbnailPanel::adoptListing(QList<Entry> all)
{
    mviewer::browseformats::partitionHiddenEntries(all, m_hiddenFormatGroups, m_typeHiddenEntries);
    m_allEntries = std::move(all);
    m_sourceRowByPath.clear();
    m_sourceRowByPath.reserve(m_allEntries.size());
    for (int i = 0; i < m_allEntries.size(); ++i)
        m_sourceRowByPath.insert(galleryPathKey(m_allEntries.at(i).path), i);
}

void ThumbnailPanel::setHiddenFormatGroups(const QStringList &groups)
{
    if (m_hiddenFormatGroups == groups)
        return;
    m_hiddenFormatGroups = groups;
    if (m_currentDir.isEmpty())
        return;
    if (!m_scanComplete)
    {
        // A scan is still streaming rows filtered by the old set: restart it.
        // setDirectory() resets the text search; restore it.
        const QString text = m_filterText;
        const bool recursive = m_filterRecursive;
        refresh();
        if (!text.trimmed().isEmpty())
            setFilter(text, recursive);
        return;
    }
    // The completed listing (shown + type-hidden rows) is re-partitioned in
    // place. No rescan: the filter rebuild below keeps the selection, current
    // item and scroll anchor of every row that is still visible.
    const QStringList previousSelection = selectedPaths();
    const QString previousCurrent = currentIndex().isValid()
                                        ? m_paths.value(currentIndex().row())
                                        : (m_selection ? m_selection->currentImage() : QString());
    QString firstPath;
    int firstOffset = 0;
    captureScrollAnchor(firstPath, firstOffset);
    const bool horizontal = (m_viewMode == Filmstrip || m_viewMode == List);
    const auto anchors = firstPath.isEmpty()
                             ? QList<QPair<QString, int>>()
                             : typeFilterAnchors(this, m_paths, firstPath, firstOffset, horizontal);
    const int shownBefore = m_allEntries.size();
    QList<Entry> all = m_allEntries + m_typeHiddenEntries;
    sortDirectoryDeltaEntries(all);
    adoptListing(std::move(all));
    if (!m_recursiveSearching)
    {
        // Recursive hits were type-filtered when found; re-walk on demand.
        m_recursiveHits.clear();
        m_recursiveHitsFor.clear();
    }
    if (m_allEntries.size() > shownBefore)
    {
        // Re-shown rows have no probed dimensions yet.
        m_dimsResolved = false;
        if (m_viewMode == Details || m_sortMode == SortResolution)
            ensureDimensions();
    }
    m_incrementalApply = true;
    m_incrementalPrevSelection = previousSelection;
    m_incrementalPrevCurrent = previousCurrent;
    m_incrementalAnchorPath.clear();
    m_incrementalAnchorOffset = 0;
    for (const auto &anchor : anchors)
    {
        if (m_sourceRowByPath.contains(galleryPathKey(anchor.first)))
        {
            m_incrementalAnchorPath = anchor.first;
            m_incrementalAnchorOffset = anchor.second;
            break;
        }
    }
    m_incrementalPreviousCurrentRow = m_paths.indexOf(previousCurrent);
    applyFilter();
    updateTypeHiddenHint();
}

QStringList ThumbnailPanel::hiddenFormatGroups() const
{
    return m_hiddenFormatGroups;
}

int ThumbnailPanel::typeHiddenCount() const
{
    return static_cast<int>(m_typeHiddenEntries.size());
}

QStringList ThumbnailPanel::typeHiddenGroups() const
{
    QStringList present;
    for (const Entry &e : m_typeHiddenEntries)
    {
        const int dot = e.path.lastIndexOf(QLatin1Char('.'));
        const QString group =
            mviewer::browseformats::groupForSuffix(dot >= 0 ? e.path.mid(dot + 1) : QString());
        if (!present.contains(group))
            present.append(group);
    }
    // Menu order, so the hint text is stable.
    QStringList ordered;
    for (const auto &g : mviewer::browseformats::groups())
    {
        const QString id = QString::fromLatin1(g.id);
        if (present.contains(id))
            ordered.append(id);
    }
    return ordered;
}

void ThumbnailPanel::updateTypeHiddenHint()
{
    // Only when the folder has nothing to show except type-hidden files; other
    // filters (text, rating, ...) keep the generic empty-folder card.
    const bool show = m_scanComplete && !m_directoryUnavailable && !m_currentDir.isEmpty() &&
                      m_allEntries.isEmpty() && !m_typeHiddenEntries.isEmpty();
    auto *hint = static_cast<TypeHiddenHint *>(m_typeHiddenHint);
    if (!show)
    {
        if (hint)
            hint->hide();
        return;
    }
    if (!hint)
    {
        hint = new TypeHiddenHint(viewport());
        m_typeHiddenHint = hint;
        // An explicit click is the user's choice; MainWindow persists it.
        connect(hint->button(), &QPushButton::clicked, this,
                [this] { emit showHiddenFormatGroupsRequested(typeHiddenGroups()); });
    }
    const QStringList ids = typeHiddenGroups();
    QStringList labels;
    for (const QString &id : ids)
        labels.append(mviewer::browseformats::groupLabel(id));
    const int n = typeHiddenCount();
    QString text;
    QString button;
    if (labels.size() == 1)
    {
        text = tr("%n %1 file(s) hidden by the type filter", nullptr, n).arg(labels.first());
        button = tr("Show %1 files").arg(labels.first());
    }
    else
    {
        text = tr("%n file(s) hidden by the type filter (%1)", nullptr, n)
                   .arg(labels.join(QStringLiteral(", ")));
        button = tr("Show these types");
    }
    hint->setTexts(text, button);
    hint->show();
    hint->relayout();
}
