#include "core/command/CommandRegistry.h"
#include "core/command/ICommand.h"

#include <QCoreApplication>
#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (c)                                                                                     \
        {                                                                                          \
            printf("  PASS: %s\n", m);                                                             \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", m);                                                             \
            g_fail++;                                                                              \
        }                                                                                          \
        fflush(stdout);                                                                            \
    } while (0)

// Map a Qt key + modifier to a registered command id (or "" if none).
static std::string idFor(int key, int mods)
{
    ICommand *c = CommandRegistry::instance().findByShortcut(key, mods);
    return c ? c->id() : std::string();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Product-critical shortcuts that must resolve to the right command.
    // Qt::ControlModifier == 0x02000000, Qt::Key_O == 0x4f, Qt::Key_S == 0x53,
    // Qt::Key_M == 0x4d, Qt::Key_H == 0x48, Qt::Key_Delete == 0x1000007.
    const int CTRL = 0x02000000;
    CHECK(idFor(0x4f, CTRL) == "open_directory", "Ctrl+O -> open_directory");
    CHECK(idFor(0x53, CTRL) == "export", "Ctrl+S -> export");
    CHECK(idFor(0x4d, CTRL) == "compare", "Ctrl+M -> compare");
    CHECK(idFor(0x48, CTRL) == "toggle_histogram", "Ctrl+H -> toggle_histogram");
    CHECK(idFor(0x1000007, 0) == "delete_image", "Delete -> delete_image");

    printf("\n=== M9-6 Polish acceptance: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
