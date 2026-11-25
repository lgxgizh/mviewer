#pragma once

#include <QOpenGLExtraFunctions>

namespace mviewer::ui
{
// OpenGL 3.3 shader blits touch only their private VAO and buffer. Keep the
// caller's bindings and blend enable intact, including texture units 0 and 1.
class BlitStateGuard
{
  public:
    explicit BlitStateGuard(QOpenGLExtraFunctions *gl) : m_gl(gl)
    {
        // extraFunctions() can be null (a core-only context); there is nothing
        // to preserve in that case, but callers must not dereference it.
        if (m_gl == nullptr)
            return;
        m_gl->glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
        m_gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vao);
        m_gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_buffer);
        m_gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &m_active);
        m_blend = m_gl->glIsEnabled(GL_BLEND);
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture0);
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture1);
        m_gl->glActiveTexture(static_cast<GLenum>(m_active));
    }

    ~BlitStateGuard()
    {
        if (m_gl == nullptr)
            return;
        m_gl->glUseProgram(static_cast<GLuint>(m_program));
        m_gl->glBindVertexArray(static_cast<GLuint>(m_vao));
        m_gl->glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_buffer));
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texture0));
        m_gl->glActiveTexture(GL_TEXTURE1);
        m_gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_texture1));
        m_gl->glActiveTexture(static_cast<GLenum>(m_active));
        if (m_blend)
            m_gl->glEnable(GL_BLEND);
        else
            m_gl->glDisable(GL_BLEND);
    }

    BlitStateGuard(const BlitStateGuard &) = delete;
    BlitStateGuard &operator=(const BlitStateGuard &) = delete;

  private:
    QOpenGLExtraFunctions *m_gl;
    GLint m_program = 0;
    GLint m_vao = 0;
    GLint m_buffer = 0;
    GLint m_active = GL_TEXTURE0;
    GLint m_texture0 = 0;
    GLint m_texture1 = 0;
    GLboolean m_blend = GL_FALSE;
};
} // namespace mviewer::ui
