#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPersistentModelIndex>
#include <QTimer>

class QAbstractItemView;

namespace mviewer::ui
{

// Short hover fade for item views. Tracks the item under the mouse and the item
// it just left, and reports a 0..1 progress that delegates blend their hover
// color with. Only repaints the two affected item rects while animating.
class HoverFade : public QObject
{
    Q_OBJECT

  public:
    static constexpr int kDurationMs = 120;

    explicit HoverFade(QAbstractItemView *view);

    // The fade attached to an item view, or nullptr.
    static HoverFade *forView(const QWidget *view);

    // 0 = not hovered, 1 = fully hovered.
    qreal progress(const QModelIndex &index) const;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void setHovered(const QModelIndex &index);
    void tick();
    void repaint(const QPersistentModelIndex &index) const;

    QAbstractItemView *m_view = nullptr;
    QPersistentModelIndex m_in;
    QPersistentModelIndex m_out;
    qreal m_inProgress = 0.0;
    qreal m_outProgress = 0.0;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

} // namespace mviewer::ui
