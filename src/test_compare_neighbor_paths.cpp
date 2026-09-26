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
    const std::vector<std::string> pool{"a", "b", "c", "d", "e", "f"};
    const auto next = mviewer::ui::neighborPairPaths(pool, 0, 2, +1);
    expect(next.size() == 2 && next[0] == "c" && next[1] == "d", "next pair from 0");
    const auto prev = mviewer::ui::neighborPairPaths(pool, 2, 2, -1);
    expect(prev.size() == 2 && prev[0] == "a" && prev[1] == "b", "prev pair from 2");
    expect(mviewer::ui::neighborPairPaths(pool, 0, 2, -1).empty(), "no prev at start");
    expect(mviewer::ui::neighborPairPaths(pool, 4, 2, +1).empty(), "no next past end");
    expect(mviewer::ui::neighborPairPaths(pool, 4, 2, -1).size() == 2, "prev from near end");
    expect(mviewer::ui::neighborPairPaths({}, 0, 2, +1).empty(), "empty pool");
    expect(mviewer::ui::neighborPairPaths(pool, 0, 0, +1).empty(), "zero window");

    if (g_failed)
        return 1;
    std::printf("compare neighbor paths ok\n");
    return 0;
}
