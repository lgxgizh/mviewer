#include "gpu/ImageBlitterShader.h"
#include "gpu/BlitStateGuard.h"

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSurfaceFormat>

#include <cmath>

namespace
{
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

const char *const kFragmentShader = R"(#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uTexture;
uniform sampler2D uOverlay;
uniform float uExposure;
uniform float uInvGamma;
uniform float uOverlayAlpha;
void main()
{
    vec3 c = texture(uTexture, vUv).rgb;
    c = clamp(c * uExposure, 0.0, 1.0);
    c = pow(max(c, vec3(0.0)), vec3(uInvGamma));
    if (uOverlayAlpha > 0.0)
    {
        vec4 o = texture(uOverlay, vUv);
        c = mix(c, o.rgb, clamp(o.a * uOverlayAlpha, 0.0, 1.0));
    }
    outColor = vec4(c, 1.0);
}
)";
} // namespace

ImageBlitterShader::~ImageBlitterShader()
{
    destroy();
}

namespace
{
int g_requested = -1;
} // namespace

bool ImageBlitterShader::requested()
{
    // Cached so the hot path never reads the environment; tests that flip the
    // variable call resetRequestedCache() first.
    if (g_requested < 0)
    {
        const QByteArray v = qgetenv("MVIEWER_BLITTER");
        g_requested = (v.compare("shader", Qt::CaseInsensitive) == 0) ? 1 : 0;
    }
    return g_requested != 0;
}

void ImageBlitterShader::resetRequestedCache()
{
    g_requested = -1;
}

bool ImageBlitterShader::create()
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr || !ctx->isValid() || ctx->isOpenGLES())
        return false;
    // Both the GLSL330 shader and state guard need desktop OpenGL3.3.
    const QSurfaceFormat format = ctx->format();
    if (format.majorVersion() < 3 || (format.majorVersion() == 3 && format.minorVersion() < 3))
        return false;
    mviewer::ui::BlitStateGuard restore(ctx->extraFunctions());
    m_program = new QOpenGLShaderProgram(ctx);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader))
    {
        destroy();
        return false;
    }
    if (!m_program->link())
    {
        destroy();
        return false;
    }
    m_program->bind();
    m_program->setUniformValue("uTexture", 0);
    m_program->setUniformValue("uOverlay", 1);
    m_program->release();

    m_vao = new QOpenGLVertexArrayObject();
    m_ownObjects = true;
    if (!m_vao->create())
    {
        destroy();
        return false;
    }
    m_vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    if (!m_vbo->create())
    {
        destroy();
        return false;
    }
    m_vbo->setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vbo->bind();
    m_vbo->allocate(16 * sizeof(float));
    m_vbo->release();

    m_ready = true;
    return true;
}

void ImageBlitterShader::destroy()
{
    if (m_ownObjects)
    {
        delete m_vbo;
        m_vbo = nullptr;
        delete m_vao;
        m_vao = nullptr;
    }
    delete m_program;
    m_program = nullptr;
    m_ready = false;
}

void ImageBlitterShader::setAdjust(float exposure, float inverseGamma)
{
    m_exposure = exposure > 1e-6f ? exposure : 1e-6f;
    m_inverseGamma = inverseGamma > 0.05f ? inverseGamma : 1.0f;
}

void ImageBlitterShader::setOverlay(unsigned int texture, float alpha)
{
    m_overlayTexture = texture;
    m_overlayAlpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

void ImageBlitterShader::blit(unsigned int texture, const QRect &destination, const QRect &viewport,
                              const QRect &content, const QSize &textureSize)
{
    if (!m_ready || texture == 0 || destination.isEmpty() || viewport.isEmpty())
        return;
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr || !ctx->isValid())
        return;
    QOpenGLFunctions *gl = ctx->functions();
    if (gl == nullptr || m_program == nullptr || m_vao == nullptr || m_vbo == nullptr)
        return;
    mviewer::ui::BlitStateGuard restore(ctx->extraFunctions());
    // The shader mixes the overlay itself and emits opaque output. Fixed
    // function blending must not depend on the caller's enable or factors.
    gl->glDisable(GL_BLEND);
    const float vw = viewport.width() > 0 ? static_cast<float>(viewport.width()) : 1.0f;
    const float vh = viewport.height() > 0 ? static_cast<float>(viewport.height()) : 1.0f;
    const float dx = static_cast<float>(destination.x()) / vw;
    const float dy = static_cast<float>(destination.y()) / vh;
    const float dw = static_cast<float>(destination.width()) / vw;
    const float dh = static_cast<float>(destination.height()) / vh;
    const float x0 = dx * 2.0f - 1.0f;
    const float x1 = (dx + dw) * 2.0f - 1.0f;
    // Qt's origin is top-left; GL's is bottom-left.
    const float yTop = 1.0f - dy * 2.0f;
    const float yBot = 1.0f - (dy + dh) * 2.0f;
    const float tw = textureSize.width() > 0 ? static_cast<float>(textureSize.width()) : 1.0f;
    const float th = textureSize.height() > 0 ? static_cast<float>(textureSize.height()) : 1.0f;
    const float cw = content.width() > 0 ? static_cast<float>(content.width()) : tw;
    const float ch = content.height() > 0 ? static_cast<float>(content.height()) : th;
    const float cx = std::floor(static_cast<float>(content.x()));
    const float cy = std::floor(static_cast<float>(content.y()));
    const float u0 = cx / tw;
    const float u1 = (cx + cw) / tw;
    const float vTop = cy / th;
    const float vBot = (cy + ch) / th;

    const float verts[16] = {
        x0, yBot, u0, vBot, // bottom-left
        x1, yBot, u1, vBot, // bottom-right
        x0, yTop, u0, vTop, // top-left
        x1, yTop, u1, vTop, // top-right
    };

    m_vao->bind();
    m_vbo->bind();
    m_vbo->write(0, verts, sizeof(verts));
    m_program->bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    m_program->setUniformValue("uExposure", m_exposure);
    m_program->setUniformValue("uInvGamma", m_inverseGamma);
    m_program->setUniformValue("uOverlayAlpha", m_overlayAlpha);

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    if (m_overlayTexture != 0 && m_overlayAlpha > 0.0f)
    {
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, m_overlayTexture);
        gl->glActiveTexture(GL_TEXTURE0);
    }
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->release();
    m_vbo->release();
    m_vao->release();
}
