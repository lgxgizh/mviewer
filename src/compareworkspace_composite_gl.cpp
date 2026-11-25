#include "compareworkspace_composite_gl.h"
#include "compareworkspace_composite_gl_upload.h"

#include <QObject>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSurfaceFormat>

#include <algorithm>
#include <optional>

namespace mviewer::ui
{
namespace
{
int g_requested = -1;

const char *const kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)";

// The two pane rasters are already at the target resolution, so every branch is
// a 1:1 texel read - no resampling, which is what keeps this bit-exact against
// the QPainter composite.
const char *const kFragmentShader = R"(#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uA;
uniform sampler2D uB;
uniform int uMode;        // 0 split/swipe, 1 overlay, 2 checkerboard
uniform float uDivider;   // 0..1, fraction of the width showing pane A
uniform float uAlpha;     // second-pane opacity
uniform vec2 uChecker;    // block edge, in uv units
void main()
{
    vec3 a = texture(uA, vUv).rgb;
    vec3 b = texture(uB, vUv).rgb;
    if (uMode == 1)
    {
        outColor = vec4(mix(a, b, clamp(uAlpha, 0.0, 1.0)), 1.0);
        return;
    }
    if (uMode == 2)
    {
        vec2 cell = floor(vUv / max(uChecker, vec2(1e-6)));
        bool useB = mod(cell.x + cell.y, 2.0) >= 0.5;
        outColor = vec4(useB ? b : a, 1.0);
        return;
    }
    outColor = vec4(vUv.x < uDivider ? a : b, 1.0);
}
)";
} // namespace

CompareCompositeGL::~CompareCompositeGL()
{
    destroy();
    // m_fbo / m_vao / m_vbo are destroyed before m_context. Do not leave them
    // owning GL objects, or their destructors run on the foreign current context.
    if (hasGpuObjects())
        abandonGpuObjects();
}

bool CompareCompositeGL::requested()
{
    if (g_requested < 0)
    {
        const QByteArray v = qgetenv("MVIEWER_COMPARE_GL");
        const bool off = (v == "0" || v.compare("false", Qt::CaseInsensitive) == 0 ||
                          v.compare("no", Qt::CaseInsensitive) == 0 ||
                          v.compare("off", Qt::CaseInsensitive) == 0);
        g_requested = off ? 0 : (!v.isEmpty() ? 1 : 0);
    }
    return g_requested != 0;
}

void CompareCompositeGL::resetRequestedCache()
{
    g_requested = -1;
}

namespace
{

class ContextRestorer
{
  public:
    ContextRestorer(QOpenGLContext *context, QSurface *surface)
        : m_previous(QOpenGLContext::currentContext()),
          m_previousSurface(m_previous != nullptr ? m_previous->surface() : nullptr)
    {
        m_bound = context != nullptr && surface != nullptr && context->isValid() &&
                  context->makeCurrent(surface);
    }

    ~ContextRestorer()
    {
        restore();
    }

    ContextRestorer(const ContextRestorer &) = delete;
    ContextRestorer &operator=(const ContextRestorer &) = delete;

    bool isBound() const
    {
        return m_bound;
    }

    void restore()
    {
        if (m_restored)
            return;
        m_restored = true;
        if (m_previous != nullptr && m_previous->isValid() && m_previousSurface != nullptr &&
            m_previous->makeCurrent(m_previousSurface))
            return;
        if (QOpenGLContext::currentContext() != nullptr)
            QOpenGLContext::currentContext()->doneCurrent();
    }

  private:
    QOpenGLContext *m_previous = nullptr;
    QSurface *m_previousSurface = nullptr;
    bool m_bound = false;
    bool m_restored = false;
};

} // namespace

bool CompareCompositeGL::hasGpuObjects() const
{
    return m_textures[0] != 0 || m_textures[1] != 0 || m_fbo || m_vbo || m_vao ||
           m_program != nullptr;
}

bool CompareCompositeGL::ensureContext(QOpenGLContext *shareContext)
{
    if (m_ready)
        return true;
    if (hasGpuObjects())
        destroy();
    if (hasGpuObjects())
        destroy();
    if (hasGpuObjects())
        return false;

    if (!m_context.isValid())
    {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        m_context.setFormat(format);
        if (shareContext != nullptr)
            m_context.setShareContext(shareContext);
        if (!m_context.create())
            return false;
        m_surface.setFormat(m_context.format());
        m_surface.create();
    }
    if (!m_aboutToBeDestroyed)
    {
        m_aboutToBeDestroyed = QObject::connect(
            &m_context, &QOpenGLContext::aboutToBeDestroyed, &m_context,
            [this]()
            {
                m_inContextTeardown = true;
                destroy();
                m_inContextTeardown = false;
            },
            Qt::DirectConnection);
    }

    ContextRestorer bound(&m_context, &m_surface);
    if (!bound.isBound())
        return false;

    QOpenGLFunctions *gl = m_context.functions();
    if (gl == nullptr)
    {
        destroy();
        return false;
    }
    m_program = new QOpenGLShaderProgram(&m_context);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
        !m_program->link())
    {
        destroy();
        return false;
    }
    m_program->bind();
    m_program->setUniformValue("uA", 0);
    m_program->setUniformValue("uB", 1);
    m_program->release();

    m_vao = std::make_unique<QOpenGLVertexArrayObject>(&m_context);
    if (!m_vao->create())
    {
        destroy();
        return false;
    }
    m_vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    if (!m_vbo->create())
    {
        destroy();
        return false;
    }
    m_vbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vbo->bind();
    m_vbo->allocate(16 * sizeof(float));
    m_vbo->release();

    gl->glGenTextures(2, m_textures);
    for (unsigned int tex : m_textures)
    {
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    m_ready = true;
    return true;
}

void CompareCompositeGL::abandonGpuObjects()
{
    if (m_program != nullptr)
    {
        m_program->setParent(nullptr);
        m_program = nullptr;
    }
    if (m_vao)
        m_vao->setParent(nullptr);
    (void)m_fbo.release();
    (void)m_vbo.release();
    (void)m_vao.release();
    m_textures[0] = m_textures[1] = 0;
}

void CompareCompositeGL::destroy()
{
    if (m_destroying)
        return;
    m_destroying = true;

    if (hasGpuObjects() && m_context.isValid())
    {
        const bool alreadyOwner =
            QOpenGLContext::currentContext() == &m_context && m_context.surface() == &m_surface;
        std::optional<ContextRestorer> bound;
        bool ownerCurrent = alreadyOwner;
        if (!alreadyOwner)
        {
            // aboutToBeDestroyed does not make the context current. The native
            // context is still alive here; make it current, then restore the caller.
            bound.emplace(&m_context, &m_surface);
            ownerCurrent = bound->isBound();
        }
        if (ownerCurrent)
        {
            if (QOpenGLFunctions *gl = m_context.functions())
            {
                if (m_textures[0] != 0 || m_textures[1] != 0)
                    gl->glDeleteTextures(2, m_textures);
            }
            m_textures[0] = m_textures[1] = 0;
            m_fbo.reset();
            m_vbo.reset();
            m_vao.reset();
            delete m_program;
            m_program = nullptr;
        }
        else if (m_inContextTeardown)
            abandonGpuObjects();
        // If makeCurrent failed and the context is still valid, leave wrappers
        // in place so a later destroy or ensureContext can retry.
    }
    else if (hasGpuObjects())
        abandonGpuObjects();

    m_fboSize = QSize();
    m_ready = false;
    m_destroying = false;
}

bool uploadCompareRgb888(QOpenGLFunctions *gl, unsigned int tex, const QImage &image)
{
    if (gl == nullptr || image.isNull())
        return false;
    // A full copy normalizes even externally supplied RGB888 row strides to
    // Qt-owned storage, whose scanlines are aligned to four bytes.
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888).copy();
    if (rgb.isNull())
        return false;
    GLint previousAlignment = 4;
    GLint previousRowLength = 0;
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previousRowLength);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, rgb.width(), rgb.height(), 0, GL_RGB,
                     GL_UNSIGNED_BYTE, rgb.constBits());
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, previousRowLength);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    return true;
}

QImage CompareCompositeGL::composite(CompareCompositeMode mode, const QImage &paneA,
                                     const QImage &paneB, const QSize &size, int dividerX,
                                     double alpha, int checkerSize)
{
    if (!m_ready || size.isEmpty())
        return QImage();
    ContextRestorer bound(&m_context, &m_surface);
    if (!bound.isBound())
        return QImage();
    QOpenGLFunctions *gl = m_context.functions();
    if (gl == nullptr)
        return QImage();

    if (!m_fbo || m_fboSize != size)
    {
        m_fbo.reset(new QOpenGLFramebufferObject(size, GL_TEXTURE_2D));
        m_fboSize = size;
    }
    if (!m_fbo->isValid())
        return QImage();

    if (!uploadCompareRgb888(gl, m_textures[0], paneA) ||
        !uploadCompareRgb888(gl, m_textures[1], paneB))
        return QImage();

    m_fbo->bind();
    gl->glViewport(0, 0, size.width(), size.height());
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT);

    m_vao->bind();
    m_vbo->bind();
    const float verts[16] = {-1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 1.0f,
                             -1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  1.0f,  1.0f, 0.0f};
    m_vbo->write(0, verts, sizeof(verts));

    m_program->bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    m_program->setUniformValue("uMode", mode == CompareCompositeMode::Overlay ? 1
                            : mode == CompareCompositeMode::Checkerboard ? 2
                                                                        : 0);
    m_program->setUniformValue(
        "uDivider", size.width() > 0 ? static_cast<float>(dividerX) / size.width() : 0.0f);
    m_program->setUniformValue("uAlpha", static_cast<float>(alpha));
    m_program->setUniformValue("uChecker",
                               QSizeF(checkerSize > 0 ? static_cast<float>(checkerSize) / size.width()
                                                     : 1.0f,
                                      checkerSize > 0 ? static_cast<float>(checkerSize) / size.height()
                                                     : 1.0f));

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, m_textures[0]);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, m_textures[1]);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->release();
    m_vbo->release();
    m_vao->release();

    QImage out = m_fbo->toImage();
    m_fbo->bindDefault();
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace mviewer::ui
