#include "core/benchmark/Benchmark.h"

Benchmark& Benchmark::instance()
{
    static Benchmark inst;
    return inst;
}

void Benchmark::run(const std::string& name, std::function<void()> fn, int iterations)
{
    if (!fn || iterations <= 0) return;
    Result r;
    r.name = name;
    r.iterations = iterations;
    r.minMs = 1e18;
    r.maxMs = 0;
    double total = 0;
    for (int i = 0; i < iterations; ++i) {
        Timer t;
        t.start();
        fn();
        double ms = t.elapsedMs();
        total += ms;
        if (ms < r.minMs) r.minMs = ms;
        if (ms > r.maxMs) r.maxMs = ms;
    }
    r.avgMs = total / iterations;
    m_results.push_back(r);
}

void Benchmark::run(const std::string& name, std::function<void(int)> fn, int iterations)
{
    run(name, [fn, iterations]() { fn(iterations); }, iterations);
}

void Benchmark::report() const
{
    std::cout << "=== Benchmark Report ===" << std::endl;
    std::cout << std::left << std::setw(40) << "Name"
              << std::right << std::setw(10) << "Avg(ms)"
              << std::setw(10) << "Min(ms)"
              << std::setw(10) << "Max(ms)"
              << std::setw(8) << "Iter" << std::endl;
    std::cout << std::string(78, '-') << std::endl;
    for (const auto& r : m_results) {
        std::cout << std::left << std::setw(40) << r.name
                  << std::right << std::setw(10) << std::fixed << std::setprecision(3) << r.avgMs
                  << std::setw(10) << std::fixed << std::setprecision(3) << r.minMs
                  << std::setw(10) << std::fixed << std::setprecision(3) << r.maxMs
                  << std::setw(8) << r.iterations << std::endl;
    }
}

void Benchmark::clear()
{
    m_results.clear();
}
