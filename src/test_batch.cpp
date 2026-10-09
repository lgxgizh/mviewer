// BatchProcessor unit tests — verify batch decode → resize → export pipeline,
// progress callback, cancellation, and result collection.
#include "batchdialog.h"
#include "core/batch/BatchProcessor.h"
#include "core/batch/BatchRename.h"
#include "domain/BatchJob.h"

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QUuid>

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

// IHDR-only PNG (33 bytes). Decode fails; the guard reads the IHDR size claim.
static QByteArray pngClaim(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

static QString makeTempDir(const char *prefix)
{
    const QString path = QDir::tempPath() + QLatin1Char('/') + QLatin1String(prefix) +
                         QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(path);
    return path;
}

static QString writePngIn(const QString &dir, const char *name, QRgb color)
{
    const QString path = dir + QLatin1Char('/') + QLatin1String(name);
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

static QString writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(bytes);
    return path;
}

static QByteArray readAllBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

static bool hasMviewerTemp(const QString &dir)
{
    const QStringList names = QDir(dir).entryList(QStringList{QStringLiteral(".mviewer-tmp-*")},
                                                  QDir::Files | QDir::Hidden);
    return !names.isEmpty();
}

static mviewer::domain::BatchFileResult exportFile(const QString &input, const QString &outputDir,
                                                   const char *pattern, bool overwrite)
{
    mviewer::domain::BatchJobConfig config;
    config.inputPaths = {input.toStdString()};
    config.operations = {mviewer::domain::BatchOp::Export};
    if (pattern != nullptr && pattern[0] != '\0')
        config.renamePattern = pattern;
    config.exportFormat = "png";
    config.outputDir = outputDir.toStdString();
    config.overwriteExisting = overwrite;
    const auto result = mviewer::core::BatchProcessor().execute(config);
    if (result.fileResults.empty())
        return {};
    return result.fileResults.front();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

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

    // ── Test 2b: Per-file result streaming ─────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString(), p2.toStdString(), p3.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor processor;
        std::vector<std::string> streamedInputs;
        processor.setFileResultCallback([&streamedInputs](const mviewer::domain::BatchFileResult &r)
                                        { streamedInputs.push_back(r.inputPath); });

        auto result = processor.execute(config);

        // One callback per finished file, in submission order, matching the
        // aggregate the caller also receives.
        CHECK(streamedInputs.size() == result.fileResults.size(),
              "file-result callback fires once per file");
        CHECK(streamedInputs.size() == 3, "file-result callback fires for all 3 files");
        bool orderMatches = streamedInputs.size() == result.fileResults.size();
        for (size_t i = 0; orderMatches && i < streamedInputs.size(); ++i)
            orderMatches = streamedInputs[i] == result.fileResults[i].inputPath;
        CHECK(orderMatches, "streamed results arrive in the same order as the aggregate");
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
        CHECK(result.cancelled, "Cancellation is recorded on the batch result");
        CHECK(result.totalSucceeded + result.totalFailed + result.totalSkipped == 3,
              "Cancel accounting covers every input");
        CHECK(result.totalSkipped >= 1, "Unstarted files are reported as skipped");
    }

    // ── Test 7b: cancel before execute, and cancel before the first file ──
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString(), p2.toStdString(), p3.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor early;
        early.requestCancel();
        const auto earlyResult = early.execute(config);
        CHECK(earlyResult.cancelled, "Cancel before execute stays cancelled");
        CHECK(earlyResult.totalSucceeded == 0 && earlyResult.totalFailed == 0,
              "Cancel before execute processes nothing");
        CHECK(earlyResult.totalSkipped == 3 && earlyResult.fileResults.empty(),
              "Cancel before execute skips every file");

        mviewer::core::BatchProcessor atStart;
        atStart.setProgressCallback(
            [&atStart](int current, int, const std::string &)
            {
                if (current == 0)
                    atStart.requestCancel();
            });
        const auto startResult = atStart.execute(config);
        CHECK(startResult.cancelled && startResult.fileResults.empty(),
              "Cancel from the first progress callback skips that file");
        CHECK(startResult.totalSkipped == 3 && startResult.totalSucceeded == 0,
              "No file is reported successful when cancel wins before it starts");
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

    // ── Test 8: Crop + Export ──────────────────────────────────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Crop, mviewer::domain::BatchOp::Export};
        config.cropX = 8;
        config.cropY = 8;
        config.cropW = 16;
        config.cropH = 16;
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();
        QFile::remove(outDir + "/mviewer_batch_a.png"); // Test 1 already wrote this name

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);

        CHECK(result.totalSucceeded == 1, "Crop+export should succeed");
        CHECK(result.fileResults[0].width == 16, "Cropped width should be 16");
        CHECK(result.fileResults[0].height == 16, "Cropped height should be 16");
        QFile f(QString::fromStdString(result.fileResults[0].outputPath));
        CHECK(f.exists(), "Cropped output file should exist");
    }

    // ── Test 9: Directory input expands without recursiveScan ──────
    {
        const QString dirPath = QDir::tempPath() + "/mviewer_batch_indir";
        QDir().mkpath(dirPath);
        const QString nested = dirPath + "/nested";
        QDir().mkpath(nested);
        QImage img(32, 24, QImage::Format_RGB32);
        img.fill(qRgb(10, 20, 30));
        const QString topFile = dirPath + "/top.png";
        const QString nestedFile = nested + "/nested.png";
        img.save(topFile, "PNG");
        img.save(nestedFile, "PNG");

        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {dirPath.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();
        config.recursiveScan = false;

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);
        CHECK(result.totalSucceeded == 1, "Non-recursive dir input should pick top-level images");
        CHECK(result.fileResults.size() == 1,
              "Non-recursive dir input should not walk nested dirs");

        config.recursiveScan = true;
        QFile::remove(outDir + "/top.png"); // non-recursive pass already wrote it
        mviewer::core::BatchProcessor recursiveProcessor;
        auto recursiveResult = recursiveProcessor.execute(config);
        CHECK(recursiveResult.totalSucceeded == 2,
              "Recursive dir input should include nested images");

        QFile::remove(topFile);
        QFile::remove(nestedFile);
        QDir(nested).rmdir(nested);
        QDir(dirPath).rmdir(dirPath);
    }

    // ── Test 10: Pure find/replace transform logic ─────────────────
    {
        using mviewer::core::applyFindReplace;
        std::string err;

        // 1. Plain replace (single and multiple occurrences, case-sensitive & insensitive)
        CHECK(applyFindReplace("image_test_01", "test", "final", false, false) == "image_final_01",
              "Plain replace single occurrence");
        CHECK(applyFindReplace("foo_bar_foo", "foo", "baz", false, false) == "baz_bar_baz",
              "Plain replace multiple occurrences");
        CHECK(applyFindReplace("a[0].jpg", "[0]", "_zero", false, false) == "a_zero.jpg",
              "Plain replace with regex special characters taken literally");

        // Case sensitivity in plain replace
        CHECK(applyFindReplace("Photo_ABC", "abc", "xyz", false, false) == "Photo_xyz",
              "Plain replace case-insensitive match");
        CHECK(applyFindReplace("Photo_ABC", "abc", "xyz", false, true) == "Photo_ABC",
              "Plain replace case-sensitive no match");
        CHECK(applyFindReplace("Photo_ABC", "ABC", "xyz", false, true) == "Photo_xyz",
              "Plain replace case-sensitive match");

        // 2. Regex with capture groups
        CHECK(applyFindReplace("IMG_1234", "^IMG_", "PHOTO_", true, false) == "PHOTO_1234",
              "Regex basic anchor replace");
        CHECK(applyFindReplace("doc_2026_05", "doc_(\\d+)_(\\d+)", "archive_\\2_\\1", true,
                               false) == "archive_05_2026",
              "Regex capture groups \\1 and \\2");
        CHECK(applyFindReplace("x1_y2_z3", "([a-z])(\\d)", "\\1-\\2", true, false) == "x-1_y-2_z-3",
              "Regex global replacement with capture groups");

        // Case sensitivity in regex mode
        CHECK(applyFindReplace("Report_FINAL", "report_([a-z]+)", "summary_\\1", true, false) ==
                  "summary_FINAL",
              "Regex case-insensitive match");
        CHECK(applyFindReplace("Report_FINAL", "report_([a-z]+)", "summary_\\1", true, true) ==
                  "Report_FINAL",
              "Regex case-sensitive no match");

        // 3. Invalid regex: populates errorMessage, returns baseName, never crashes
        err.clear();
        std::string resInvalid =
            applyFindReplace("keep_name", "[unclosed_bracket", "rep", true, false, &err);
        CHECK(resInvalid == "keep_name", "Invalid regex returns baseName unchanged");
        CHECK(!err.empty(), "Invalid regex populates error string");

        // 4. Empty find: no-op, returns baseName, empty error
        err.clear();
        CHECK(applyFindReplace("original", "", "something", false, false, &err) == "original",
              "Empty find in plain mode is no-op");
        CHECK(err.empty(), "Empty find in plain mode produces no error");
        CHECK(applyFindReplace("original", "", "something", true, false, &err) == "original",
              "Empty find in regex mode is no-op");
        CHECK(err.empty(), "Empty find in regex mode produces no error");
    }

    // ── Test 11: Batch rename composition and validation ──────────
    {
        using mviewer::core::applyBatchRename;
        using mviewer::core::BatchRenameOptions;
        using mviewer::core::validateBatchRename;

        // Apply batch rename on file path: find/replace applied to stem, extension kept
        BatchRenameOptions opts;
        opts.find = "raw";
        opts.replace = "proc";
        auto res1 = applyBatchRename("C:/photos/raw_photo.png", opts, 0, 1);
        CHECK(res1.valid, "applyBatchRename valid");
        CHECK(res1.newName == "proc_photo.png",
              "applyBatchRename transforms stem and preserves extension");

        // Composed with pattern
        opts.pattern = "{name}_v{seq:2}";
        auto res2 = applyBatchRename("C:/photos/raw_photo.png", opts, 0, 5);
        CHECK(res2.valid, "applyBatchRename with pattern valid");
        CHECK(res2.newName == "proc_photo_v01.png", "applyBatchRename composed with pattern");

        // Invalid regex in options marks result invalid
        opts.useRegex = true;
        opts.find = "([0-9]+"; // invalid unclosed parenthesis
        auto res3 = applyBatchRename("C:/photos/raw_photo.png", opts, 0, 1);
        CHECK(!res3.valid, "applyBatchRename invalid regex is marked invalid");
        CHECK(res3.errorMessage.find("正则表达式无效：") != std::string::npos,
              "Error message indicates invalid regex");

        // Invalid Windows filename characters in replacement
        opts.useRegex = false;
        opts.find = "raw";
        opts.replace = "bad:char";
        opts.pattern.clear();
        auto res4 = applyBatchRename("C:/photos/raw_photo.png", opts, 0, 1);
        CHECK(!res4.valid, "applyBatchRename invalid character marked invalid");

        // validateBatchRename detecting duplicate target filenames
        BatchRenameOptions dupOpts;
        dupOpts.pattern = "same_name";
        std::string aggErr;
        auto dupResults = validateBatchRename({"C:/img1.jpg", "C:/img2.jpg"}, dupOpts, &aggErr);
        CHECK(dupResults.size() == 2, "validateBatchRename returns result for all inputs");
        CHECK(!dupResults[0].valid && !dupResults[1].valid, "Duplicate filenames marked invalid");
        CHECK(!aggErr.empty() && aggErr.find("重复") != std::string::npos,
              "Aggregate error detects duplicates");

        // Re-encoded output: the target extension replaces the source one
        BatchRenameOptions extOpts;
        extOpts.find = "raw";
        extOpts.replace = "proc";
        auto res5 = applyBatchRename("C:/photos/raw_photo.png", extOpts, 0, 1, "jpg");
        CHECK(res5.valid && res5.newName == "proc_photo.jpg",
              "applyBatchRename uses the target extension");

        // A replacement that empties the stem is rejected
        BatchRenameOptions emptyOpts;
        emptyOpts.find = ".*";
        emptyOpts.useRegex = true;
        auto res6 = applyBatchRename("C:/photos/raw_photo.png", emptyOpts, 0, 1);
        CHECK(!res6.valid, "replacement that empties the file name is invalid");

        // Regex error is reported even with no files
        BatchRenameOptions badRegex;
        badRegex.find = "(";
        badRegex.useRegex = true;
        CHECK(mviewer::core::findReplaceError(badRegex).find("正则表达式无效：") == 0,
              "findReplaceError reports invalid regex");
        badRegex.useRegex = false;
        CHECK(mviewer::core::findReplaceError(badRegex).empty(),
              "literal mode never reports a regex error");

        // Composition order: replace → pattern → extension
        BatchRenameOptions composeOpts;
        composeOpts.find = "IMG_(\\d+)";
        composeOpts.replace = "shot\\1";
        composeOpts.useRegex = true;
        composeOpts.pattern = "{name}_{n}";
        CHECK(mviewer::core::composeRenamedFileName("IMG_42", "png", composeOpts, 1, 3) ==
                  "shot42_2.png",
              "composeRenamedFileName applies replace, then pattern, then extension");
        CHECK(!mviewer::core::fileNameError("CON.png").empty(),
              "reserved device names are rejected");
        CHECK(mviewer::core::fileNameError("ok_name.png").empty(), "a normal name is accepted");
    }

    // ── Test 12: Settings persistence round-trip ───────────────────
    {
        // Isolated INI store: never touches the user's real settings.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::tempPath() + "/mviewer_batch_settings");
        QCoreApplication::setOrganizationName(QStringLiteral("mviewer-test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_batch"));
        QSettings settings;
        settings.remove(QStringLiteral("batchRename"));

        mviewer::core::BatchRenameOptions saveOpts;
        saveOpts.pattern = "{name}_renamed_{seq:3}";
        saveOpts.find = "old_prefix_";
        saveOpts.replace = "new_prefix_";
        saveOpts.useRegex = true;
        saveOpts.caseSensitive = true;

        mviewer::core::saveBatchRenameSettings(saveOpts);

        mviewer::core::BatchRenameOptions loadedOpts = mviewer::core::loadBatchRenameSettings();
        CHECK(loadedOpts.pattern == saveOpts.pattern, "Settings pattern matches");
        CHECK(loadedOpts.find == saveOpts.find, "Settings find matches");
        CHECK(loadedOpts.replace == saveOpts.replace, "Settings replace matches");
        CHECK(loadedOpts.useRegex == saveOpts.useRegex, "Settings useRegex matches");
        CHECK(loadedOpts.caseSensitive == saveOpts.caseSensitive, "Settings caseSensitive matches");

        // Separate groups (batch dialog vs export dialog) do not leak
        const auto other = mviewer::core::loadBatchRenameSettings("exportRename");
        CHECK(other.find.empty() && other.pattern.empty() && !other.useRegex,
              "an unused settings group loads defaults");
        settings.remove(QStringLiteral("exportRename"));

        settings.remove(QStringLiteral("batchRename"));
    }

    // ── Test 13: BatchProcessor execution with find/replace ─────────
    {
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {p1.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Rename, mviewer::domain::BatchOp::Export};
        config.renameFind = "batch_a";
        config.renameReplace = "replaced_out";
        config.exportFormat = "png";
        config.outputDir = outDir.toStdString();

        mviewer::core::BatchProcessor processor;
        auto result = processor.execute(config);

        CHECK(result.totalSucceeded == 1, "BatchProcessor with find/replace succeeds");
        CHECK(result.fileResults[0].outputPath.find("replaced_out") != std::string::npos,
              "BatchProcessor output path reflects find/replace transformation");
        QFile outFile(QString::fromStdString(result.fileResults[0].outputPath));
        CHECK(outFile.exists(), "Exported file exists on disk");
    }

    // ── Test 14: empty / whitespace output dir writes beside the source ──
    {
        const QString dir = makeTempDir("mviewer_batch_beside_");
        const QString input = writePngIn(dir, "src.png", qRgb(9, 8, 7));
        const auto beside = exportFile(input, QString(), "beside_{name}", false);
        const QString besideOut = QString::fromStdString(beside.outputPath);
        CHECK(beside.success, "empty outputDir export succeeds");
        CHECK(QFileInfo(besideOut).absolutePath() == QFileInfo(input).absolutePath(),
              "empty outputDir writes next to the input");
        CHECK(QFileInfo(besideOut).fileName() == QStringLiteral("beside_src.png"),
              "empty outputDir applies the rename pattern");
        CHECK(!hasMviewerTemp(dir), "empty outputDir leaves no .mviewer-tmp file");
        const auto spaced = exportFile(input, QStringLiteral("   "), "spaced_{name}", false);
        const QString spacedPath = QString::fromStdString(spaced.outputPath);
        const bool spacedBeside =
            QFileInfo(spacedPath).absolutePath() == QFileInfo(input).absolutePath();
        CHECK(spaced.success && spacedBeside, "whitespace outputDir writes next to the input");
        QDir(dir).removeRecursively();
    }

    // ── Test 15: a missing Chinese output directory is created ──
    {
        const QString dir = QDir::tempPath() + QStringLiteral("/mviewer_批处理_输出_") +
                            QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString srcDir = makeTempDir("mviewer_batch_cnsrc_");
        const QString input = writePngIn(srcDir, "cn.png", qRgb(4, 5, 6));
        const auto written = exportFile(input, QStringLiteral("  ") + dir + QStringLiteral("  "),
                                        "out_{name}", false);
        const QString out = QString::fromStdString(written.outputPath);
        CHECK(written.success && QDir(dir).exists(), "Chinese output directory is created");
        CHECK(QFileInfo(out).absolutePath() == QDir(dir).absolutePath(),
              "file is written inside the Chinese output directory");
        CHECK(QFileInfo::exists(out), "Chinese output directory contains the file");
        CHECK(!hasMviewerTemp(dir), "Chinese output directory has no .mviewer-tmp file");
        QDir(dir).removeRecursively();
        QDir(srcDir).removeRecursively();
    }

    // ── Test 16: never overwrite the source, even when overwrite is on ──
    {
        const QString dir = makeTempDir("mviewer_batch_same_");
        const QString input = writePngIn(dir, "same.png", qRgb(1, 2, 3));
        const QByteArray before = readAllBytes(input);
        const QString sourceDir = QFileInfo(input).absolutePath();
        const auto blocked = exportFile(input, sourceDir, "", false);
        CHECK(!blocked.success, "export onto the source is not a success");
        CHECK(blocked.errorMessage == "目标与源文件相同，已跳过",
              "export onto the source reports the conflict");
        CHECK(readAllBytes(input) == before, "source bytes stay unchanged");
        CHECK(!hasMviewerTemp(dir), "source conflict leaves no .mviewer-tmp file");
        const auto forced = exportFile(input, sourceDir, "", true);
        CHECK(!forced.success && forced.errorMessage == "目标与源文件相同，已跳过",
              "overwrite still refuses to replace the source");
        CHECK(readAllBytes(input) == before, "source bytes stay unchanged when overwrite is on");
        QDir(dir).removeRecursively();
    }

    // ── Test 17: existing destination is skipped unless overwrite is on ──
    {
        const QString srcDir = makeTempDir("mviewer_batch_owsrc_");
        const QString dstDir = makeTempDir("mviewer_batch_owdst_");
        const QString input = writePngIn(srcDir, "keep.png", qRgb(10, 20, 30));
        const QString existing = writePngIn(dstDir, "keep.png", qRgb(200, 210, 220));
        const QByteArray before = readAllBytes(existing);
        const auto skipped = exportFile(input, dstDir, "", false);
        CHECK(!skipped.success, "existing destination is skipped");
        CHECK(skipped.errorMessage == "目标文件已存在：keep.png",
              "existing destination names the file");
        CHECK(readAllBytes(existing) == before, "skipped destination bytes stay unchanged");
        CHECK(!hasMviewerTemp(dstDir), "skipped destination leaves no .mviewer-tmp file");
        const auto replaced = exportFile(input, dstDir, "", true);
        CHECK(replaced.success, "overwriteExisting replaces the destination");
        CHECK(readAllBytes(existing) != before, "overwriteExisting changes the destination bytes");
        CHECK(!hasMviewerTemp(dstDir), "overwrite leaves no .mviewer-tmp file");
        QDir(srcDir).removeRecursively();
        QDir(dstDir).removeRecursively();
    }

    // ── Test 18: a failed commit removes the .mviewer-tmp file ──
    {
        const QString srcDir = makeTempDir("mviewer_batch_badsrc_");
        const QString dstDir = makeTempDir("mviewer_batch_baddst_");
        const QString input = writePngIn(srcDir, "photo.png", qRgb(7, 8, 9));
        QDir().mkpath(dstDir + QStringLiteral("/photo.png"));
        const auto failed = exportFile(input, dstDir, "", true);
        CHECK(!failed.success && failed.errorMessage.find("导出失败：") == 0,
              "commit onto a directory reports 导出失败");
        CHECK(!hasMviewerTemp(dstDir), "failed commit leaves no .mviewer-tmp file");
        QDir(srcDir).removeRecursively();
        QDir(dstDir).removeRecursively();
    }

    // ── Test 19: huge and undecodable images get distinct messages ──
    {
        const QString dir = makeTempDir("mviewer_batch_huge_");
        const QString hugeOut = dir + QStringLiteral("/out");
        const char *wide = "89504e470d0a1a0a0000000d4948445200004e2000000008080200000093d95811";
        const char *heavy = "89504e470d0a1a0a0000000d4948445200002328000023280802000000e2b7e5ed";
        const auto tooWide =
            exportFile(writeBytes(dir + QStringLiteral("/huge.png"), pngClaim(wide)), hugeOut,
                       "x_{name}", false);
        CHECK(tooWide.errorMessage == "图片过大（20000x8），批处理暂不支持",
              "an image wider than 16384 reports 图片过大");
        const auto tooHeavy =
            exportFile(writeBytes(dir + QStringLiteral("/heavy.png"), pngClaim(heavy)), hugeOut,
                       "y_{name}", false);
        CHECK(tooHeavy.errorMessage == "图片过大（9000x9000），批处理暂不支持",
              "an image over 256MB reports 图片过大");
        const auto undecoded =
            exportFile(writeBytes(dir + QStringLiteral("/junk.png"), QByteArray("not a png")),
                       hugeOut, "z_{name}", false);
        CHECK(undecoded.errorMessage == "无法解码图片", "undecodable image reports 无法解码图片");
        QDir(dir).removeRecursively();
    }

    // ── Test 21: directory scan uses ImageFormats, not a private list ──
    {
        const QString dir = makeTempDir("mviewer_batch_scan_");
        const QString scanOut = makeTempDir("mviewer_batch_scanout_");
        writePngIn(dir, "ok.png", qRgb(3, 3, 3));
        writeBytes(dir + QStringLiteral("/notes.txt"), QByteArray("note"));
        writeBytes(dir + QStringLiteral("/also.cr3"), QByteArray("raw"));
        mviewer::domain::BatchJobConfig config;
        config.inputPaths = {dir.toStdString()};
        config.operations = {mviewer::domain::BatchOp::Export};
        config.exportFormat = "png";
        config.outputDir = scanOut.toStdString();
        config.renamePattern = "scan_{name}";
        const auto result = mviewer::core::BatchProcessor().execute(config);
        bool sawCr3 = false;
        bool sawTxt = false;
        for (const auto &file : result.fileResults)
        {
            sawCr3 = sawCr3 || file.inputPath.ends_with(".cr3");
            sawTxt = sawTxt || file.inputPath.ends_with(".txt");
        }
        CHECK(sawCr3, "directory scan includes cr3 from ImageFormats");
        CHECK(!sawTxt, "directory scan skips a txt file");
        QDir(dir).removeRecursively();
        QDir(scanOut).removeRecursively();
    }

    // ── Test 22: overwriteExisting defaults off; result lines stay explicit ──
    {
        mviewer::domain::BatchJobConfig config;
        CHECK(!config.overwriteExisting, "overwriteExisting defaults to false");
        mviewer::domain::BatchFileResult analyzed;
        analyzed.success = true;
        analyzed.inputPath = "C:/in.png";
        CHECK(BatchDialog::formatResultLine(analyzed) ==
                  QStringLiteral("[OK] C:/in.png（仅分析，未写出文件）"),
              "analysis-only success does not show an empty arrow");
        mviewer::domain::BatchFileResult exported;
        exported.success = true;
        exported.inputPath = "C:/in.png";
        exported.outputPath = "C:/out/in.png";
        CHECK(BatchDialog::formatResultLine(exported) ==
                  QStringLiteral("[OK] C:/in.png → C:/out/in.png"),
              "export success still shows input and output");
    }

    // ── Test 23: overwrite checkbox and the no-export guard ──
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir::tempPath() + "/mviewer_batch_settings");
        QCoreApplication::setOrganizationName(QStringLiteral("mviewer-test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_batch"));
        QSettings settings;
        settings.remove(QStringLiteral("batch/overwriteExisting"));
        BatchDialog fresh;
        auto *box = fresh.findChild<QCheckBox *>(QStringLiteral("batchOverwriteExisting"));
        CHECK(box != nullptr && !box->isChecked(), "overwrite checkbox starts unchecked");
        settings.setValue(QStringLiteral("batch/overwriteExisting"), true);
        BatchDialog loaded;
        auto *loadedBox = loaded.findChild<QCheckBox *>(QStringLiteral("batchOverwriteExisting"));
        CHECK(loadedBox != nullptr && loadedBox->isChecked(),
              "overwrite checkbox restores batch/overwriteExisting");
        auto *exportBox = loaded.findChild<QCheckBox *>(QStringLiteral("batchChkExport"));
        auto *resizeBox = loaded.findChild<QCheckBox *>(QStringLiteral("batchChkResize"));
        auto *start = loaded.findChild<QPushButton *>(QStringLiteral("batchStartButton"));
        auto *log = loaded.findChild<QTextEdit *>(QStringLiteral("batchLog"));
        loaded.setInputFiles({p1});
        CHECK(exportBox && resizeBox && start && log, "batch export guard controls exist");
        if (exportBox && resizeBox && start)
        {
            exportBox->setChecked(false);
            resizeBox->setChecked(true);
            QString seen;
            QTimer::singleShot(0,
                               [&seen]()
                               {
                                   auto *message = qobject_cast<QMessageBox *>(
                                       QApplication::activeModalWidget());
                                   if (!message)
                                       return;
                                   seen = message->text();
                                   message->reject();
                               });
            start->click();
            CHECK(start->isEnabled(), "resize without 导出 does not start the batch");
            CHECK(seen.contains(QStringLiteral("请勾选「导出」")),
                  "resize without 导出 warns to check 导出");
            CHECK(log && !log->toPlainText().contains(QStringLiteral("[OK]")),
                  "resize without 导出 writes no result line");
        }
        settings.remove(QStringLiteral("batch/overwriteExisting"));
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
