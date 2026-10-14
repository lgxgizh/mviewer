#include "compareworkspace_prefetch.h"

#include <cstdio>

namespace
{
int g_failed = 0;

void expect(bool cond, const char *message)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL %s\n", message);
        ++g_failed;
    }
}
} // namespace

int main()
{
    expect(mviewer::ui::canReuseComparePanes(2, 2), "reuse same count");
    expect(!mviewer::ui::canReuseComparePanes(0, 2), "no reuse empty panes");
    expect(!mviewer::ui::canReuseComparePanes(2, 0), "no reuse empty frames");
    expect(!mviewer::ui::canReuseComparePanes(2, 4), "no reuse count mismatch");
    expect(!mviewer::ui::canReuseComparePanes(4, 2), "no reuse shrink");

    if (g_failed)
        return 1;
    std::printf("compare pane reuse ok\n");
    return 0;
}
