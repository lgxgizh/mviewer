// Review P2-08: the A/B switch that selects the fragment-shader blitter.
//
// Deliberately does NOT create an OpenGL context: this suite runs headless,
// where QOpenGLContext::create() is not available (and can fault). The switch
// is what makes the shader path opt-in, so its contract is worth pinning on its
// own - compile/link/draw parity needs real hardware and a separate harness.
#include "gpu/ImageBlitterShader.h"

#include <QCoreApplication>

#include <iostream>

namespace
{
int g_failures = 0;

void check(bool ok, const char *what)
{
    std::cout << (ok ? "[ok] " : "[FAIL] ") << what << "\n";
    if (!ok)
        ++g_failures;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    setbuf(stdout, nullptr);

    ImageBlitterShader::resetRequestedCache();
    const bool unsetMeansQt = !ImageBlitterShader::requested();
    check(unsetMeansQt, "MVIEWER_BLITTER unset keeps Qt's blitter");

    qputenv("MVIEWER_BLITTER", "shader");
    ImageBlitterShader::resetRequestedCache();
    check(ImageBlitterShader::requested(), "MVIEWER_BLITTER=shader selects the shader");

    qputenv("MVIEWER_BLITTER", "SHADER");
    ImageBlitterShader::resetRequestedCache();
    check(ImageBlitterShader::requested(), "the switch is case-insensitive");

    qputenv("MVIEWER_BLITTER", "qt");
    ImageBlitterShader::resetRequestedCache();
    check(!ImageBlitterShader::requested(), "MVIEWER_BLITTER=qt keeps Qt's blitter");

    qunsetenv("MVIEWER_BLITTER");
    ImageBlitterShader::resetRequestedCache();
    check(!ImageBlitterShader::requested(), "clearing the variable restores the default");

    // Cached between resets: the hot path must not re-read the environment.
    qputenv("MVIEWER_BLITTER", "shader");
    ImageBlitterShader::resetRequestedCache();
    const bool first = ImageBlitterShader::requested();
    qputenv("MVIEWER_BLITTER", "qt");
    const bool second = ImageBlitterShader::requested();
    check(first && second, "the value is cached until resetRequestedCache() is called");
    ImageBlitterShader::resetRequestedCache();
    check(!ImageBlitterShader::requested(), "a later reset picks up the new value");

    if (g_failures != 0)
    {
        std::cout << "blitshader_flag_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "blitshader_flag_tests: PASS\n";
    return 0;
}
