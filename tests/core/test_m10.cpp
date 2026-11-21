// M10 test entry point — folds MemoryTracker + benchmark scenario
// structural suites into the consolidated core_tests exe.
// core/test_compare.cpp owns main() and calls m10_tests().

#include <iostream>

int memorytracker_suite();
int benchmark_suite();

int m10_tests()
{
    int f = 0;
    f += memorytracker_suite();
    f += benchmark_suite();
    if (f == 0)
        std::cerr << "m10_tests: ALL PASS\n";
    else
        std::cerr << "m10_tests: " << f << " FAILURES\n";
    return f;
}
