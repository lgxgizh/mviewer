#pragma once

// Compare-canvas compositor (review P2-09).
//
// The panes are still rasterised by the SAME CPU code as before (QPainter into a
// QImage per pane), because that is what makes the result pixel-parity with the
// old path provable. What moves to the GPU is the compositing itself - which
// pixels of pane A / pane B / the blended second image end up where - done in a
// fragment shader over two textures. A 1:1 texel copy is exact, so the modes
// that only clip or alpha-blend (split, swipe, overlay, checkerboard) reproduce
// the old pixels bit for bit.
//
// Opt-in (`MVIEWER_COMPARE_GL=1`) until it has been compared against
// tests/gl_baseline on real hardware.

#include <QByteArray>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLVertexArrayObject>
#include <QRect>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;

namespace mviewer::ui
{

enum class CompareCompositeMode
{
    Split,
    Swipe,
    Overlay,
    Checkerboard
};

class CompareCompositeGL
{
  public:
    CompareCompositeGL() = default;
    ~CompareCompositeGL();

    CompareCompositeGL(const CompareCompositeGL &) = delete;
    CompareCompositeGL &operator=(const CompareCompositeGL &) = delete;

    static bool requested();
    static void resetRequestedCache();

    /// True when an offscreen GL path can actually run here.
    bool isReady() const
    {
        return m_ready;
    }

    bool ensureContext(QOpenGLContext *shareContext);
    void destroy();

    /// Composite `paneA` / `paneB` (both at the target size) into an image of
    /// `size`. `dividerX` is in image pixels; `alpha` is the second pane's
    /// opacity for Overlay; `checkerSize` is the block edge in pixels.
    QImage composite(CompareCompositeMode mode, const QImage &paneA, const QImage &paneB,
                     const QSize &size, int dividerX, double alpha, int checkerSize);

    /// Composite, reusing the last result while `cacheKey` is unchanged. A single
    /// state change repaints the canvas several times (overlay toggles, ROI
    /// redraws); each of those used to re-rasterise both panes, run the shader
    /// and read the whole framebuffer back. The key must cover everything the
    /// composite depends on, exactly like the CPU base-surface key.
    QImage compositeCached(const QByteArray &cacheKey,
                           const std::function<void(QImage &, QImage &)> &rasterizePanes,
                           CompareCompositeMode mode, const QSize &size, int dividerX,
                           double alpha, int checkerSize);

  private:
    QOpenGLContext m_context;
    QOffscreenSurface m_surface;
    QOpenGLShaderProgram *m_program = nullptr;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
    std::unique_ptr<QOpenGLBuffer> m_vbo;
    unsigned int m_textures[2] = {0, 0};
    QSize m_fboSize;
    bool m_ready = false;
    bool m_initFailed = false; // no usable GL here: stop retrying every frame
    bool m_destroying = false;
    bool m_inContextTeardown = false;
    QMetaObject::Connection m_aboutToBeDestroyed;
    QByteArray m_cachedKey; // empty = nothing cached
    QImage m_cachedImage;

    bool hasGpuObjects() const;
    void abandonGpuObjects();
};

} // namespace mviewer::ui
