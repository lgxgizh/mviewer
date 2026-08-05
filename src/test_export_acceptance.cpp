// M24 Phase 4D — Export workflow acceptance tests.
//
// Maps the M24 Workflow D acceptance items that are automatable headless:
//   D#3  batch export can be cancelled (cooperative token), progress reported
//   D#4  no-permission / invalid output dirs and overlong paths degrade with
//        per-item failure counts instead of crashing
//   D#5  PNG/JPEG/TIFF outputs match the requested format and reopen correctly
//   D#6  exported images reopen with correct dimensions (orientation/geometry)
//   D#8  a failed encode leaves NO partial file at the final name (temp +
//        atomic rename); an existing file is replaced atomically
//
// (ExportManager / PDF / CSV / HTML / report generation are covered by
// export_tests / export_pipeline_tests / export_job_tests.)

#include "core/export/ExportJob.h"

#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

QString writeSeedPng(const QString &dir, const QString &name, int w, int h)
{
    const QString path = dir + "/" + name;
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, QColor((x * 3) & 0xFF, (y * 5) & 0xFF, 128).rgb());
    img.save(path, "PNG");
    return path;
}

void testD5D6Formats(QTemporaryDir &tmp)
{
    std::cout << "── Export D#5/D#6: format fidelity + reopen ──\n";
    QDir out(tmp.filePath("out"));
    out.mkpath(".");
    QDir in(tmp.filePath("in"));
    in.mkpath(".");
    const QString seed = writeSeedPng(in.absolutePath(), "src.png", 96, 64);

    const QList<QString> formats = {"png", "jpeg", "tiff"};
    for (const QString &fmt : formats)
    {
        mviewer::exportjob::ExportJobConfig cfg;
        cfg.outDir = out.absolutePath().toStdString();
        cfg.format = fmt.toStdString();
        cfg.renamePattern = "{name}";
        cfg.sources.push_back(seed.toStdString());
        const auto r = mviewer::exportjob::run(cfg);
        CHECK(r.done == 1 && r.failed == 0, ("D#5: " + fmt + " export succeeds").toUtf8().constData());

        const QString ext = (fmt == "jpeg") ? "jpg" : fmt;
        const QString outPath = out.filePath("src." + ext);
        const QImage reopened(outPath);
        CHECK(!reopened.isNull(), ("D#5: " + fmt + " output reopens").toUtf8().constData());
        if (!reopened.isNull())
        {
            CHECK(reopened.width() == 96 && reopened.height() == 64,
                  "D#6: reopened image keeps source dimensions");
            const QColor c = reopened.pixelColor(10, 10);
            // JPEG is lossy: allow a small per-channel tolerance; PNG/TIFF are
            // lossless and must match exactly.
            const int tol = (fmt == "jpeg") ? 24 : 0;
            CHECK(qAbs(c.red() - 30) <= tol && qAbs(c.green() - 50) <= tol,
                  ("D#6: reopened pixels match the source content (" + fmt + ")")
                      .toUtf8()
                      .constData());
        }
        // No temp files may linger after a successful run.
        bool tmpLeft = false;
        for (const auto &e : fs::directory_iterator(out.absolutePath().toStdString()))
            if (e.path().string().find(".mviewer-tmp") != std::string::npos)
                tmpLeft = true;
        CHECK(!tmpLeft, ("D#8: no temp file left after " + fmt + " success").toUtf8().constData());
    }
}

void testD8AtomicReplace(QTemporaryDir &tmp)
{
    std::cout << "── Export D#8: atomic replace + no partial files ──\n";
    QDir out(tmp.filePath("out8"));
    out.mkpath(".");
    QDir in8(tmp.filePath("in8"));
    in8.mkpath(".");
    const QString seed = writeSeedPng(in8.absolutePath(), "a.png", 32, 32);

    // Pre-existing destination with distinct content: it must be replaced
    // fully (never left half-written).
    const QString dst = out.filePath("a.png");
    QImage oldImg(16, 16, QImage::Format_RGB32);
    oldImg.fill(QColor(255, 0, 0));
    oldImg.save(dst);

    mviewer::exportjob::ExportJobConfig cfg;
    cfg.outDir = out.absolutePath().toStdString();
    cfg.format = "png";
    cfg.renamePattern = "{name}";
    cfg.sources.push_back(seed.toStdString());
    const auto r = mviewer::exportjob::run(cfg);
    CHECK(r.done == 1, "D#8: overwrite run completes");
    const QImage replaced(dst);
    CHECK(!replaced.isNull() && replaced.width() == 32,
          "D#8: destination fully replaced (old 16px file gone, new 32px complete)");

    // Failed encode (unsupported format id): must NOT create the final file.
    mviewer::exportjob::ExportJobConfig bad;
    bad.outDir = out.absolutePath().toStdString();
    bad.format = "not_a_format";
    bad.renamePattern = "{name}";
    bad.sources.push_back(seed.toStdString());
    const auto rb = mviewer::exportjob::run(bad);
    CHECK(rb.failed == 1 && rb.done == 0, "D#8: unsupported format reported as failed");
    CHECK(!fs::exists(out.filePath("a.not_a_format").toStdString()),
          "D#8: failed encode leaves no file at the final name");
    bool tmpLeft = false;
    for (const auto &e : fs::directory_iterator(out.absolutePath().toStdString()))
        if (e.path().string().find(".mviewer-tmp") != std::string::npos)
            tmpLeft = true;
    CHECK(!tmpLeft, "D#8: failed encode cleans up its temp file");
}

void testD3Cancel(QTemporaryDir &tmp)
{
    std::cout << "── Export D#3: cancellation ──\n";
    QDir out(tmp.filePath("out3"));
    out.mkpath(".");
    QDir in(tmp.filePath("in3"));
    in.mkpath(".");

    std::vector<std::string> sources;
    for (int i = 0; i < 20; ++i)
        sources.push_back(writeSeedPng(in.absolutePath(), QString("s%1.png").arg(i), 24, 24)
                              .toStdString());

    mviewer::exportjob::ExportJobConfig cfg;
    cfg.outDir = out.absolutePath().toStdString();
    cfg.format = "png";
    cfg.renamePattern = "{name}";
    cfg.sources = sources;
    const auto cancel = std::make_shared<std::atomic<bool>>(false);
    cfg.cancel = cancel;

    int progressCalls = 0;
    auto r = mviewer::exportjob::run(
        cfg, [&](int, int, const std::string &) { ++progressCalls; });
    CHECK(r.done == 20, "D#3: uncancelled batch completes all items");
    CHECK(progressCalls >= 20, "D#3: progress reported per item");

    cancel->store(true, std::memory_order_relaxed);
    r = mviewer::exportjob::run(cfg);
    CHECK(r.done == 0 && r.total == 20,
          "D#3: pre-cancelled run stops before processing any item");
    CHECK(r.message.find("cancelled") != std::string::npos,
          "D#3: cancellation reported in the result message");
}

void testD4DegradedDirs(QTemporaryDir &tmp)
{
    std::cout << "── Export D#4: degraded output dirs ──\n";
    QDir in(tmp.filePath("in4"));
    in.mkpath(".");
    const QString seed = writeSeedPng(in.absolutePath(), "a.png", 24, 24);

    // Invalid output directory (illegal path chars): every item fails cleanly.
    mviewer::exportjob::ExportJobConfig cfg;
    cfg.outDir = tmp.filePath("bad dir \u0001<>:\"").toStdString();
    cfg.format = "png";
    cfg.renamePattern = "{name}";
    cfg.sources = {seed.toStdString(), seed.toStdString()};
    const auto r = mviewer::exportjob::run(cfg);
    CHECK(r.failed == 2 && r.done == 0,
          "D#4: unwritable output dir yields per-item failures, no crash");

    // Overlong destination path: fails per-item, no crash.
    std::string longName(240, 'n');
    mviewer::exportjob::ExportJobConfig cfg2;
    cfg2.outDir = in.absolutePath().toStdString(); // writable
    cfg2.format = "png";
    cfg2.renamePattern = longName; // absurdly long base name
    cfg2.sources = {seed.toStdString()};
    const auto r2 = mviewer::exportjob::run(cfg2);
    CHECK(r2.failed == 1 || r2.done == 1,
          "D#4: overlong path handled (failed or written), no crash");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;

    testD5D6Formats(tmp);
    testD8AtomicReplace(tmp);
    testD3Cancel(tmp);
    testD4DegradedDirs(tmp);

    if (g_failures > 0)
    {
        std::cout << "export_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "export_acceptance_tests: PASS\n";
    return 0;
}
