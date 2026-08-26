//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// P1: unit tests for the persistent star-rating store.
//
#include "core/RatingStore.h"
#include "core/SidecarStore.h"
#include "core/filesystem/Utf8Path.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <chrono>
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
    s.setFilePath("test_ratings_tmp.txt");

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
    CHECK(s.hasRating("a.jpg"), "hasRating true after set");

    s.setRating("a.jpg", 9); // clamps to 5
    CHECK(s.rating("a.jpg") == 5, "rating clamps to 5");

    s.setRating("a.jpg", -1); // clamps to 0 -> cleared
    CHECK(s.rating("a.jpg") == 0, "negative rating clamps to 0 (cleared)");
    CHECK(!s.hasRating("a.jpg"), "hasRating false after clear");

    // Persistence: save to file A, then reload from A after pointing elsewhere.
    s.setFilePath("test_ratings_a.txt");
    s.setRating("persist.png", 4);
    CHECK(s.save(), "save() returns true");

    // Simulate losing in-memory state by pointing at a different (empty) file,
    // then reloading from the original file that holds the persisted rating.
    s.setFilePath("test_ratings_b.txt");
    CHECK(!s.load(), "load() of a missing file returns false");
    CHECK(s.rating("persist.png") == 0, "rating absent from empty file B");
    s.setFilePath("test_ratings_a.txt");
    CHECK(s.load(), "load() returns true");
    CHECK(s.rating("persist.png") == 4, "persisted rating reloaded from disk");

    // M49 Windows contract: a user path is UTF-8 at the core boundary and is
    // converted to native filesystem paths only at the I/O edge. Include
    // spaces, CJK, and an emoji in both directory and filename.
    const QString unicodeDir = QDir(QDir::tempPath()).filePath(
        QStringLiteral("mviewer_路径 closure 😀/嵌套 目录"));
    QDir().mkpath(unicodeDir);
    const QString unicodeImage = QDir(unicodeDir).filePath(
        QStringLiteral("测试 image 😀.png"));
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
    CHECK(SidecarStore::instance().writeSidecar(unicodePath),
          "Unicode sidecar write succeeds");
    const QString sidecarPath = QString::fromUtf8(unicodeSidecar.data(),
                                                  static_cast<int>(unicodeSidecar.size()));
    CHECK(QFileInfo::exists(sidecarPath), "Unicode sidecar exists at the native path");
    s.clearRating(unicodePath);
    s.clearColorLabel(unicodePath);
    s.setPicked(unicodePath, false);
    CHECK(SidecarStore::instance().readSidecar(unicodePath),
          "Unicode sidecar read succeeds");
    CHECK(s.rating(unicodePath) == 5 && s.colorLabel(unicodePath) == 4 && s.picked(unicodePath),
          "Unicode sidecar restores RatingStore identity");
    CHECK(SidecarStore::instance().removeSidecar(unicodePath),
          "Unicode sidecar remove succeeds");
    CHECK(!QFileInfo::exists(sidecarPath), "Unicode sidecar removal reaches the native file");
    CHECK(!SidecarStore::instance().readSidecar(unicodePath + ".missing"),
          "missing Unicode sidecar is a handled failure");
    QDir(QDir(QDir::tempPath()).filePath(QStringLiteral("mviewer_路径 closure 😀")))
        .removeRecursively();

    std::remove("test_ratings_a.txt");
    std::remove("test_ratings_b.txt");
    std::remove("test_ratings_tmp.txt");
    std::filesystem::remove(recentRatings, recentEc);
    std::filesystem::remove(recentFlags, recentEc);
    printf("\nratingstore_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
