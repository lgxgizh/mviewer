// P0-3 / P1-17 first-slice real-GL pixel gate. Built with the UI tests, not
// registered with CTest: Windows and the offscreen ctest step must not skip
// or fail it. Required Linux CI runs it under xvfb / xcb / Mesa.

#include "gpu/GpuTileUploader.h"
#include "gpu/ImageBlitterShader.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace
{
int g_pass = 0;
int g_fail = 0;

void check(bool ok, const char *message)
{
    if (ok)
    {
        ++g_pass;
        std::printf("  PASS: %s\n", message);
    }
    else
    {
        ++g_fail;
        std::printf("  FAIL: %s\n", message);
    }
}

TileKey makeKey(const char *id, int col = 0)
{
    TileKey key;
    key.imageId = id;
    key.col = col;
    return key;
}

std::vector<std::uint8_t> grayRamp(int width, int height, int seed)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
            pixels[static_cast<std::size_t>(y * width + x)] =
                static_cast<std::uint8_t>(16 + (seed + x * 13 + y * 29) % 200);
    }
    return pixels;
}

std::vector<std::uint8_t> paddedGrayProbe(int width, int height, int rowLength, int alignment,
                                          int seed)
{
    const int rowPixels = width > rowLength ? width : rowLength;
    const std::size_t rowBytes = static_cast<std::size_t>(rowPixels);
    const std::size_t align = alignment > 1 ? static_cast<std::size_t>(alignment) : 1;
    const std::size_t stride = ((rowBytes + align - 1) / align) * align;
    std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(height), 0);
    for (int y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < stride; ++x)
            pixels[static_cast<std::size_t>(y) * stride + x] =
                static_cast<std::uint8_t>(16 + (seed + static_cast<int>(x) * 13 + y * 29) % 200);
    }
    return pixels;
}

struct ScopedUnpackReset
{
    QOpenGLFunctions *gl = nullptr;

    explicit ScopedUnpackReset(QOpenGLFunctions *functions) : gl(functions)
    {
    }

    ~ScopedUnpackReset()
    {
        if (gl != nullptr)
        {
            gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
    }

    ScopedUnpackReset(const ScopedUnpackReset &) = delete;
    ScopedUnpackReset &operator=(const ScopedUnpackReset &) = delete;
};

std::vector<std::uint8_t> rgbPattern(int width, int height, int seed)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t i = static_cast<std::size_t>((y * width + x) * 3);
            pixels[i] = static_cast<std::uint8_t>((seed + x * 17) % 256);
            pixels[i + 1] = static_cast<std::uint8_t>((seed + y * 43) % 256);
            pixels[i + 2] = static_cast<std::uint8_t>((seed + x * 11 + y * 7) % 256);
        }
    }
    return pixels;
}

std::vector<std::uint8_t> rgbaPattern(int width, int height, int seed, std::uint8_t alpha)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
    const auto rgb = rgbPattern(width, height, seed);
    for (int i = 0; i < width * height; ++i)
    {
        pixels[static_cast<std::size_t>(i * 4)] = rgb[static_cast<std::size_t>(i * 3)];
        pixels[static_cast<std::size_t>(i * 4 + 1)] = rgb[static_cast<std::size_t>(i * 3 + 1)];
        pixels[static_cast<std::size_t>(i * 4 + 2)] = rgb[static_cast<std::size_t>(i * 3 + 2)];
        pixels[static_cast<std::size_t>(i * 4 + 3)] = alpha;
    }
    return pixels;
}

int maxAbsDiff(int a, int b)
{
    return a > b ? a - b : b - a;
}

bool rgbaMatches(const std::vector<std::uint8_t> &got, const std::vector<std::uint8_t> &expected,
                 int tolerance)
{
    if (got.size() != expected.size() || got.empty())
        return false;
    for (std::size_t i = 0; i < got.size(); ++i)
    {
        if (maxAbsDiff(got[i], expected[i]) > tolerance)
            return false;
    }
    return true;
}

struct ScratchContext
{
    QOpenGLContext context;
    QOffscreenSurface surface;

    bool bind()
    {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        context.setFormat(format);
        if (!context.create())
            return false;
        surface.setFormat(context.format());
        surface.create();
        return context.makeCurrent(&surface);
    }

    ~ScratchContext()
    {
        if (QOpenGLContext::currentContext() == &context)
            context.doneCurrent();
    }
};

struct PixelFbo
{
    QOpenGLFunctions *gl = nullptr;
    GLuint fbo = 0;
    GLuint rbo = 0;
    int width = 0;
    int height = 0;

    bool create(QOpenGLFunctions *functions, int w, int h)
    {
        destroy();
        gl = functions;
        width = w;
        height = h;
        gl->glGenFramebuffers(1, &fbo);
        gl->glGenRenderbuffers(1, &rbo);
        gl->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
        return gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void bind() const
    {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glViewport(0, 0, width, height);
        gl->glClearColor(0.f, 0.f, 0.f, 0.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
    }

    // Top-left origin, RGBA8 tight packed, matching the CPU tile buffer.
    std::vector<std::uint8_t> readTopLeft() const
    {
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(width * height * 4));
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        std::vector<std::uint8_t> top(raw.size());
        const std::size_t stride = static_cast<std::size_t>(width * 4);
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t *src =
                raw.data() + static_cast<std::size_t>(height - 1 - y) * stride;
            std::memcpy(top.data() + static_cast<std::size_t>(y) * stride, src, stride);
        }
        return top;
    }

    void destroy()
    {
        if (gl == nullptr)
            return;
        if (rbo != 0)
            gl->glDeleteRenderbuffers(1, &rbo);
        if (fbo != 0)
            gl->glDeleteFramebuffers(1, &fbo);
        fbo = 0;
        rbo = 0;
        gl = nullptr;
    }

    ~PixelFbo()
    {
        destroy();
    }
};

std::vector<std::uint8_t> expectedGrayRgb(const std::vector<std::uint8_t> &gray)
{
    std::vector<std::uint8_t> out(gray.size() * 4);
    for (std::size_t i = 0; i < gray.size(); ++i)
    {
        out[i * 4] = gray[i];
        out[i * 4 + 1] = gray[i];
        out[i * 4 + 2] = gray[i];
        out[i * 4 + 3] = 255;
    }
    return out;
}

std::uint8_t applyAdjustChannel(std::uint8_t value, float exposure, float invGamma)
{
    float c = (static_cast<float>(value) / 255.0f) * exposure;
    if (c < 0.0f)
        c = 0.0f;
    if (c > 1.0f)
        c = 1.0f;
    c = std::pow(c, invGamma);
    const int rounded = static_cast<int>(c * 255.0f + 0.5f);
    if (rounded < 0)
        return 0;
    if (rounded > 255)
        return 255;
    return static_cast<std::uint8_t>(rounded);
}

std::vector<std::uint8_t> expectedAdjustedGray(const std::vector<std::uint8_t> &gray,
                                               float exposure, float invGamma)
{
    std::vector<std::uint8_t> out(gray.size() * 4);
    for (std::size_t i = 0; i < gray.size(); ++i)
    {
        const std::uint8_t c = applyAdjustChannel(gray[i], exposure, invGamma);
        out[i * 4] = c;
        out[i * 4 + 1] = c;
        out[i * 4 + 2] = c;
        out[i * 4 + 3] = 255;
    }
    return out;
}

std::vector<std::uint8_t> expectedOverlayMix(const std::vector<std::uint8_t> &baseRgb,
                                             const std::vector<std::uint8_t> &overlayRgba,
                                             float overlayAlpha)
{
    const std::size_t pixels = baseRgb.size() / 3;
    std::vector<std::uint8_t> out(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i)
    {
        const float oa = (static_cast<float>(overlayRgba[i * 4 + 3]) / 255.0f) * overlayAlpha;
        const float f = oa < 0.0f ? 0.0f : (oa > 1.0f ? 1.0f : oa);
        for (int c = 0; c < 3; ++c)
        {
            const float b =
                static_cast<float>(baseRgb[i * 3 + static_cast<std::size_t>(c)]) / 255.0f;
            const float o =
                static_cast<float>(overlayRgba[i * 4 + static_cast<std::size_t>(c)]) / 255.0f;
            const int rounded = static_cast<int>((b * (1.0f - f) + o * f) * 255.0f + 0.5f);
            out[i * 4 + static_cast<std::size_t>(c)] =
                static_cast<std::uint8_t>(rounded < 0 ? 0 : (rounded > 255 ? 255 : rounded));
        }
        out[i * 4 + 3] = 255;
    }
    return out;
}

std::vector<std::uint8_t> expectedRgbOpaque(const std::vector<std::uint8_t> &rgb)
{
    const std::size_t pixels = rgb.size() / 3;
    std::vector<std::uint8_t> out(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i)
    {
        out[i * 4] = rgb[i * 3];
        out[i * 4 + 1] = rgb[i * 3 + 1];
        out[i * 4 + 2] = rgb[i * 3 + 2];
        out[i * 4 + 3] = 255;
    }
    return out;
}

bool uploadAndRead(GpuTileUploader &uploader, ImageBlitterShader &shader, PixelFbo &fbo,
                   const TileKey &key, const std::uint8_t *pixels, int width, int height,
                   int channels, std::vector<std::uint8_t> *out)
{
    if (!uploader.ensure(key, pixels, width, height, channels))
        return false;
    const auto handle = uploader.handle(key);
    if (handle == 0)
        return false;
    if (!fbo.create(QOpenGLContext::currentContext()->functions(), width, height))
        return false;
    fbo.bind();
    shader.blit(static_cast<unsigned int>(handle), QRect(0, 0, width, height),
                QRect(0, 0, width, height), QRect(0, 0, width, height), QSize(width, height));
    *out = fbo.readTopLeft();
    return true;
}

void checkUnpackRestored(QOpenGLFunctions *gl, GpuTileUploader &uploader)
{
    {
        ScopedUnpackReset reset(gl);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
        const auto gray = paddedGrayProbe(5, 3, 17, 8, 3);
        const TileKey key = makeKey("unpack-r8");
        check(uploader.ensure(key, gray.data(), 5, 3, 1), "odd-width R8 upload succeeds");
        GLint alignment = 0;
        GLint rowLength = 0;
        gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
        check(alignment == 8, "upload restores unpack alignment to 8");
        check(rowLength == 17, "upload leaves unpack row length at 17");
        const auto replaced = paddedGrayProbe(5, 3, 17, 8, 9);
        check(uploader.ensure(key, replaced.data(), 5, 3, 1), "odd-width R8 replace succeeds");
        gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
        check(alignment == 8, "replace restores unpack alignment to 8");
        check(rowLength == 17, "replace leaves unpack row length at 17");
    }
    GLint resetAlignment = 0;
    GLint resetRowLength = -1;
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &resetAlignment);
    gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &resetRowLength);
    check(resetAlignment == 4 && resetRowLength == 0,
          "probe resets unpack alignment to 4 and row length to 0");
}

void checkR8WidthsAndReplace(GpuTileUploader &uploader, ImageBlitterShader &shader, PixelFbo &fbo)
{
    for (int width : {5, 6, 7, 8})
    {
        const auto gray = grayRamp(width, 3, width);
        std::vector<std::uint8_t> got;
        char label[96];
        std::snprintf(label, sizeof(label), "R8 %dx3 swizzle-or-default RGB equal, alpha 255",
                      width);
        const bool ok = uploadAndRead(uploader, shader, fbo, makeKey("r8-default", width),
                                      gray.data(), width, 3, 1, &got);
        check(ok && rgbaMatches(got, expectedGrayRgb(gray), 0), label);
    }
    const auto first = grayRamp(5, 3, 21);
    const auto second = grayRamp(5, 3, 77);
    const TileKey key = makeKey("r8-replace-pixels");
    std::vector<std::uint8_t> got;
    check(uploadAndRead(uploader, shader, fbo, key, first.data(), 5, 3, 1, &got) &&
              rgbaMatches(got, expectedGrayRgb(first), 0),
          "R8 first upload pixels");
    check(uploadAndRead(uploader, shader, fbo, key, second.data(), 5, 3, 1, &got) &&
              rgbaMatches(got, expectedGrayRgb(second), 0),
          "R8 replace updates pixels");
}

void checkExpandAndSwizzle(ImageBlitterShader &shader, PixelFbo &fbo, GpuTileUploader &uploader,
                           bool swizzle)
{
    GpuTileUploader::setForceGrayExpandForTest(true);
    {
        GpuTileUploader fallback;
        const auto gray = grayRamp(7, 4, 15);
        std::vector<std::uint8_t> got;
        check(uploadAndRead(fallback, shader, fbo, makeKey("r8-forced-expand"), gray.data(), 7, 4,
                            1, &got) &&
                  rgbaMatches(got, expectedGrayRgb(gray), 0),
              "forced R8-to-RGB expand RGB equal, alpha 255");
    }
    GpuTileUploader::setForceGrayExpandForTest(false);
    if (!swizzle)
        return;
    const auto gray = grayRamp(5, 3, 41);
    std::vector<std::uint8_t> viaSwizzle;
    std::vector<std::uint8_t> viaExpand;
    check(uploadAndRead(uploader, shader, fbo, makeKey("r8-swizzle-compare"), gray.data(), 5, 3, 1,
                        &viaSwizzle) &&
              rgbaMatches(viaSwizzle, expectedGrayRgb(gray), 0),
          "swizzle path pixels");
    GpuTileUploader::setForceGrayExpandForTest(true);
    GpuTileUploader expand;
    check(uploadAndRead(expand, shader, fbo, makeKey("r8-expand-compare"), gray.data(), 5, 3, 1,
                        &viaExpand) &&
              rgbaMatches(viaExpand, viaSwizzle, 0),
          "forced expand matches swizzle pixels");
    GpuTileUploader::setForceGrayExpandForTest(false);
}

void checkRgbRgbaAndAdjust(GpuTileUploader &uploader, ImageBlitterShader &shader, PixelFbo &fbo)
{
    {
        const auto rgb = rgbPattern(5, 3, 11);
        std::vector<std::uint8_t> got;
        check(uploadAndRead(uploader, shader, fbo, makeKey("rgb-odd"), rgb.data(), 5, 3, 3, &got) &&
                  rgbaMatches(got, expectedRgbOpaque(rgb), 0),
              "RGB 5x3 opaque, no red-only swizzle leak");
    }
    {
        const auto rgba = rgbaPattern(6, 3, 19, 64);
        const auto rgb = rgbPattern(6, 3, 19);
        std::vector<std::uint8_t> got;
        check(uploadAndRead(uploader, shader, fbo, makeKey("rgba"), rgba.data(), 6, 3, 4, &got) &&
                  rgbaMatches(got, expectedRgbOpaque(rgb), 0),
              "RGBA source RGB preserved; shader writes opaque alpha");
    }
    const auto gray = grayRamp(8, 4, 5);
    const float exposure = 2.0f;
    const float invGamma = 0.5f;
    shader.setAdjust(exposure, invGamma);
    std::vector<std::uint8_t> got;
    check(uploadAndRead(uploader, shader, fbo, makeKey("adjust-r8"), gray.data(), 8, 4, 1, &got) &&
              rgbaMatches(got, expectedAdjustedGray(gray, exposure, invGamma), 2),
          "R8 setAdjust exposure 2 invGamma 0.5 vs CPU, tolerance 2");
    shader.setAdjust(1.0f, 1.0f);
}

void checkOverlayMix(QOpenGLFunctions *gl, GpuTileUploader &uploader, ImageBlitterShader &shader,
                     PixelFbo &fbo)
{
    const auto base = rgbPattern(6, 3, 33);
    const auto overlay = rgbaPattern(6, 3, 90, 128);
    const float overlayAlpha = 0.5f;
    check(uploader.ensure(makeKey("overlay-base"), base.data(), 6, 3, 3), "overlay base upload");
    check(uploader.ensure(makeKey("overlay-tex"), overlay.data(), 6, 3, 4),
          "overlay texture upload");
    shader.setOverlay(static_cast<unsigned int>(uploader.handle(makeKey("overlay-tex"))),
                      overlayAlpha);
    if (!fbo.create(gl, 6, 3))
    {
        check(false, "overlay FBO");
        shader.setOverlay(0, 0.0f);
        return;
    }
    fbo.bind();
    shader.blit(static_cast<unsigned int>(uploader.handle(makeKey("overlay-base"))),
                QRect(0, 0, 6, 3), QRect(0, 0, 6, 3), QRect(0, 0, 6, 3), QSize(6, 3));
    const auto got = fbo.readTopLeft();
    check(rgbaMatches(got, expectedOverlayMix(base, overlay, overlayAlpha), 2),
          "overlay alpha mix vs CPU, tolerance 2");
    shader.setOverlay(0, 0.0f);
}

void checkNullContextBlit(ScratchContext &ctx, ImageBlitterShader &shader)
{
    ctx.context.doneCurrent();
    shader.blit(1, QRect(0, 0, 2, 2), QRect(0, 0, 2, 2), QRect(0, 0, 2, 2), QSize(2, 2));
    check(true, "blit returns without crashing when no context is current");
    if (!ctx.context.makeCurrent(&ctx.surface))
        std::fprintf(stderr, "failed to restore context after null blit\n");
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ScratchContext ctx;
    if (!ctx.bind() || ctx.context.functions() == nullptr)
    {
        std::fprintf(stderr, "GL context creation failed\n");
        return 1;
    }
    QOpenGLFunctions *gl = ctx.context.functions();
    std::printf("GL_VENDOR=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)));
    std::printf("GL_RENDERER=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
    std::printf("GL_VERSION=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));

    qputenv("MVIEWER_GPU", "1");
    GpuTileUploader::resetEnabledCache();
    GpuTileUploader::setForceGrayExpandForTest(false);
    check(GpuTileUploader::enabled(), "uploader enabled with context and MVIEWER_GPU=1");
    const bool swizzle = GpuTileUploader::textureSwizzleSupported();
    std::printf("textureSwizzleSupported=%d\n", int(swizzle));

    ImageBlitterShader shader;
    check(shader.create() && shader.ready(), "ImageBlitterShader creates on a 3.3 core context");
    if (!shader.ready())
        return 1;

    PixelFbo fbo;
    GpuTileUploader uploader;
    shader.setAdjust(1.0f, 1.0f);
    shader.setOverlay(0, 0.0f);
    checkUnpackRestored(gl, uploader);
    checkR8WidthsAndReplace(uploader, shader, fbo);
    checkExpandAndSwizzle(shader, fbo, uploader, swizzle);
    checkRgbRgbaAndAdjust(uploader, shader, fbo);
    checkOverlayMix(gl, uploader, shader, fbo);
    checkNullContextBlit(ctx, shader);
    if (QOpenGLContext::currentContext() != &ctx.context)
        return 1;

    uploader.clear();
    std::printf("assertions: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
