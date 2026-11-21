#include "compare_diff_renderer.h"

#include <QCoreApplication>
#include <QImage>
#include <iostream>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

using namespace mviewer::ui;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    CompareDiffRenderer renderer;
    CHECK(renderer.threshold() == 0, "initial threshold is 0");
    CHECK(renderer.splitRatio() == 0.5, "initial splitRatio is 0.5");
    CHECK(renderer.blendMode() == DiffBlendMode::Difference,
          "initial blendMode is Difference");
    CHECK(!renderer.isBlinkActive(), "initial blink is not active");

    bool thEmitted = false;
    QObject::connect(&renderer, &CompareDiffRenderer::thresholdChanged,
                     [&](uint8_t th) { thEmitted = (th == 42); });
    renderer.setThreshold(42);
    CHECK(thEmitted, "thresholdChanged emitted");
    CHECK(renderer.threshold() == 42, "threshold updated to 42");

    bool ratioEmitted = false;
    QObject::connect(&renderer, &CompareDiffRenderer::splitRatioChanged,
                     [&](double r) { ratioEmitted = std::abs(r - 0.75) < 1e-4; });
    renderer.setSplitRatio(0.75);
    CHECK(ratioEmitted, "splitRatioChanged emitted");
    CHECK(std::abs(renderer.splitRatio() - 0.75) < 1e-4, "splitRatio updated to 0.75");

    bool modeEmitted = false;
    QObject::connect(&renderer, &CompareDiffRenderer::blendModeChanged,
                     [&](DiffBlendMode m) { modeEmitted = (m == DiffBlendMode::Swipe); });
    renderer.setBlendMode(DiffBlendMode::Swipe);
    CHECK(modeEmitted, "blendModeChanged emitted");
    CHECK(renderer.blendMode() == DiffBlendMode::Swipe, "blendMode updated to Swipe");

    bool blinkEmitted = false;
    QObject::connect(&renderer, &CompareDiffRenderer::blinkStateChanged,
                     [&](bool b) { blinkEmitted = b; });
    renderer.setBlinkActive(true);
    CHECK(blinkEmitted, "blinkStateChanged emitted");
    CHECK(renderer.isBlinkActive(), "blink is active");

    // Test renderSplit horizontal (left/right along X)
    QImage imgA(100, 100, QImage::Format_ARGB32_Premultiplied);
    imgA.fill(QColor(255, 0, 0)); // Red
    QImage imgB(100, 100, QImage::Format_ARGB32_Premultiplied);
    imgB.fill(QColor(0, 0, 255)); // Blue

    QImage splitH = CompareDiffRenderer::renderSplit(imgA, imgB, 0.5, Qt::Horizontal);
    CHECK(!splitH.isNull(), "horizontal split rendered");
    CHECK(splitH.width() == 100 && splitH.height() == 100, "split dimensions 100x100");
    CHECK(splitH.pixelColor(25, 50) == QColor(255, 0, 0), "left half is red");
    CHECK(splitH.pixelColor(75, 50) == QColor(0, 0, 255), "right half is blue");

    // Test renderSplit default orientation (defaults to Qt::Horizontal)
    QImage splitDef = CompareDiffRenderer::renderSplit(imgA, imgB, 0.5);
    CHECK(!splitDef.isNull(), "default split rendered");
    CHECK(splitDef.pixelColor(25, 50) == QColor(255, 0, 0), "default split left half is red");
    CHECK(splitDef.pixelColor(75, 50) == QColor(0, 0, 255), "default split right half is blue");

    // Test renderSplit vertical (top/bottom along Y)
    QImage splitV = CompareDiffRenderer::renderSplit(imgA, imgB, 0.5, Qt::Vertical);
    CHECK(!splitV.isNull(), "vertical split rendered");
    CHECK(splitV.pixelColor(50, 25) == QColor(255, 0, 0), "top half is red");
    CHECK(splitV.pixelColor(50, 75) == QColor(0, 0, 255), "bottom half is blue");

    // Test computeDifference
    ImageData a = makeImageData(10, 10, PixelFormat::RGB24);
    std::fill(a.buffer->begin(), a.buffer->end(), 100);
    ImageData b = makeImageData(10, 10, PixelFormat::RGB24);
    std::fill(b.buffer->begin(), b.buffer->end(), 100);

    ImageData diffZero = CompareDiffRenderer::computeDifference(a, b, 0);
    CHECK(!diffZero.isNull(), "diff computed for identical images");
    bool allZero = true;
    for (uint8_t v : *diffZero.buffer)
    {
        if (v != 0)
            allZero = false;
    }
    CHECK(allZero, "diff of identical images is all zero");

    std::fill(b.buffer->begin(), b.buffer->end(), 130);
    ImageData diffNonZero = CompareDiffRenderer::computeDifference(a, b, 0);
    CHECK(!diffNonZero.isNull(), "diff computed for distinct images");
    bool hasDiff = false;
    for (uint8_t v : *diffNonZero.buffer)
    {
        if (v > 0)
            hasDiff = true;
    }
    CHECK(hasDiff, "diff of distinct images has non-zero values");

    std::cout << "All CompareDiffRenderer tests passed!" << std::endl;
    return 0;
}
