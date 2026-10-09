// Included twice from thumbnailpanel.h. ReadyPixmap is guarded. The nested
// delegates are defined only on the second include, after ThumbnailPanel is
// complete. Do not add #pragma once: it would skip that second include.

#ifndef MVIEWER_THUMBNAILPANEL_TYPES_H
#define MVIEWER_THUMBNAILPANEL_TYPES_H

#include <cstdint>
#include <list>

#include <QPixmap>
#include <QString>

namespace mviewer::tp
{

struct ReadyPixmap
{
    QPixmap pixmap;
    qint64 bytes = 0;
    uint64_t lastUse = 0;
    std::list<QString>::iterator lruIt;
};

} // namespace mviewer::tp

#endif // MVIEWER_THUMBNAILPANEL_TYPES_H

#ifdef MVIEWER_THUMBNAILPANEL_DEFINE_DELEGATES
#undef MVIEWER_THUMBNAILPANEL_DEFINE_DELEGATES

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>
#include <QObject>
#include <QPainter>
#include <QSize>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

// Paints only the visible cells: a (cached/decoded) thumbnail + filename. No
// widget is created per image, so the gallery scales to very large directories.
class ThumbnailPanel::ThumbDelegate : public QStyledItemDelegate
{
  public:
    explicit ThumbDelegate(ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_panel(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

  private:
    int thumbSize() const; // reads m_panel->thumbSize()
    ThumbnailPanel *m_panel;
};

// Details / List mode delegate: renders each row as a horizontal strip with
// columns for thumbnail, filename, resolution, size, date, and format.
class ThumbnailPanel::DetailsDelegate : public QStyledItemDelegate
{
  public:
    explicit DetailsDelegate(ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_panel(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

  private:
    ThumbnailPanel *m_panel;
};

// P0: Windows-Explorer-style list — a small icon plus the file name, wrapping
// into columns. Used by ViewMode::List. Lighter than Details (no columns).
class ThumbnailPanel::ListDelegate : public QStyledItemDelegate
{
  public:
    explicit ListDelegate(ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_panel(panel)
    {
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

  private:
    ThumbnailPanel *m_panel;
};

#endif // MVIEWER_THUMBNAILPANEL_DEFINE_DELEGATES
