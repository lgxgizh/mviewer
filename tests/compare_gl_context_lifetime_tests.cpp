#include "compareworkspace_composite_gl.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <cstdio>

using mviewer::ui::CompareCompositeGL;
using mviewer::ui::CompareCompositeMode;

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

    bool isCurrent() const
    {
        return QOpenGLContext::currentContext() == &context && context.surface() == &surface;
    }

    ~ScratchContext()
    {
        if (QOpenGLContext::currentContext() == &context)
            context.doneCurrent();
    }
};

QImage solidPane(int width, int height, int red, int green, int blue)
{
    QImage image(width, height, QImage::Format_RGB888);
    image.fill(QColor(red, green, blue));
    return image;
}

QImage runComposite(CompareCompositeGL &comp)
{
    return comp.composite(CompareCompositeMode::Overlay, solidPane(4, 4, 10, 20, 30),
                          solidPane(4, 4, 40, 50, 60), QSize(4, 4), 0, 1.0, 8);
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    ScratchContext probe;
    if (!probe.bind() || probe.context.functions() == nullptr)
    {
        std::fprintf(stderr, "GL probe context failed\n");
        return 1;
    }
    QOpenGLFunctions *gl = probe.context.functions();
    std::printf("GL_VENDOR=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)));
    std::printf("GL_RENDERER=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
    std::printf("GL_VERSION=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));
    probe.context.doneCurrent();
    check(QOpenGLContext::currentContext() == nullptr, "probe released to no current context");

    CompareCompositeGL first;
    check(first.ensureContext(nullptr) && first.isReady(), "ensureContext from null current");
    check(QOpenGLContext::currentContext() == nullptr, "ensureContext restores null current");

    ScratchContext caller;
    if (!caller.bind())
    {
        std::fprintf(stderr, "GL caller context failed\n");
        return 1;
    }
    check(caller.isCurrent(), "caller context is current before composite");
    check(!runComposite(first).isNull(), "composite succeeds with caller current");
    check(caller.isCurrent(), "composite success restores caller context and surface");

    check(first.composite(CompareCompositeMode::Overlay, QImage(), QImage(), QSize(4, 4), 0, 0.0, 8)
              .isNull(),
          "composite rejects missing panes");
    check(caller.isCurrent(), "composite failure restores caller context and surface");

    first.destroy();
    check(!first.isReady(), "destroy clears ready");
    check(caller.isCurrent(), "destroy restores caller context and surface");
    first.destroy();
    first.destroy();
    check(!first.isReady(), "repeated destroy stays not ready");
    check(caller.isCurrent(), "repeated destroy restores caller context and surface");

    check(first.ensureContext(nullptr) && first.isReady(),
          "ensureContext reuses a still-valid context after destroy");
    check(caller.isCurrent(), "reused ensureContext restores caller context and surface");
    check(!runComposite(first).isNull(), "composite works after destroy and reuse");
    check(caller.isCurrent(), "post-reuse composite restores caller context and surface");

    CompareCompositeGL second;
    check(second.ensureContext(nullptr) && second.isReady(), "second compositor is ready");
    check(caller.isCurrent(), "second ensureContext leaves caller current");
    first.destroy();
    check(caller.isCurrent(), "destroy first while caller current keeps caller");
    check(!runComposite(second).isNull(),
          "second compositor still composites after foreign destroy");
    check(caller.isCurrent(), "second composite restores caller context and surface");
    check(second.ensureContext(nullptr) && second.isReady() && caller.isCurrent(),
          "ensureContext no-op keeps caller current");

    CompareCompositeGL shared;
    check(shared.ensureContext(&caller.context) && shared.isReady(),
          "ensureContext with a share context succeeds");
    check(caller.isCurrent(), "shared ensureContext restores caller context and surface");
    check(!runComposite(shared).isNull(), "shared compositor composites");
    check(caller.isCurrent(), "shared composite restores caller context and surface");

    std::printf("assertions: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
