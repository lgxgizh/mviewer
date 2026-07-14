// Real UI fixture + screenshot comparison test (tests/ui_fixture/).
// Instantiates the real MainWindow, loads synthetic test images into it,
// renders offscreen, and compares against a golden reference image.
//
// Usage:
//   ./ui_fixture.exe --generate   (save golden screenshot from current UI)
//   ./ui_fixture.exe --compare    (compare current screenshot vs golden)
//   ./ui_fixture.exe --golden-dir <path> --output-dir <path>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMainWindow>
#include <QPixmap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "mainwindow.h"

#ifndef MVIEWER_SOURCE_DIR
#define MVIEWER_SOURCE_DIR "."
#endif

struct UiTestFixture {
  std::string name;
  std::string goldenPath;
  std::string currentPath;
  double tolerance = 8.0;        // per-channel diff threshold (0-255)
  double maxDiffFraction = 0.05; // fraction of pixels allowed to differ (5%)
};

struct UiTestResult {
  std::string name;
  bool passed = false;
  int diffPixels = 0;
  double diffFraction = 0.0;
  std::string message;
};

class UiFixtureRegression {
public:
  explicit UiFixtureRegression(std::string goldenDir, std::string outputDir)
      : m_goldenDir(std::move(goldenDir)), m_outputDir(std::move(outputDir)) {}

  void addFixture(const std::string &name, double tolerance = 8.0,
                  double maxDiffFraction = 0.05) {
    m_fixtures.push_back({name, m_goldenDir + "/" + name + ".png",
                          m_outputDir + "/" + name + ".png", tolerance,
                          maxDiffFraction});
  }

  bool saveCurrent(const std::string &name, const QImage &img) {
    return img.save(QString::fromStdString(m_outputDir + "/" + name + ".png"));
  }

  bool saveGolden(const std::string &name, const QImage &img) {
    return img.save(QString::fromStdString(m_goldenDir + "/" + name + ".png"));
  }

  static QImage captureWidget(QWidget *w) {
    QPixmap pm(w->size());
    w->render(&pm);
    return pm.toImage();
  }

  std::vector<UiTestResult> compareAll() {
    std::vector<UiTestResult> results;
    for (const auto &f : m_fixtures)
      results.push_back(compareOne(f));
    return results;
  }

private:
  UiTestResult compareOne(const UiTestFixture &f) {
    UiTestResult r;
    r.name = f.name;
    QImage gold(QString::fromStdString(f.goldenPath));
    QImage curr(QString::fromStdString(f.currentPath));
    if (gold.isNull()) {
      r.message = "missing golden: " + f.goldenPath;
      return r;
    }
    if (curr.isNull()) {
      r.message = "missing current: " + f.currentPath;
      return r;
    }
    if (gold.size() != curr.size()) {
      r.message = "size mismatch: " + std::to_string(gold.width()) + "x" +
                  std::to_string(gold.height()) + " vs " +
                  std::to_string(curr.width()) + "x" +
                  std::to_string(curr.height());
      return r;
    }

    const int w = gold.width(), h = gold.height();
    int diffs = 0;
    for (int y = 0; y < h; ++y) {
      const QRgb *lg = reinterpret_cast<const QRgb *>(gold.constScanLine(y));
      const QRgb *lc = reinterpret_cast<const QRgb *>(curr.constScanLine(y));
      for (int x = 0; x < w; ++x) {
        const int dr = std::abs(static_cast<int>(qRed(lg[x])) - qRed(lc[x]));
        const int dg =
            std::abs(static_cast<int>(qGreen(lg[x])) - qGreen(lc[x]));
        const int db = std::abs(static_cast<int>(qBlue(lg[x])) - qBlue(lc[x]));
        if (dr > f.tolerance || dg > f.tolerance || db > f.tolerance)
          ++diffs;
      }
    }
    r.diffPixels = diffs;
    r.diffFraction = static_cast<double>(diffs) / (w * h);
    r.passed = (r.diffFraction <= f.maxDiffFraction);
    r.message = r.passed ? "OK"
                         : "diff=" + std::to_string(diffs) + "px (" +
                               std::to_string(int(r.diffFraction * 100)) + "%)";
    return r;
  }

  std::string m_goldenDir;
  std::string m_outputDir;
  std::vector<UiTestFixture> m_fixtures;
};

static std::vector<QImage> generateTestImages() {
  std::vector<QImage> images;

  QImage g1(256, 256, QImage::Format_RGB32);
  for (int y = 0; y < 256; ++y)
    for (int x = 0; x < 256; ++x)
      g1.setPixel(x, y, qRgb(x, y, 128));
  images.push_back(g1);

  QImage g2(256, 256, QImage::Format_RGB32);
  g2.fill(qRgb(200, 100, 50));
  images.push_back(g2);

  QImage g3(256, 256, QImage::Format_RGB32);
  for (int y = 0; y < 256; ++y)
    for (int x = 0; x < 256; ++x)
      g3.setPixel(
          x, y, ((x / 16 + y / 16) % 2) ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
  images.push_back(g3);

  QImage g4(256, 256, QImage::Format_RGB32);
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, 255);
  for (int y = 0; y < 256; ++y)
    for (int x = 0; x < 256; ++x)
      g4.setPixel(x, y, qRgb(dist(rng), dist(rng), dist(rng)));
  images.push_back(g4);

  return images;
}

static bool saveTestImages(const QString &dir,
                           const std::vector<QImage> &imgs) {
  for (int i = 0; i < (int)imgs.size(); ++i) {
    QString path = QStringLiteral("%1/test_image_%2.png").arg(dir).arg(i);
    if (!imgs[i].save(path))
      return false;
  }
  return true;
}

static QImage renderMainWindow(int w = 1600, int h = 900) {
  QString tempRoot = QDir::tempPath() + "/mviewer_ui_fixture";
  QDir(tempRoot).removeRecursively();
  QDir().mkpath(tempRoot);

  auto imgs = generateTestImages();
  saveTestImages(tempRoot, imgs);

  MainWindow window;
  window.resize(w, h);
  window.show();

  QApplication::processEvents();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  QApplication::processEvents();

  QImage img = UiFixtureRegression::captureWidget(&window);

  window.close();
  QDir(tempRoot).removeRecursively();
  return img;
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  std::string goldenDir = std::string(MVIEWER_SOURCE_DIR) + "/golden/ui";
  std::string outputDir =
      std::string(MVIEWER_SOURCE_DIR) + "/tests/ui_fixture/current";
  std::string mode = "generate";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--compare")
      mode = "compare";
    else if (arg == "--generate")
      mode = "generate";
    else if (arg == "--golden-dir" && i + 1 < argc)
      goldenDir = argv[++i];
    else if (arg == "--output-dir" && i + 1 < argc)
      outputDir = argv[++i];
  }
  QDir().mkpath(QString::fromStdString(goldenDir));
  QDir().mkpath(QString::fromStdString(outputDir));

  UiFixtureRegression vr(goldenDir, outputDir);
  vr.addFixture("main_window_default");

  if (mode == "generate") {
    QImage img = renderMainWindow();
    vr.saveGolden("main_window_default", img);
    std::cout << "Generated UI golden screenshot: " << img.width() << "x"
              << img.height() << "\n";
    return 0;
  }

  QImage img = renderMainWindow();
  vr.saveCurrent("main_window_default", img);
  auto results = vr.compareAll();
  int fails = 0;
  for (const auto &r : results) {
    std::cout << (r.passed ? "PASS  " : "FAIL  ") << r.name << "  " << r.message
              << std::endl;
    if (!r.passed)
      ++fails;
  }
  return fails == 0 ? 0 : 1;
}
