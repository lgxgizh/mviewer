// Per-scenario benchmark library: outputs CSV with Min/Avg/Max for each scenario.
#include <QImage>
#include <QElapsedTimer>
#include <QBuffer>
#include <QCoreApplication>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cmath>
#include <iomanip>

struct BenchResult {
    std::string scenario;
    std::string name;
    double avg_ms = 0;
    double min_ms = 0;
    double max_ms = 0;
    int iterations = 0;
};

class ScenarioBenchmark {
public:
    void run(const std::string& scenario, const std::string& name,
             std::function<void()> fn, int iters = 50) {
        std::vector<double> times;
        times.reserve(iters);
        for (int i = 0; i < iters; ++i) {
            QElapsedTimer t;
            t.start();
            fn();
            times.push_back(t.nsecsElapsed() / 1e6);
        }
        double sum = 0, mn = times[0], mx = times[0];
        for (double v : times) { sum += v; mn = std::min(mn, v); mx = std::max(mx, v); }
        BenchResult r{scenario, name, sum/iters, mn, mx, iters};
        m_results.push_back(r);
        std::cout << "  " << scenario << "::" << name
                  << "  avg=" << std::fixed << std::setprecision(3) << r.avg_ms
                  << "ms  min=" << r.min_ms << "ms  max=" << r.max_ms << "ms" << std::endl;
    }

    void writeCsv(const std::string& path) const {
        std::ofstream f(path);
        f << "scenario,name,avg_ms,min_ms,max_ms,iterations\n";
        for (const auto& r : m_results)
            f << r.scenario << "," << r.name << ","
              << r.avg_ms << "," << r.min_ms << "," << r.max_ms << "," << r.iterations << "\n";
        std::cout << "CSV written: " << path << " (" << m_results.size() << " rows)" << std::endl;
    }

private:
    std::vector<BenchResult> m_results;
};
