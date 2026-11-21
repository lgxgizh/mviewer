// BlinkController unit tests — pure state-machine, no Qt dependency.
#include "core/compare/BlinkController.h"
#include <iostream>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << "\n";                                                  \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << "\n";                                                  \
        }                                                                                          \
    } while (0)

int main()
{
    BlinkController bc;
    CHECK(!bc.isBlinking(), "default not blinking");
    CHECK(bc.blinkIndex() == -1, "default blinkIndex -1");

    // setBlinkIndex enforces idx < imageCount, so set count first.
    bc.setImageCount(5);

    bc.setBlinkIndex(2);
    CHECK(bc.isBlinking(), "isBlinking after setBlinkIndex");
    CHECK(bc.blinkIndex() == 2, "blinkIndex == 2");

    bc.clearBlink();
    CHECK(!bc.isBlinking(), "not blinking after clear");
    CHECK(bc.blinkIndex() == -1, "blinkIndex -1 after clear");

    bc.setBlinkIndex(4);
    CHECK(bc.blinkIndex() == 4, "last image blinkable");

    bc.setBlinkIndex(0);
    CHECK(bc.blinkIndex() == 0, "first image blinkable");

    std::cout << "\nBlinkController: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
