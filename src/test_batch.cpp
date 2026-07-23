// BatchProcessor unit tests — verify batch decode → resize → export pipeline,
// progress callback, cancellation, and result collection.
#include "core/batch/BatchProcessor.h"
#include "domain/BatchJob.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;

static void CHECK(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    }
}

// Write a small solid-color PNG to a temp file.
static QString writeTempPng(const std::string &name, int w = 64, int h = 48,
                            QRgb color = qRgb(128, 64, 200))
{
    const QString path =
        QDir::tempPath() + "/mviewer_batch_" + QString::fromStdString(name) + ".png";
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ── Setup: create temp images ──────────────────────────────────
    const QString p1 = writeTempPng("a", 64, 48);
    const QString p2 = writeTempPng("b", 80, 60);
    const QString p3 = writeTempPng("c", 32, 32);

    const QString outDir = QDir::tempPath() + "/mviewer_batch_out";
    QDir().mkpath(outDir);

    // ── Test 1: Resize + Export (PNG→PNG) ──────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString(), p2.toStdString(), p3.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Resize, mviewer::domain::BatchOp::Export};
        config.resizeMaxEdge = 32;
        config.exportFormat = "png";
        config.exportQuality = 90;
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);

        CHECK(result.fileResults.size() == 3, "Should process all 3 files");
        CHECK(result.totalSucceeded == 3, "All 3 files should succeed");
        CHECK(result.totalFailed == 0, "No failures expected");

        // Verify output files exist and have correct dimensions.
        for (const auto &r : result.fileResults)
        {
            CHECK(r.success, "Each result should be successful");
            CHECK(!r.outputPath.empty(), "Output path should be set");
            CHECK(r.width <= 32 && r.height <= 32, "Resized image should fit within 32x32");
            QFile f(QString::fromStdString(r.outputPath));
            CHECK(f.exists(), "Output file should exist");
        }
    }

    // ── Test 2: Progress callback ──────────────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString(), p2.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor processor;

        std::vector<std::pair<int, int>> progressCalls;
        processor.setProgressCallback([&progressCalls](int current, int total, const std::string &)
                                      { progressCalls.emplace_back(current, total); });

        processor.execute(config);

        // Should get at least: (0,2), (1,2), (2,2) = 3 calls.
        CHECK(progressCalls.size() >= 3,
              "Progress callback should fire at least 3 times for 2 files");
        CHECK(progressCalls.front().first == 0, "First progress call should be (0, total)");
        CHECK(progressCalls.back().first == 2, "Last progress call should be (total, total)");
    }

    // ── Test 3: Rename pattern ─────────────────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString(), p2.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Rename, mviewer::domain::BatchOp::Export};
        config.renamePattern = "batch_{seq:3}";
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);

        CHECK(result.totalSucceeded == 2, "Rename+export should succeed");
        // First file should be batch_001.png
        CHECK(result.fileResults[0].outputPath.find("batch_001") != std::string::npos,
              "First file should be renamed to batch_001");
        CHECK(result.fileResults[1].outputPath.find("batch_002") != std::string::npos,
              "Second file should be renamed to batch_002");
    }

    // ── Test 5: Invalid file ───────────────────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {"/nonexistent/file.png"};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);

        CHECK(result.totalFailed == 1, "Invalid file should fail");
        CHECK(!result.fileResults[0].success, "Result should be failure");
        CHECK(!result.fileResults[0].errorMessage.empty(), "Error message should be set");
    }

    // ── Test 6: Empty operations ───────────────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString()};
        config.operations = {}; // no operations

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);

        CHECK(result.totalSucceeded == 1, "File with no operations should still 'succeed'");
    }

    // ── Test 7: Cancellation ──────────────────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString(), p2.toStdString(), p3.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor processor;

        int callCount = 0;
        processor.setProgressCallback(
            [&processor, &callCount](int current, int total, const std::string &)
            {
                if (current == 1)
                    processor.requestCancel();
                ++callCount;
            });

        auto result = processor.execute(config);

        // Should have processed at most 2 files (cancel after first).
        CHECK(result.fileResults.size() <= 2, "Cancellation should stop after at most 2 files");
        CHECK(processor.isCancelled(), "Cancelled flag should be true");
    }

    // ── Test 7: BatchJobConfig defaults ───────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        CHECK(config.resizeMaxEdge == 1920, "Default resize max edge should be 1920");
        CHECK(config.watermarkOpacity == 0.3, "Default watermark opacity should be 0.3");
        CHECK(config.watermarkFontSize == 24, "Default watermark font size should be 24");
        CHECK(config.exportQuality == 90, "Default export quality should be 90");
        CHECK(config.inputPaths.empty(), "Default input paths should be empty");
        CHECK(config.operations.empty(), "Default operations should be empty");
    }

    // ── Cleanup ────────────────────────────────────────────────────
    QFile::remove(p1);
    QFile::remove(p2);
    QFile::remove(p3);
    QDir(outDir).removeRecursively();

    std::fprintf(stderr, "%s: %d failures\n", g_fail == 0 ? "All tests passed" : "Tests failed",
                 g_fail);
    return g_fail == 0 ? 0 : 1;
}
