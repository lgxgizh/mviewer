// Vision regression test framework (tests/vision/).
// Captures rendered output from the program and compares against golden images.
// Works on QImage rendered to disk.
//
// Usage:
//   ./vision_regression.exe --generate    (save golden images from current
//   render)
//   ./vision_regression.exe --compare     (compare current render vs golden)
//   ./vision_regression.exe --golden-dir <path> --output-dir <path>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
#define MVIEWER_SOURCE_DIR "."
#endif

struct VisionTestCase
{
    std::string name;
    std::string goldenPath;
    std::string currentPath;
    double tolerance = 2.0;        // per-channel diff threshold (0-255)
    double maxDiffFraction = 0.01; // fraction of pixels allowed to differ
};

struct VisionResult
{
    std::string name;
    bool passed = false;
    int diffPixels = 0;
    double diffFraction = 0.0;
    std::string message;
};

class VisionRegression
{
public:
    explicit VisionRegression(std::string goldenDir, std::string outputDir)
        : m_goldenDir(std::move(goldenDir))
        , m_outputDir(std::move(outputDir))
    {
    }

    void addTestCase(const std::string& name, double tolerance = 2.0, double maxDiffFraction = 0.01)
    {
        m_tests.push_back({name,
            m_goldenDir + "/" + name + ".png",
            m_outputDir + "/" + name + ".png",
            tolerance,
            maxDiffFraction});
    }

    // Save a rendered frame as the current snapshot
    bool saveCurrent(const std::string& name, const QImage& img)
    {
        return img.save(QString::fromStdString(m_outputDir + "/" + name + ".png"));
    }

    // Save a rendered frame as golden (only during --generate)
    bool saveGolden(const std::string& name, const QImage& img)
    {
        return img.save(QString::fromStdString(m_goldenDir + "/" + name + ".png"));
    }

    // Run all tests
    std::vector<VisionResult> compareAll()
    {
        std::vector<VisionResult> results;
        for (const auto& tc : m_tests)
        {
            results.push_back(compareOne(tc));
        }
        return results;
    }

private:
    VisionResult compareOne(const VisionTestCase& tc)
    {
        VisionResult r;
        r.name = tc.name;
        QImage gold(QString::fromStdString(tc.goldenPath));
        QImage curr(QString::fromStdString(tc.currentPath));
        if (gold.isNull())
        {
            r.message = "missing golden: " + tc.goldenPath;
            return r;
        }
        if (curr.isNull())
        {
            r.message = "missing current: " + tc.currentPath;
            return r;
        }
        if (gold.size() != curr.size())
        {
            r.message = "size mismatch";
            return r;
        }

        const int w = gold.width(), h = gold.height();
        int diffs = 0;
        for (int y = 0; y < h; ++y)
        {
            const QRgb* lg = reinterpret_cast<const QRgb*>(gold.constScanLine(y));
            const QRgb* lc = reinterpret_cast<const QRgb*>(curr.constScanLine(y));
            for (int x = 0; x < w; ++x)
            {
                const int dr = std::abs(static_cast<int>(qRed(lg[x])) - qRed(lc[x]));
                const int dg = std::abs(static_cast<int>(qGreen(lg[x])) - qGreen(lc[x]));
                const int db = std::abs(static_cast<int>(qBlue(lg[x])) - qBlue(lc[x]));
                if (dr > tc.tolerance || dg > tc.tolerance || db > tc.tolerance)
                    ++diffs;
            }
        }
        r.diffPixels = diffs;
        r.diffFraction = static_cast<double>(diffs) / (w * h);
        r.passed = (r.diffFraction <= tc.maxDiffFraction);
        r.message = r.passed ? "OK"
                             : "diff=" + std::to_string(diffs) + "px (" +
                                   std::to_string(int(r.diffFraction * 100)) + "%)";
        return r;
    }

    std::string m_goldenDir;
    std::string m_outputDir;
    std::vector<VisionTestCase> m_tests;
};

// Render helper: creates test patterns as placeholder for actual UI render.
inline QImage renderTestPattern(const std::string& name, int w = 256, int h = 256)
{
    QImage img(w, h, QImage::Format_RGB32);
    if (name == "gradient")
    {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img.setPixel(x, y, qRgb(x * 255 / w, y * 255 / h, 128));
    }
    else if (name == "flat_gray")
    {
        img.fill(qRgb(128, 128, 128));
    }
    else if (name == "checker")
    {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img.setPixel(x, y, ((x / 16 + y / 16) % 2) ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    }
    else
    {
        img.fill(qRgb(100, 150, 200));
    }
    return img;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::string goldenDir = std::string(MVIEWER_SOURCE_DIR) + "/golden/vision";
    std::string outputDir = std::string(MVIEWER_SOURCE_DIR) + "/tests/vision/current";
    std::string mode = "generate";

    for (int i = 1; i < argc; ++i)
    {
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

    VisionRegression vr(goldenDir, outputDir);
    vr.addTestCase("gradient");
    vr.addTestCase("flat_gray");
    vr.addTestCase("checker");

    if (mode == "generate")
    {
        for (const auto& name : {"gradient", "flat_gray", "checker"})
        {
            QImage img = renderTestPattern(name);
            vr.saveGolden(name, img);
        }
        std::cout << "Generated vision golden images\n";
        return 0;
    }

    // Compare mode: render current + diff
    for (const auto& name : {"gradient", "flat_gray", "checker"})
    {
        QImage img = renderTestPattern(name);
        vr.saveCurrent(name, img);
    }
    auto results = vr.compareAll();
    int fails = 0;
    for (const auto& r : results)
    {
        std::cout << (r.passed ? "PASS  " : "FAIL  ") << r.name << "  " << r.message << std::endl;
        if (!r.passed)
            ++fails;
    }
    return fails == 0 ? 0 : 1;
}
