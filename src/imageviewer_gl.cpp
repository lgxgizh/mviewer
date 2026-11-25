#include "gpu/GlOwnerContext.h"
#include "gpu/ImageBlitterShader.h"
#include "imageviewer.h"

#include <QOpenGLFunctions>

void ImageViewer::releaseGlResources(bool ownerCurrent)
{
    m_gpu.clear();
    if (m_shader)
    {
        delete m_shader;
        m_shader = nullptr;
    }
    if (ownerCurrent)
    {
        if (m_blitter)
            m_blitter->destroy();
        m_blitter.reset();
    }
    else
    {
        // Do not run Qt GL wrapper destructors after failed owner activation.
        // The native context will reclaim GPU allocations at its destruction.
        (void)m_blitter.release();
    }
    m_blitterReady = false;
    m_shaderReady = false;
}

void ImageViewer::initializeGL()
{
    // Also handles a second initialize on the same context, or after reparent.
    // Retire the previous owner BEFORE replacing any shader/blitter pointers.
    m_glOwner.reset();
    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx || !ctx->isValid())
        return;
    m_glOwner = std::make_shared<mviewer::ui::GlOwnerContext>(ctx, [this](bool current)
                                                              { releaseGlResources(current); });
    ctx->functions()->glClearColor(0.f, 0.f, 0.f, 1.f);
    m_blitter = std::make_unique<QOpenGLTextureBlitter>();
    m_blitterReady = m_blitter->create();
    if (ImageBlitterShader::requested())
    {
        m_shader = new ImageBlitterShader();
        m_shaderReady = m_shader->create();
    }
}
