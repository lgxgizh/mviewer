//
// Copyright (c) 2026 mviewer project. All rights reserved.
// SPDX-License-Identifier: MIT
//
// P1: unit tests for the persistent star-rating store.
//
#include "core/RatingStore.h"

#include <cstdio>
#include <cstdlib>

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

    std::remove("test_ratings_a.txt");
    std::remove("test_ratings_b.txt");
    std::remove("test_ratings_tmp.txt");
    printf("\nratingstore_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
