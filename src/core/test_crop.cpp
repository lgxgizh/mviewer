// CropCommand tests: crop extracts the correct region; undo restores exactly.
#include "core/command/CommandStack.h"
#include "core/command/CropCommand.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "domain/Selection.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

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

// Build a w*h RGB24 image with a unique color per pixel: pixel (x,y) -> rgb
// (x, y, 128). Makes region extraction trivially verifiable.
static ImageData makeGradient(int w, int h)
{
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = d.buffer->data() + (static_cast<size_t>(y) * w + x) * 3;
            p[0] = static_cast<uint8_t>(x);
            p[1] = static_cast<uint8_t>(y);
            p[2] = 128;
        }
    return d;
}

int main()
{
    std::cout << "[CropCommand tests]\n";

    // 1) Crop a 4x4 region from a 10x10 image (origin 2,3).
    {
        auto frame = std::make_shared<ImageFrame>();
        frame->setPixels(makeGradient(10, 10));
        CHECK(frame->width() == 10 && frame->height() == 10, "source is 10x10");

        mviewer::domain::Selection sel{2, 3, 4, 4};
        CropCommand cmd(frame, sel);
        CHECK(cmd.canExecute(), "crop canExecute with valid selection");
        cmd.execute();

        CHECK(frame->width() == 4 && frame->height() == 4, "cropped to 4x4");
        // Top-left cropped pixel should be source (2,3): rgb (2,3,128).
        const uint8_t *tl = frame->pixels().buffer->data();
        CHECK(tl[0] == 2 && tl[1] == 3 && tl[2] == 128, "top-left pixel == source (2,3)");
        // Bottom-right cropped pixel should be source (5,6): rgb (5,6,128).
        const uint8_t *br = frame->pixels().buffer->data() + (static_cast<size_t>(3) * 4 + 3) * 3;
        CHECK(br[0] == 5 && br[1] == 6 && br[2] == 128, "bottom-right pixel == source (5,6)");

        cmd.undo();
        CHECK(frame->width() == 10 && frame->height() == 10, "undo restores 10x10");
        const uint8_t *restored = frame->pixels().buffer->data();
        CHECK(restored[0] == 0 && restored[1] == 0, "undo restores original top-left (0,0)");
    }

    // 2) Out-of-bounds selection is clamped (no crash, valid intersection).
    {
        auto frame = std::make_shared<ImageFrame>();
        frame->setPixels(makeGradient(10, 10));
        mviewer::domain::Selection sel{8, 8, 10, 10}; // extends past bounds
        CropCommand cmd(frame, sel);
        cmd.execute();
        CHECK(frame->width() == 2 && frame->height() == 2, "clamped to 2x2 intersection");
        cmd.undo();
        CHECK(frame->width() == 10 && frame->height() == 10, "undo after clamp restores 10x10");
    }

    // 3) Empty selection is a no-op (cannot execute).
    {
        auto frame = std::make_shared<ImageFrame>();
        frame->setPixels(makeGradient(10, 10));
        mviewer::domain::Selection sel{0, 0, 0, 0};
        CropCommand cmd(frame, sel);
        CHECK(!cmd.canExecute(), "empty selection cannot execute");
        cmd.execute();
        CHECK(frame->width() == 10, "execute on empty selection leaves frame unchanged");
    }

    // 4) Crop integrates with CommandStack (undo/redo).
    {
        auto frame = std::make_shared<ImageFrame>();
        frame->setPixels(makeGradient(8, 8));
        CommandStack stack;
        stack.execute(std::make_unique<CropCommand>(frame, mviewer::domain::Selection{1, 1, 3, 3}));
        CHECK(frame->width() == 3 && frame->height() == 3, "stack execute crops to 3x3");
        stack.undo();
        CHECK(frame->width() == 8 && frame->height() == 8, "stack undo restores 8x8");
        stack.redo();
        CHECK(frame->width() == 3 && frame->height() == 3, "stack redo crops to 3x3 again");
    }

    std::cout << "CropCommand tests done\n";
    return 0;
}
