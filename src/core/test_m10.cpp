// M10 test entry point — folds MemoryTracker + benchmark scenario
// structural suites into the consolidated core_tests exe (known-good link env).
// Invoked from test_compare.cpp's main() via m10_tests().
// Externs defined in test_memorytracker.cpp / test_benchmark.cpp.

#include <iostream>

int memorytracker_suite();
int benchmark_suite();

int m10_tests()
{
    int f = 0;
    f += memorytracker_suite();
    f += benchmark_suite();
    if (f == 0)
    {
        std::cerr << "m10_tests: ALL PASS\n";
        return 0;
    }
    std::cerr << "m10_tests: " << f << " FAILURES\n";
    return 1;
}
