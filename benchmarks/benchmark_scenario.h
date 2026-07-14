// Per-scenario benchmark library: outputs CSV with Min/Avg/Max for each
// scenario. Supports baseline comparison with degradation threshold (RFC-012
// EE).
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct BenchResult {
  std::string scenario;
  std::string name;
  double avg_ms = 0;
  double min_ms = 0;
  double max_ms = 0;
  int iterations = 0;
};

struct BaselineEntry {
  double avg_ms = 0;
  double min_ms = 0;
  double max_ms = 0;
};

// Compare result vs baseline; returns true if within threshold (e.g., 1.2 =
// +20%)
struct CompareResult {
  std::string scenario;
  std::string name;
  double current_avg_ms = 0;
  double baseline_avg_ms = 0;
  double ratio = 0; // current / baseline (1.0 = same, >1 = slower)
  bool passed = false;
};

class ScenarioBenchmark {
public:
  void run(const std::string &scenario, const std::string &name,
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
    for (double v : times) {
      sum += v;
      mn = std::min(mn, v);
      mx = std::max(mx, v);
    }
    BenchResult r{scenario, name, sum / iters, mn, mx, iters};
    m_results.push_back(r);
    std::cout << "  " << scenario << "::" << name << "  avg=" << std::fixed
              << std::setprecision(3) << r.avg_ms << "ms  min=" << r.min_ms
              << "ms  max=" << r.max_ms << "ms" << std::endl;
  }

  void writeCsv(const std::string &path) const {
    std::ofstream f(path);
    f << "scenario,name,avg_ms,min_ms,max_ms,iterations\n";
    for (const auto &r : m_results)
      f << r.scenario << "," << r.name << "," << r.avg_ms << "," << r.min_ms
        << "," << r.max_ms << "," << r.iterations << "\n";
    std::cout << "CSV written: " << path << " (" << m_results.size() << " rows)"
              << std::endl;
  }

  // Load a baseline CSV (format: scenario,name,avg_ms,min_ms,max_ms,iterations)
  static std::unordered_map<std::string, BaselineEntry>
  loadBaseline(const std::string &path) {
    std::unordered_map<std::string, BaselineEntry> map;
    std::ifstream f(path);
    if (!f.is_open()) {
      std::cerr << "baseline not found: " << path << std::endl;
      return map;
    }
    std::string line;
    std::getline(f, line); // header
    while (std::getline(f, line)) {
      std::istringstream ss(line);
      std::string scenario, name, avg_s, min_s, max_s, it_s;
      if (!std::getline(ss, scenario, ','))
        continue;
      if (!std::getline(ss, name, ','))
        continue;
      if (!std::getline(ss, avg_s, ','))
        continue;
      if (!std::getline(ss, min_s, ','))
        continue;
      if (!std::getline(ss, max_s, ','))
        continue;
      BaselineEntry e;
      e.avg_ms = std::stod(avg_s);
      e.min_ms = std::stod(min_s);
      e.max_ms = std::stod(max_s);
      map[scenario + "::" + name] = e;
    }
    return map;
  }

  // Compare current results vs baseline with threshold ratio
  // threshold = 1.2 means up to +20% degradation is allowed
  std::vector<CompareResult>
  compare(const std::unordered_map<std::string, BaselineEntry> &baseline,
          double threshold = 1.2) const {
    std::vector<CompareResult> out;
    for (const auto &r : m_results) {
      CompareResult c;
      c.scenario = r.scenario;
      c.name = r.name;
      c.current_avg_ms = r.avg_ms;
      auto it = baseline.find(r.scenario + "::" + r.name);
      if (it == baseline.end()) {
        c.baseline_avg_ms = 0;
        c.ratio = 0;
        c.passed = true; // no baseline = pass
      } else {
        c.baseline_avg_ms = it->second.avg_ms;
        c.ratio = (it->second.avg_ms > 0) ? r.avg_ms / it->second.avg_ms : 0;
        c.passed = (c.ratio <= threshold);
      }
      out.push_back(c);
    }
    return out;
  }

  const std::vector<BenchResult> &results() const { return m_results; }

private:
  std::vector<BenchResult> m_results;
};
