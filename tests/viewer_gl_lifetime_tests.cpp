#include "gpu/GlOwnerContext.h"
#include "gpu/GpuTileUploader.h"
#include "gpu/ImageBlitterShader.h"
#include "imageviewer.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QOpenGLFunctions>

#include <array>
#include <cstdio>
#include <memory>

namespace
{
int passed = 0;
int failed = 0;
void check(bool ok, const char *label)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (ok)
        ++passed;
    else
        ++failed;
}

struct Scratch
{
    std::unique_ptr<QOpenGLContext> context = std::make_unique<QOpenGLContext>();
    QOffscreenSurface surface;
    bool bind()
    {
        if (!context)
            return false;
        if (!context->isValid())
        {
            QSurfaceFormat format;
            format.setRenderableType(QSurfaceFormat::OpenGL);
            format.setProfile(QSurfaceFormat::CoreProfile);
            format.setVersion(3, 3);
            context->setFormat(format);
            if (!context->create())
                return false;
            surface.setFormat(context->format());
            surface.create();
        }
        return surface.isValid() && context->makeCurrent(&surface);
    }
};

TileKey key()
{
    TileKey value;
    value.imageId = "lifetime";
    return value;
}

bool upload(GpuTileUploader &uploader)
{
    const std::array<std::uint8_t, 9> pixels{10, 20, 30, 40, 50, 60, 70, 80, 90};
    return uploader.ensure(key(), pixels.data(), 3, 3, 1);
}

GLuint sentinel(QOpenGLFunctions *gl)
{
    GLuint texture = 0;
    gl->glGenTextures(1, &texture);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    return texture;
}

void checkForeignAndReuse(Scratch &owner, Scratch &foreign)
{
    check(owner.bind(), "owner bind");
    ImageBlitterShader shader;
    GpuTileUploader uploader;
    check(shader.create() && upload(uploader), "owner shader and texture created");
    const auto oldHandle = static_cast<GLuint>(uploader.handle(key()));
    check(foreign.bind(), "foreign bind");
    check(uploader.handle(key()) == 0 && !uploader.isResident(key()),
          "foreign context cannot observe owner handles");
    auto *gl = foreign.context->functions();
    const GLuint texture = sentinel(gl);
    shader.destroy();
    uploader.clear();
    check(QOpenGLContext::currentContext() == foreign.context.get() &&
              foreign.context->surface() == &foreign.surface,
          "destroy/clear restore foreign context and surface");
    check(gl->glIsTexture(texture), "foreign same-namespace texture is not deleted");
    check(!shader.ready() && uploader.residentCount() == 0, "resources retired");
    shader.destroy();
    uploader.clear();
    check(QOpenGLContext::currentContext() == foreign.context.get(), "repeated teardown safe");
    gl->glDeleteTextures(1, &texture);
    check(owner.bind(), "owner rebound for reuse");
    check(!owner.context->functions()->glIsTexture(oldHandle),
          "successful owner cleanup actually deletes the owner texture");
    check(shader.create() && upload(uploader), "destroyed shader/uploader reusable");
    owner.context->doneCurrent();
    shader.destroy();
    uploader.clear();
    check(QOpenGLContext::currentContext() == nullptr, "null caller restored after teardown");
}

void checkContextDiesFirst(Scratch &foreign)
{
    Scratch owner;
    check(owner.bind(), "dying owner bind");
    ImageBlitterShader shader;
    GpuTileUploader uploader;
    check(shader.create() && upload(uploader), "resources before native-context destruction");
    check(foreign.bind(), "foreign caller before native teardown");
    auto *gl = foreign.context->functions();
    const GLuint texture = sentinel(gl);
    owner.context.reset();
    check(!shader.ready() && uploader.residentCount() == 0,
          "direct aboutToBeDestroyed retires shader and uploader map");
    check(QOpenGLContext::currentContext() == foreign.context.get() &&
              foreign.context->surface() == &foreign.surface,
          "native owner destruction restores foreign caller");
    shader.destroy();
    uploader.clear();
    check(gl->glIsTexture(texture), "wrapper teardown after context death leaves foreign intact");
    gl->glDeleteTextures(1, &texture);
    check(shader.create() && upload(uploader), "rebuild on new context after original owner dies");
}

void checkFailedActivation(Scratch &owner, Scratch &foreign)
{
    check(owner.bind(), "bind-failure fixture owner");
    ImageBlitterShader shader;
    GpuTileUploader uploader;
    check(shader.create() && upload(uploader), "bind-failure fixture resources");
    const auto oldHandle = uploader.handle(key());
    shader.forceCleanupBindFailureForTest(true);
    uploader.forceCleanupBindFailureForTest(true);
    check(foreign.bind(), "bind-failure fixture foreign");
    auto *gl = foreign.context->functions();
    const GLuint texture = sentinel(gl);
    shader.destroy();
    uploader.clear();
    check(!shader.ready() && uploader.residentCount() == 0,
          "failed owner activation abandons wrappers and invalidates map");
    check(QOpenGLContext::currentContext() == foreign.context.get() && gl->glIsTexture(texture),
          "failed activation never deletes on foreign context");
    gl->glDeleteTextures(1, &texture);
    check(owner.bind(), "owner reusable after injected failure");
    uploader.forceCleanupBindFailureForTest(false);
    check(shader.create() && upload(uploader) && uploader.handle(key()) != oldHandle,
          "no old handles reused after failed cleanup");
}

bool waitReady(ImageViewer &viewer)
{
    QElapsedTimer timer;
    timer.start();
    while (!viewer.isValid() && timer.elapsed() < 5000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return viewer.isValid();
}
} // namespace

struct ViewerGlLifetimeProbe
{
    static bool ready(ImageViewer &viewer)
    {
        return viewer.m_glOwner && viewer.m_glOwner->context() == viewer.context() &&
               viewer.m_blitterReady && viewer.m_shaderReady && viewer.m_shader &&
               viewer.m_shader->ready();
    }

    static bool reinitialize(ImageViewer &viewer)
    {
        viewer.makeCurrent();
        const bool uploaded = upload(viewer.m_gpu);
        const auto oldHandle = static_cast<GLuint>(viewer.m_gpu.handle(key()));
        viewer.initializeGL();
        const bool clean = viewer.m_gpu.residentCount() == 0 && ready(viewer) &&
                           !viewer.context()->functions()->glIsTexture(oldHandle);
        viewer.doneCurrent();
        return uploaded && clean;
    }
};

namespace
{
void checkWidgetLifecycle()
{
    QWidget first;
    QWidget second;
    auto *viewer = new ImageViewer(&first);
    viewer->resize(64, 64);
    first.resize(80, 80);
    second.resize(80, 80);
    first.show();
    viewer->show();
    check(waitReady(*viewer) && ViewerGlLifetimeProbe::ready(*viewer), "widget initial resources");
    check(ViewerGlLifetimeProbe::reinitialize(*viewer), "repeat initialize retires old resources");
    int destroyed = 0;
    auto connection = QObject::connect(
        viewer->context(), &QOpenGLContext::aboutToBeDestroyed, &first,
        [&destroyed]() { ++destroyed; }, Qt::DirectConnection);
    viewer->setParent(&second);
    second.show();
    viewer->show();
    check(waitReady(*viewer) && destroyed > 0 && ViewerGlLifetimeProbe::ready(*viewer),
          "cross-top-level reparent recreates context and resources");
    second.showFullScreen();
    QApplication::processEvents();
    second.showNormal();
    QApplication::processEvents();
    check(ViewerGlLifetimeProbe::reinitialize(*viewer), "fullscreen cycle and repeated initialize");
    QObject::disconnect(connection);
    delete viewer;
    check(true, "widget resource teardown completes");
}
} // namespace

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    qputenv("MVIEWER_GPU", "1");
    qputenv("MVIEWER_BLITTER", "shader");
    GpuTileUploader::resetEnabledCache();
    ImageBlitterShader::resetRequestedCache();
    Scratch owner;
    Scratch foreign;
    if (!owner.bind() || !foreign.bind())
        return 1;
    auto *gl = foreign.context->functions();
    std::printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
                reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)),
                reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)),
                reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));
    checkForeignAndReuse(owner, foreign);
    checkContextDiesFirst(foreign);
    checkFailedActivation(owner, foreign);
    owner.context->doneCurrent();
    checkWidgetLifecycle();
    std::printf("assertions: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
