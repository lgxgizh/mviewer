#pragma once

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPointer>
#include <QThread>

#include <functional>
#include <utility>

namespace mviewer::ui
{
// One UI-thread GL resource owner. Its private surface permits cleanup even
// after the widget/offscreen surface used for drawing is unavailable.
class GlOwnerContext
{
  public:
    class Binding
    {
      public:
        explicit Binding(GlOwnerContext &owner)
            : m_owner(owner.context()), m_previous(QOpenGLContext::currentContext()),
              m_previousSurface(m_previous ? m_previous->surface() : nullptr)
        {
            if (!m_owner || !m_owner->isValid() || owner.m_forceBindFailure ||
                m_owner->thread() != QThread::currentThread())
                return;
            if (m_previous == m_owner)
                m_valid = true;
            else if (owner.m_surface.isValid())
                m_valid = m_owner->makeCurrent(&owner.m_surface);
        }

        ~Binding()
        {
            if (!m_valid || m_previous == m_owner)
                return;
            if (m_previous && m_previous->isValid() && m_previousSurface &&
                m_previous->makeCurrent(m_previousSurface))
                return;
            if (m_owner && QOpenGLContext::currentContext() == m_owner)
                m_owner->doneCurrent();
        }

        bool valid() const
        {
            return m_valid;
        }
        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;

      private:
        QPointer<QOpenGLContext> m_owner;
        QPointer<QOpenGLContext> m_previous;
        QSurface *m_previousSurface = nullptr;
        bool m_valid = false;
    };

    GlOwnerContext(QOpenGLContext *context, std::function<void(bool)> cleanup)
        : m_context(context), m_cleanup(std::move(cleanup))
    {
        m_surface.setFormat(context->format());
        m_surface.create();
        m_connection = QObject::connect(
            context, &QOpenGLContext::aboutToBeDestroyed, context, [this]() { retire(); },
            Qt::DirectConnection);
    }

    ~GlOwnerContext()
    {
        retire();
    }
    GlOwnerContext(const GlOwnerContext &) = delete;
    GlOwnerContext &operator=(const GlOwnerContext &) = delete;

    QOpenGLContext *context() const
    {
        return m_context.data();
    }
    void forceBindFailureForTest(bool fail)
    {
        m_forceBindFailure = fail;
    }

  private:
    void retire()
    {
        QObject::disconnect(m_connection);
        if (!m_cleanup)
            return;
        auto cleanup = std::exchange(m_cleanup, {});
        {
            Binding bound(*this);
            cleanup(bound.valid());
        }
        m_context.clear();
    }

    QPointer<QOpenGLContext> m_context;
    QOffscreenSurface m_surface;
    QMetaObject::Connection m_connection;
    std::function<void(bool)> m_cleanup;
    bool m_forceBindFailure = false;
};
} // namespace mviewer::ui
