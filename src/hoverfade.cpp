#include "hoverfade.h"

#include <QAbstractItemView>
#include <QMouseEvent>

#include <algorithm>

namespace mviewer::ui
{

namespace
{
constexpr int kFrameMs = 16;
const char *const kFadeObjectName = "mvHoverFade";
} // namespace

HoverFade::HoverFade(QAbstractItemView *view) : QObject(view), m_view(view)
{
    setObjectName(QLatin1String(kFadeObjectName));
    m_timer.setInterval(kFrameMs);
    connect(&m_timer, &QTimer::timeout, this, &HoverFade::tick);
    view->viewport()->setMouseTracking(true);
    view->viewport()->installEventFilter(this);
}

HoverFade *HoverFade::forView(const QWidget *view)
{
    if (view == nullptr)
        return nullptr;
    return view->findChild<HoverFade *>(QLatin1String(kFadeObjectName), Qt::FindDirectChildrenOnly);
}

qreal HoverFade::progress(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0.0;
    if (m_in.isValid() && index == QModelIndex(m_in))
        return m_inProgress;
    if (m_out.isValid() && index == QModelIndex(m_out))
        return m_outProgress;
    return 0.0;
}

bool HoverFade::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type())
    {
    case QEvent::MouseMove:
    case QEvent::HoverMove:
    {
        const QPoint pos = m_view->viewport()->mapFromGlobal(QCursor::pos());
        setHovered(m_view->indexAt(pos));
        break;
    }
    case QEvent::Leave:
    case QEvent::HoverLeave:
        setHovered(QModelIndex());
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void HoverFade::setHovered(const QModelIndex &index)
{
    if (QModelIndex(m_in) == index)
        return;
    if (m_out.isValid() && m_out != m_in)
        repaint(m_out);
    if (index.isValid() && QModelIndex(m_out) == index)
    {
        // Moving back onto the item that is still fading out: resume from there.
        std::swap(m_in, m_out);
        std::swap(m_inProgress, m_outProgress);
    }
    else
    {
        m_out = m_in;
        m_outProgress = m_inProgress;
        m_in = QPersistentModelIndex(index);
        m_inProgress = 0.0;
    }
    m_clock.restart();
    if (!m_timer.isActive())
        m_timer.start();
    tick();
}

void HoverFade::tick()
{
    const qreal step = static_cast<qreal>(std::max<qint64>(m_clock.restart(), 1)) / kDurationMs;
    m_inProgress = m_in.isValid() ? std::min(1.0, m_inProgress + step) : 0.0;
    m_outProgress = m_out.isValid() ? std::max(0.0, m_outProgress - step) : 0.0;
    repaint(m_in);
    repaint(m_out);
    const bool inDone = !m_in.isValid() || m_inProgress >= 1.0;
    const bool outDone = !m_out.isValid() || m_outProgress <= 0.0;
    if (outDone)
        m_out = QPersistentModelIndex();
    if (inDone && outDone)
        m_timer.stop();
}

void HoverFade::repaint(const QPersistentModelIndex &index) const
{
    if (index.isValid())
        m_view->viewport()->update(m_view->visualRect(index));
}

} // namespace mviewer::ui
