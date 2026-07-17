#pragma once

#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// 简易基准测试框架
class Benchmark
{
  public:
    struct Result
    {
        std::string name;
        double avgMs = 0;
        double minMs = 0;
        double maxMs = 0;
        int iterations = 0;
    };

    static Benchmark &instance();

    void run(const std::string &name, std::function<void()> fn, int iterations = 10);
    void run(const std::string &name, std::function<void(int)> fn, int iterations = 10);

    void report() const;
    // Emit a machine-readable CSV (name,avg_ms,min_ms,max_ms,iterations) to `path`.
    // Returns true if the file was written successfully. If `path` is empty,
    // writes "<exe_dir>/benchmark_results.csv".
    bool reportCsv(const std::string &path = "") const;
    void clear();

  private:
    std::vector<Result> m_results;
};

// 计时辅助类
class Timer
{
  public:
    void start()
    {
        m_start = std::chrono::high_resolution_clock::now();
    }
    double elapsedMs() const
    {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - m_start).count();
    }

  private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};
