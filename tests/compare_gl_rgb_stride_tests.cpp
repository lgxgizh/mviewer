#include "compareworkspace_composite_gl.h"
#include "compareworkspace_composite_gl_upload.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using mviewer::ui::CompareCompositeGL;
using mviewer::ui::CompareCompositeMode;
using mviewer::ui::uploadCompareRgb888;

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

QImage pattern(int width, int height, int seed)
{
    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
            image.setPixel(
                x, y,
                qRgb((seed + x * 17) % 256, (seed + y * 43) % 256, (seed + x * 11 + y * 7) % 256));
    }
    return image;
}

bool pixelsMatch(const QImage &got, const QImage &expected)
{
    if (got.size() != expected.size() || got.isNull())
        return false;
    for (int y = 0; y < got.height(); ++y)
    {
        for (int x = 0; x < got.width(); ++x)
        {
            const QRgb out = got.pixel(x, y);
            const QRgb in = expected.pixel(x, y);
            if (qRed(out) != qRed(in) || qGreen(out) != qGreen(in) || qBlue(out) != qBlue(in) ||
                qAlpha(out) != 255)
                return false;
        }
    }
    return true;
}

void checkComposite(CompareCompositeGL &comp, int width, int height, bool external)
{
    // Only fixed tiny dimensions below reach this fixture, not product inputs.
    const QImage expectedA = pattern(width, height, 11);
    const QImage expectedB = pattern(width, height, 97);
    const int stride = ((width * 3 + 3) & ~3) + 8;
    std::vector<std::uint8_t> storageA(static_cast<std::size_t>(stride * height), 0xA5);
    std::vector<std::uint8_t> storageB(static_cast<std::size_t>(stride * height), 0x5A);
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(storageA.data() + y * stride, expectedA.constScanLine(y),
                    static_cast<std::size_t>(width * 3));
        std::memcpy(storageB.data() + y * stride, expectedB.constScanLine(y),
                    static_cast<std::size_t>(width * 3));
    }
    const QImage paneA = external ? QImage(static_cast<const uchar *>(storageA.data()), width,
                                           height, stride, QImage::Format_RGB888)
                                  : expectedA;
    const QImage paneB = external ? QImage(static_cast<const uchar *>(storageB.data()), width,
                                           height, stride, QImage::Format_RGB888)
                                  : expectedB;
    char label[100];
    const QImage outA = comp.composite(CompareCompositeMode::Overlay, paneA, paneB,
                                       QSize(width, height), 0, 0.0, 8);
    std::snprintf(label, sizeof(label), "%dx%d %s stride pane A", width, height,
                  external ? "external padded" : "default");
    check(pixelsMatch(outA, expectedA), label);
    const QImage outB = comp.composite(CompareCompositeMode::Overlay, paneA, paneB,
                                       QSize(width, height), 0, 1.0, 8);
    std::snprintf(label, sizeof(label), "%dx%d %s stride pane B", width, height,
                  external ? "external padded" : "default");
    check(pixelsMatch(outB, expectedB), label);
}

void checkUnpackRestored(QOpenGLFunctions *gl)
{
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
    unsigned int tex = 0;
    gl->glGenTextures(1, &tex);
    const bool uploaded = uploadCompareRgb888(gl, tex, pattern(6, 3, 11));
    GLint alignment = 0;
    GLint rowLength = 0;
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    check(uploaded && alignment == 8, "unpack alignment restored to 8");
    check(uploaded && rowLength == 17, "unpack row length restored to 17");
    check(!uploadCompareRgb888(gl, tex, QImage()), "null image rejected");
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    check(alignment == 8 && rowLength == 17, "null rejection preserves unpack state");
    gl->glDeleteTextures(1, &tex);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    CompareCompositeGL comp;
    if (!comp.ensureContext(nullptr) || QOpenGLContext::currentContext() == nullptr)
    {
        std::fprintf(stderr, "GL context creation failed\n");
        return 1;
    }
    QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
    if (gl == nullptr)
    {
        std::fprintf(stderr, "GL functions unavailable\n");
        return 1;
    }
    std::printf("GL_VENDOR=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)));
    std::printf("GL_RENDERER=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
    std::printf("GL_VERSION=%s\n", reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));
    checkUnpackRestored(gl);
    check(comp.composite(CompareCompositeMode::Overlay, QImage(), QImage(), QSize(2, 2), 0, 0.0, 8)
              .isNull(),
          "composite rejects missing panes");
    for (int width : {5, 6, 7, 8})
    {
        checkComposite(comp, width, 3, false);
        checkComposite(comp, width, 3, true);
    }
    checkComposite(comp, 6, 4, false);
    checkComposite(comp, 7, 5, true);
    std::printf("assertions: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
