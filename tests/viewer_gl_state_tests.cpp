#include "gpu/GpuTileUploader.h"
#include "gpu/ImageBlitterShader.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>

#include <array>
#include <cstdio>

namespace
{
int failures = 0;
int passed = 0;

void check(bool ok, const char *label)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (ok)
        ++passed;
    else
        ++failures;
}

using State = std::array<GLint, 18>;

State capture(QOpenGLExtraFunctions *gl)
{
    State result{};
    const GLenum properties[] = {
        GL_CURRENT_PROGRAM,      GL_VERTEX_ARRAY_BINDING, GL_ARRAY_BUFFER_BINDING,
        GL_ACTIVE_TEXTURE,       GL_BLEND_SRC_RGB,        GL_BLEND_DST_RGB,
        GL_BLEND_SRC_ALPHA,      GL_BLEND_DST_ALPHA,      GL_BLEND_EQUATION_RGB,
        GL_BLEND_EQUATION_ALPHA, GL_UNPACK_ALIGNMENT,     GL_UNPACK_ROW_LENGTH,
        GL_FRAMEBUFFER_BINDING};
    for (std::size_t i = 0; i < 13; ++i)
        gl->glGetIntegerv(properties[i], &result[i]);
    result[13] = gl->glIsEnabled(GL_BLEND);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &result[14]);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &result[15]);
    gl->glActiveTexture(GL_TEXTURE3);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &result[16]);
    gl->glActiveTexture(static_cast<GLenum>(result[3]));
    result[17] = static_cast<GLint>(gl->glGetError());
    return result;
}

bool createSentinel(QOpenGLShaderProgram &program)
{
    return program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(#version 330 core
void main() { gl_Position = vec4(0.0); }
)") && program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(#version 330 core
out vec4 color;
void main() { color = vec4(1.0); }
)") && program.link() &&
           program.bind();
}

void exercise(QOpenGLExtraFunctions *gl)
{
    QOpenGLFramebufferObject target(3, 3);
    check(target.isValid() && target.bind(), "state fixture FBO");
    GLuint vao = 0;
    GLuint buffer = 0;
    GLuint textures[3] = {};
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    gl->glGenBuffers(1, &buffer);
    gl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    gl->glBufferData(GL_ARRAY_BUFFER, 64, nullptr, GL_STATIC_DRAW);
    gl->glGenTextures(3, textures);
    const GLenum units[] = {GL_TEXTURE0, GL_TEXTURE1, GL_TEXTURE3};
    for (int i = 0; i < 3; ++i)
    {
        gl->glActiveTexture(units[i]);
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    gl->glEnable(GL_BLEND);
    gl->glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
    gl->glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    QOpenGLShaderProgram sentinel;
    if (!createSentinel(sentinel))
    {
        check(false, "sentinel program creation");
        return;
    }
    const State before = capture(gl);
    check(before[17] == GL_NO_ERROR, "clean GL error state before operations");
    ImageBlitterShader shader;
    check(shader.create(), "shader creation");
    check(capture(gl) == before, "create restores caller program/VAO/VBO/textures/blend");
    GpuTileUploader uploader;
    TileKey key;
    key.imageId = "state-probe";
    std::array<std::uint8_t, 9> pixels{20, 40, 60, 80, 100, 120, 140, 160, 180};
    check(uploader.ensure(key, pixels.data(), 3, 3, 1), "upload succeeds");
    check(capture(gl) == before, "upload preserves caller active-unit binding and unpack state");
    pixels[0] = 55;
    check(uploader.ensure(key, pixels.data(), 3, 3, 1), "replace succeeds");
    check(capture(gl) == before, "replace preserves caller binding and unpack state");
    uploader.setMagnifyNearest(uploader.handle(key), true);
    check(capture(gl) == before, "filter change preserves caller texture binding");
    for (bool blend : {false, true})
    {
        if (blend)
            gl->glEnable(GL_BLEND);
        else
            gl->glDisable(GL_BLEND);
        const State drawBefore = capture(gl);
        gl->glViewport(0, 0, 3, 3);
        shader.blit(static_cast<unsigned int>(uploader.handle(key)), QRect(0, 0, 3, 3),
                    QRect(0, 0, 3, 3), QRect(0, 0, 3, 3), QSize(3, 3));
        check(capture(gl) == drawBefore,
              "blit restores program/VAO/VBO/units/bindings/blend enable and factors");
    }
    uploader.clear();
    shader.destroy();
    sentinel.release();
    gl->glBindVertexArray(0);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glDeleteTextures(3, textures);
    gl->glDeleteBuffers(1, &buffer);
    gl->glDeleteVertexArrays(1, &vao);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    QOpenGLContext context;
    context.setFormat(format);
    if (!context.create())
        return 1;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid() || !context.makeCurrent(&surface))
        return 1;
    auto *gl = context.extraFunctions();
    std::printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
                reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)),
                reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)),
                reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));
    qputenv("MVIEWER_GPU", "1");
    GpuTileUploader::resetEnabledCache();
    exercise(gl);
    context.doneCurrent();
    std::printf("assertions: %d passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
