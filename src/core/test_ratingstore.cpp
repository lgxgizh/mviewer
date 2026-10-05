//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// P1: unit tests for the persistent star-rating store.
//
#include "core/RatingStore.h"
#include "core/SidecarStore.h"
#include "core/TagStore.h"
#include "core/filesystem/Utf8Path.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

using namespace mviewer::core;

int main()
{
    auto &s = RatingStore::instance();

    // Keep every ratings/flags pair under unique temp dirs so parallel ctest
    // (-jN) cannot clobber a shared ./flags.txt next to relative paths.
    QTemporaryDir suiteTmp;
    if (!suiteTmp.isValid())
    {
        printf("FAIL: suite temp dir\n");
        return 1;
    }
    const std::string suiteDir = suiteTmp.path().toUtf8().toStdString();
    s.setFilePath(suiteDir + "/test_ratings_tmp.txt");

    // Regression: recents are worker-debounced and must persist even when no
    // later setFilePath()/destructor flush is available to hide a broken timer.
    const std::filesystem::path recentDir =
        std::filesystem::temp_directory_path() / "mviewer_ratingstore_recent_test";
    std::error_code recentEc;
    std::filesystem::create_directories(recentDir, recentEc);
    const auto recentRatings = recentDir / "ratings.txt";
    const auto recentFlags = recentDir / "flags.txt";
    std::filesystem::remove(recentRatings, recentEc);
    std::filesystem::remove(recentFlags, recentEc);
    s.setFilePath(recentRatings.string());
    s.addRecent("debounced.png");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    bool recentWritten = false;
    {
        std::ifstream flags(recentFlags);
        std::string line;
        while (std::getline(flags, line))
            recentWritten = recentWritten || line == "N|debounced.png";
    }
    CHECK(recentWritten, "recent is written after the debounce quiet period");
    s.setFilePath(recentRatings.string());
    CHECK(!s.recents().empty() && s.recents().front() == "debounced.png",
          "debounced recent reloads from disk");

    s.setRating("a.jpg", 3);
    CHECK(s.rating("a.jpg") == 3, "rating set to 3");
    s.setRating("C:\\photos\\rated.png", 4);
    s.setColorLabel("C:\\photos\\rated.png", 2);
    s.setRejected("C:\\photos\\rated.png", true);
    s.setPicked("C:/photos/picked.png", true);
    s.addRecent("C:\\photos\\recent.png");
    {
        const RatingStore::Snapshot snap = s.snapshot();
        CHECK(snap.rating("C:/photos/rated.png") == 4, "snapshot rating ignores slash style");
        CHECK(snap.colorLabel("C:/photos/rated.png") == 2,
              "snapshot color label ignores slash style");
        CHECK(snap.isRejected("C:/photos/rated.png"), "snapshot reject ignores slash style");
        CHECK(snap.isPicked("C:\\photos\\picked.png"), "snapshot pick ignores slash style");
        CHECK(snap.isRecent("C:/photos/recent.png"), "snapshot recent ignores slash style");
    }
    s.clearRating("C:\\photos\\rated.png");
    s.clearColorLabel("C:/photos/rated.png");
    s.setRejected("C:/photos/rated.png", false);
    s.setPicked("C:\\photos\\picked.png", false);
    CHECK(s.hasRating("a.jpg"), "hasRating true after set");

    s.setRating("a.jpg", 9); // clamps to 5
    CHECK(s.rating("a.jpg") == 5, "rating clamps to 5");

    s.setRating("a.jpg", -1); // clamps to 0 -> cleared
    CHECK(s.rating("a.jpg") == 0, "negative rating clamps to 0 (cleared)");
    CHECK(!s.hasRating("a.jpg"), "hasRating false after clear");

    // Persistence: save to file A, then reload from A after pointing elsewhere.
    s.setFilePath(suiteDir + "/test_ratings_a.txt");
    s.setRating("persist.png", 4);
    CHECK(s.save(), "save() returns true");

    // Simulate losing in-memory state by pointing at a different (empty) file,
    // then reloading from the original file that holds the persisted rating.
    s.setFilePath(suiteDir + "/test_ratings_b.txt");
    CHECK(!s.load(), "load() of a missing file returns false");
    CHECK(s.rating("persist.png") == 0, "rating absent from empty file B");
    s.setFilePath(suiteDir + "/test_ratings_a.txt");
    CHECK(s.load(), "load() returns true");
    CHECK(s.rating("persist.png") == 4, "persisted rating reloaded from disk");

    // M49 Windows contract: a user path is UTF-8 at the core boundary and is
    // converted to native filesystem paths only at the I/O edge. Include
    // spaces, CJK, and an emoji in both directory and filename.
    const QString unicodeDir =
        QDir(QDir::tempPath()).filePath(QStringLiteral("mviewer_路径 closure 😀/嵌套 目录"));
    QDir().mkpath(unicodeDir);
    const QString unicodeImage = QDir(unicodeDir).filePath(QStringLiteral("测试 image 😀.png"));
    QImage unicodeFixture(8, 8, QImage::Format_RGB32);
    unicodeFixture.fill(Qt::blue);
    CHECK(unicodeFixture.save(unicodeImage, "PNG"), "Unicode fixture is written");
    const std::string unicodePath = unicodeImage.toUtf8().toStdString();
    const std::string unicodeRatings =
        QDir(unicodeDir).filePath(QStringLiteral("评分 状态.txt")).toUtf8().toStdString();
    const std::string roundTrip = pathToUtf8(pathFromUtf8(unicodePath));
    CHECK(roundTrip == unicodePath, "UTF-8 path round-trips without locale loss");
    s.setFilePath(unicodeRatings);
    s.setRating(unicodePath, 5);
    s.setColorLabel(unicodePath, 4);
    s.setPicked(unicodePath, true);
    const std::string unicodeSidecar = SidecarStore::sidecarPath(unicodePath);
    CHECK(unicodeSidecar.find("测试 image") != std::string::npos,
          "sidecar identity keeps the Unicode filename");
    CHECK(SidecarStore::instance().writeSidecar(unicodePath), "Unicode sidecar write succeeds");
    const QString sidecarPath =
        QString::fromUtf8(unicodeSidecar.data(), static_cast<int>(unicodeSidecar.size()));
    CHECK(QFileInfo::exists(sidecarPath), "Unicode sidecar exists at the native path");
    s.clearRating(unicodePath);
    s.clearColorLabel(unicodePath);
    s.setPicked(unicodePath, false);
    CHECK(SidecarStore::instance().readSidecar(unicodePath), "Unicode sidecar read succeeds");
    CHECK(s.rating(unicodePath) == 5 && s.colorLabel(unicodePath) == 4 && s.picked(unicodePath),
          "Unicode sidecar restores RatingStore identity");
    CHECK(SidecarStore::instance().removeSidecar(unicodePath), "Unicode sidecar remove succeeds");
    CHECK(!QFileInfo::exists(sidecarPath), "Unicode sidecar removal reaches the native file");
    CHECK(!SidecarStore::instance().readSidecar(unicodePath + ".missing"),
          "missing Unicode sidecar is a handled failure");
    QDir(QDir(QDir::tempPath()).filePath(QStringLiteral("mviewer_路径 closure 😀")))
        .removeRecursively();

    std::filesystem::remove(recentRatings, recentEc);
    std::filesystem::remove(recentFlags, recentEc);

    // Prune missing entries for RatingStore and TagStore
    {
        QTemporaryDir pruneTmp;
        const QString existFile = pruneTmp.filePath("real_image.png");
        QImage dummy(4, 4, QImage::Format_RGB32);
        dummy.fill(Qt::red);
        CHECK(dummy.save(existFile, "PNG"), "Created real image for prune test");
        const std::string existPath = existFile.toUtf8().toStdString();
        const std::string missingPath =
            pruneTmp.filePath("no_such_file.png").toUtf8().toStdString();

        s.setRating(existPath, 4);
        s.setColorLabel(existPath, 2);
        s.setPicked(existPath, true);
        s.setRating(missingPath, 3);
        s.setColorLabel(missingPath, 1);
        s.setPicked(missingPath, true);

        CHECK(s.hasRating(missingPath), "missingPath has rating before prune");
        CHECK(s.hasColorLabel(missingPath), "missingPath has label before prune");
        CHECK(s.picked(missingPath), "missingPath is picked before prune");

        size_t pruned = s.pruneMissing();
        CHECK(pruned >= 3, "pruneMissing removed missingPath entries");
        CHECK(!s.hasRating(missingPath), "missingPath rating pruned");
        CHECK(!s.hasColorLabel(missingPath), "missingPath label pruned");
        CHECK(!s.picked(missingPath), "missingPath picked flag pruned");
        CHECK(s.rating(existPath) == 4, "existPath rating preserved");
        CHECK(s.colorLabel(existPath) == 2, "existPath label preserved");
        CHECK(s.picked(existPath), "existPath picked preserved");

        // TagStore pruning
        auto &ts = TagStore::instance();
        ts.setFilePath(pruneTmp.path().toUtf8().toStdString() + "/test_tags.txt");
        ts.addTag(existPath, "landscape");
        ts.addTag(missingPath, "ghost");
        CHECK(ts.hasTag(missingPath, "ghost"), "TagStore has tag for missingPath before prune");
        size_t tagsPruned = ts.pruneMissing();
        CHECK(tagsPruned >= 1, "TagStore pruneMissing pruned ghost tag");
        CHECK(!ts.hasTag(missingPath, "ghost"), "TagStore ghost tag gone");
        CHECK(ts.hasTag(existPath, "landscape"), "TagStore landscape tag preserved");
    }

    printf("\nratingstore_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
