// Browse-bar combo labels. Construction and language switches share these
// tables so item data and the current selection stay put while the text changes.
#include "mainwindow_p.h"

#include "core/image/ImageFormats.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

#include <functional>
#include <iterator>

namespace
{

struct IntItem
{
    const char *text;
    int data;
};

void applyIntItems(QComboBox *combo, const IntItem *items, int count,
                   const std::function<QString(const char *)> &translate)
{
    if (!combo || !items || count <= 0)
        return;
    const QVariant current = combo->count() > 0 ? combo->currentData() : QVariant();
    const bool fill = combo->count() == 0;
    const QSignalBlocker blocker(combo);
    if (fill)
    {
        for (int i = 0; i < count; ++i)
            combo->addItem(translate(items[i].text), items[i].data);
        return;
    }
    for (int i = 0; i < count && i < combo->count(); ++i)
        combo->setItemText(i, translate(items[i].text));
    const int idx = combo->findData(current);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
}

} // namespace

void MainWindow::applySortComboItems(QComboBox *combo)
{
    static const IntItem kItems[] = {
        {QT_TR_NOOP("File Name"), ThumbnailPanel::SortName},
        {QT_TR_NOOP("Date"), ThumbnailPanel::SortDate},
        {QT_TR_NOOP("Size"), ThumbnailPanel::SortSize},
        {QT_TR_NOOP("Resolution"), ThumbnailPanel::SortResolution},
        {QT_TR_NOOP("Type"), ThumbnailPanel::SortType},
        {QT_TR_NOOP("Rating"), ThumbnailPanel::SortRating},
        {QT_TR_NOOP("Camera"), ThumbnailPanel::SortCamera},
        {QT_TR_NOOP("Lens"), ThumbnailPanel::SortLens},
    };
    applyIntItems(combo, kItems, int(std::size(kItems)),
                  [this](const char *text) { return tr(text); });
}

void MainWindow::applyViewModeComboItems(QComboBox *combo)
{
    static const IntItem kItems[] = {
        {QT_TR_NOOP("Grid (Ctrl+1)"), ThumbnailPanel::Thumbnail},
        {QT_TR_NOOP("Large icons"), ThumbnailPanel::LargeIcon},
        {QT_TR_NOOP("Small icons (Ctrl+5)"), ThumbnailPanel::SmallIcon},
        {QT_TR_NOOP("List (Ctrl+2)"), ThumbnailPanel::List},
        {QT_TR_NOOP("Details (Ctrl+3)"), ThumbnailPanel::Details},
        {QT_TR_NOOP("Filmstrip (Ctrl+4)"), ThumbnailPanel::Filmstrip},
        {QT_TR_NOOP("Compact (Ctrl+6)"), ThumbnailPanel::Compact},
    };
    applyIntItems(combo, kItems, int(std::size(kItems)),
                  [this](const char *text) { return tr(text); });
}

void MainWindow::applyRatingFilterItems(QComboBox *combo)
{
    static const IntItem kItems[] = {
        {QT_TR_NOOP("All"), 0},
        {QT_TR_NOOP("★ and above"), 1},
        {QT_TR_NOOP("★★ and above"), 2},
        {QT_TR_NOOP("★★★ and above"), 3},
        {QT_TR_NOOP("★★★★ and above"), 4},
        {QT_TR_NOOP("★★★★★"), 5},
    };
    applyIntItems(combo, kItems, int(std::size(kItems)),
                  [this](const char *text) { return tr(text); });
}

void MainWindow::applyFlagFilterItems(QComboBox *combo)
{
    static const IntItem kItems[] = {
        {QT_TR_NOOP("All"), 0},           {QT_TR_NOOP("Favorited"), 1},
        {QT_TR_NOOP("Rejected"), 2},      {QT_TR_NOOP("Recently viewed"), 3},
        {QT_TR_NOOP("Red label"), 11},    {QT_TR_NOOP("Orange label"), 12},
        {QT_TR_NOOP("Yellow label"), 13}, {QT_TR_NOOP("Green label"), 14},
        {QT_TR_NOOP("Blue label"), 15},   {QT_TR_NOOP("Purple label"), 16},
    };
    applyIntItems(combo, kItems, int(std::size(kItems)),
                  [this](const char *text) { return tr(text); });
}

QString MainWindow::sortDirectionToolTip(bool detailed) const
{
    if (detailed)
    {
        return tr("↑ Ascending: A→Z (numbers by value), earliest to latest, smallest to "
                  "largest; ↓ the opposite. Identical items still sort by filename");
    }
    return tr("↑ Ascending: A→Z, earliest to latest, smallest to largest; ↓ the opposite");
}

// P1-5: widgets built once in the constructor keep their first tr() text, so a
// language switch refreshes them here. Combo items keep their data and the
// current choice; only the visible text changes.
void MainWindow::retranslateBrowseBar()
{
    struct TextEntry
    {
        const char *objectName;
        const char *text;
        const char *toolTip;
    };
    static const TextEntry kNamed[] = {
        {"sortFieldLabel", QT_TR_NOOP("Sort:"), nullptr},
        {"secondarySortLabel", QT_TR_NOOP("Sort:"), nullptr},
        {"ratingFilterLabel", QT_TR_NOOP("Rating:"), nullptr},
        {"flagFilterLabel", QT_TR_NOOP("Flag:"), nullptr},
        {"advancedFilterToggle", QT_TR_NOOP("Advanced Filter"),
         QT_TR_NOOP("Show camera, lens, rating, and metadata filters")},
        {"clearFiltersButton", QT_TR_NOOP("Clear Filters"), nullptr},
        {"resetFiltersButton", QT_TR_NOOP("Reset Filters"),
         QT_TR_NOOP("Clear all search and filter conditions")},
    };
    for (const TextEntry &entry : kNamed)
    {
        auto *widget = findChild<QWidget *>(QLatin1String(entry.objectName));
        if (!widget)
            continue;
        if (auto *label = qobject_cast<QLabel *>(widget))
            label->setText(tr(entry.text));
        else if (auto *button = qobject_cast<QAbstractButton *>(widget))
            button->setText(tr(entry.text));
        if (entry.toolTip)
            widget->setToolTip(tr(entry.toolTip));
    }

    struct EditEntry
    {
        const char *objectName;
        const char *placeholder;
        const char *toolTip;
    };
    static const EditEntry kEdits[] = {
        {"cameraFilterEdit", QT_TR_NOOP("Camera"),
         QT_TR_NOOP("Filter by camera (brand/model), substring match")},
        {"lensFilterEdit", QT_TR_NOOP("Lens"), QT_TR_NOOP("Filter by lens model, substring match")},
        {"tagFilterEdit", QT_TR_NOOP("Label"), QT_TR_NOOP("Filter by tag exactly (empty = all)")},
    };
    for (const EditEntry &entry : kEdits)
    {
        if (auto *edit = findChild<QLineEdit *>(QLatin1String(entry.objectName)))
        {
            edit->setPlaceholderText(tr(entry.placeholder));
            edit->setToolTip(tr(entry.toolTip));
        }
    }
    if (auto *iso = findChild<QSpinBox *>(QStringLiteral("isoFilterSpin")))
    {
        iso->setSpecialValueText(tr("ISO"));
        iso->setToolTip(tr("Filter by exact ISO (0 = all)"));
    }

    applySortComboItems(m_sortCombo);
    applySortComboItems(m_secondarySortCombo);
    applyViewModeComboItems(m_viewModeCombo);
    applyRatingFilterItems(m_ratingFilter);
    applyFlagFilterItems(m_flagFilter);
    for (FormatFilterCombo *combo : {m_typeFilterCombo, m_secondaryTypeFilterCombo})
    {
        if (combo)
            combo->retranslate();
    }
    if (m_viewModeCombo)
        m_viewModeCombo->setToolTip(tr("Switch thumbnail view mode (common modes Ctrl+1..6)"));
    syncViewModeSegmentToolTips();
    if (m_sortDirBtn)
        m_sortDirBtn->setToolTip(sortDirectionToolTip(true));
    if (m_secondarySortDirBtn)
        m_secondarySortDirBtn->setToolTip(sortDirectionToolTip(false));
    if (m_searchEdit)
        m_searchEdit->setPlaceholderText(tr("Filter by filename..."));
    if (m_searchRecursive)
        m_searchRecursive->setText(tr("Include Subfolders"));
    if (m_searchMeta)
        m_searchMeta->setText(tr("Metadata"));
    if (m_pathEdit)
    {
        m_pathEdit->setPlaceholderText(tr("Enter a directory path and press Enter to switch..."));
        m_pathEdit->setToolTip(
            tr("Type or paste a directory path and press Enter to open it (shortcut: "
               "Ctrl+F / Alt+D to focus)."));
    }
    if (m_favoritesBar)
        m_favoritesBar->setToolTip(
            tr("Favorites — Right-click to remove, Ctrl+D to favorite the current folder"));
    retranslateSplitHeader();
}

void MainWindow::retranslateSplitHeader()
{
    struct ButtonEntry
    {
        QPushButton *button;
        const char *text;
        const char *toolTip;
    };
    const ButtonEntry buttons[] = {
        {m_secondaryBrowseBtn, QT_TR_NOOP("Browse..."), nullptr},
        {m_secondaryCloneBtn, QT_TR_NOOP("Clone Left"),
         QT_TR_NOOP("Sync right pane to the left pane directory")},
        {m_secondarySwapBtn, QT_TR_NOOP("Swap"),
         QT_TR_NOOP("Swap left and right pane directories and selections")},
        {m_secondaryCloseBtn, nullptr, QT_TR_NOOP("Close Split Browse View")},
    };
    for (const ButtonEntry &entry : buttons)
    {
        if (!entry.button)
            continue;
        if (entry.text)
            entry.button->setText(tr(entry.text));
        if (entry.toolTip)
            entry.button->setToolTip(tr(entry.toolTip));
    }
    if (m_secondaryCloseBtn)
        m_secondaryCloseBtn->setAccessibleName(tr("Close Split Browse View"));
    if (m_secondaryPathEdit)
        m_secondaryPathEdit->setPlaceholderText(tr("Enter secondary directory path..."));
}
