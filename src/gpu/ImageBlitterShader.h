#pragma once

// Custom fragment-shader blitter for the GPU tile path (review P2-08).
//
// `QOpenGLTextureBlitter` can only copy a texture - no per-pixel work. This
// class draws the same quad through a small shader so exposure / gamma / zebra
// and a second (overlay) texture can be applied without dropping back to
// QPainter.
//
// It is opt-in (`MVIEWER_BLITTER=shader`): Qt's blitter stays the default until
// the two paths have been compared frame by frame on real hardware.

#include <QOpenGLContext>
#include <QRect>
#include <QSize>

class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;
class QOpenGLBuffer;

class ImageBlitterShader
{
  public:
    ImageBlitterShader() = default;
    ~ImageBlitterShader();

    ImageBlitterShader(const ImageBlitterShader &) = delete;
    ImageBlitterShader &operator=(const ImageBlitterShader &) = delete;

    /// Compile and link. Returns false when the context cannot support it; the
    /// caller then keeps using QOpenGLTextureBlitter.
    bool create();
    void destroy();
    bool ready() const
    {
        return m_ready;
    }

    /// True when the operator asked for the shader path.
    static bool requested();
    static void resetRequestedCache();

    /// Display adjustment, applied in the fragment shader.
    void setAdjust(float exposure, float inverseGamma);

    /// Draw `texture` into `destination` (widget pixels), sampling the sub-rect
    /// of the texture described by `content` (in texel units). `viewport` is the
    /// widget rect, used to normalise the target.
    void blit(unsigned int texture, const QRect &destination, const QRect &viewport,
              const QRect &content, const QSize &textureSize);

    /// Optional second texture drawn on top at `alpha` (the overlay raster).
    void setOverlay(unsigned int texture, float alpha);

  private:
    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLVertexArrayObject *m_vao = nullptr;
    QOpenGLBuffer *m_vbo = nullptr;
    unsigned int m_overlayTexture = 0;
    float m_overlayAlpha = 0.0f;
    float m_exposure = 1.0f;
    float m_inverseGamma = 1.0f;
    bool m_ready = false;
    bool m_ownObjects = false;
};
