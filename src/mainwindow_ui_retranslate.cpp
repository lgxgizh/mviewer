// Browse-bar combo labels. Construction and language switches share these
// tables so item data and the current selection stay put while the text changes.
#include "mainwindow_p.h"

#include "core/image/ImageFormats.h"

#include <QComboBox>
#include <QSignalBlocker>

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

void MainWindow::applyTypeFilterItems(QComboBox *combo)
{
    if (!combo)
        return;
    const QString raw = QString::fromStdString(mviewer::core::rawTypeFilterData());
    struct Item
    {
        const char *text;
        QString data;
    };
    const Item items[] = {
        {QT_TR_NOOP("All types"), QString()},
        {QT_TR_NOOP("JPG"), QStringLiteral("jpg,jpeg")},
        {QT_TR_NOOP("PNG"), QStringLiteral("png")},
        {QT_TR_NOOP("TIFF"), QStringLiteral("tif,tiff")},
        {QT_TR_NOOP("WebP"), QStringLiteral("webp")},
        {QT_TR_NOOP("RAW"), raw},
    };
    const QVariant current = combo->count() > 0 ? combo->currentData() : QVariant();
    const bool fill = combo->count() == 0;
    const QSignalBlocker blocker(combo);
    if (fill)
    {
        for (const Item &item : items)
            combo->addItem(tr(item.text), item.data);
        return;
    }
    for (int i = 0; i < int(std::size(items)) && i < combo->count(); ++i)
        combo->setItemText(i, tr(items[i].text));
    const int idx = combo->findData(current);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
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
