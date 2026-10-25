// RAW display policy, cache-key suffix, and chooseRawFull branches.

#include "core/image/decoder/RawDecodePlan.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool condition, const char *message)
{
    if (condition)
    {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

using mviewer::core::chooseRawFull;
using mviewer::core::RawDisplayPolicy;
using mviewer::core::RawFullIntent;
using mviewer::domain::RawSource;

static void expect(RawFullIntent got, RawFullIntent want, const char *message)
{
    check(got == want, message);
}

static void testPreferPreview()
{
    const auto policy = RawDisplayPolicy::PreferPreview;
    expect(chooseRawFull(1200, 8000, true, 8'000'000, policy), RawFullIntent::Preview,
           "prefer: any preview wins, even when it is far below the sensor");
    expect(chooseRawFull(1, 0, true, 8'000'000, policy), RawFullIntent::Preview,
           "prefer: a one-pixel preview still wins");
    expect(chooseRawFull(0, 4000, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "prefer: missing preview demosaics");
    expect(chooseRawFull(0, 0, true, 256, policy), RawFullIntent::Empty,
           "prefer: tiny file is not demosaiced");
    expect(chooseRawFull(0, 0, false, 8'000'000, policy), RawFullIntent::Empty,
           "prefer: no LibRaw and no preview is empty");
    expect(chooseRawFull(0, 0, true, 4096, policy), RawFullIntent::FullDemosaic,
           "prefer: 4096 bytes is large enough to demosaic");
    expect(chooseRawFull(0, 0, true, 4095, policy), RawFullIntent::Empty,
           "prefer: 4095 bytes is still a fixture");
}

static void testAlwaysFull()
{
    const auto policy = RawDisplayPolicy::AlwaysFull;
    expect(chooseRawFull(6000, 6000, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "always: a full-size preview still demosaics");
    expect(chooseRawFull(100, 0, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "always: unknown sensor still demosaics");
    expect(chooseRawFull(100, 6000, false, 8'000'000, policy), RawFullIntent::Preview,
           "always: without LibRaw the preview remains");
    expect(chooseRawFull(0, 6000, false, 8'000'000, policy), RawFullIntent::Empty,
           "always: no LibRaw and no preview is empty");
    expect(chooseRawFull(1600, 6000, true, 256, policy), RawFullIntent::Preview,
           "always: a tiny file keeps the preview");
    expect(chooseRawFull(0, 0, true, 100, policy), RawFullIntent::Empty,
           "always: a tiny file with no preview is empty");
}

static void testAutoSmallPreview()
{
    const auto policy = RawDisplayPolicy::AutoFullWhenPreviewSmall;
    expect(chooseRawFull(1000, 2000, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "auto: 1000 < 0.9 * 2000 demosaics");
    expect(chooseRawFull(1900, 2000, true, 8'000'000, policy), RawFullIntent::Preview,
           "auto: a near-full preview stays a preview");
    expect(chooseRawFull(1800, 2000, true, 8'000'000, policy), RawFullIntent::Preview,
           "auto: exactly 0.9 * sensor stays a preview");
    expect(chooseRawFull(1799, 2000, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "auto: one pixel under 0.9 * sensor demosaics");
    expect(chooseRawFull(0, 2000, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "auto: a missing preview is small");
    expect(chooseRawFull(1000, 0, true, 8'000'000, policy), RawFullIntent::Preview,
           "auto: unknown sensor with a preview matches prefer");
    expect(chooseRawFull(0, 0, true, 8'000'000, policy), RawFullIntent::FullDemosaic,
           "auto: unknown sensor and no preview demosaics");
    expect(chooseRawFull(100, 2000, false, 8'000'000, policy), RawFullIntent::Preview,
           "auto: a small preview without LibRaw stays a preview");
    expect(chooseRawFull(100, 2000, true, 256, policy), RawFullIntent::Preview,
           "auto: a small preview in a tiny file stays a preview");
    expect(chooseRawFull(0, 2000, false, 8'000'000, policy), RawFullIntent::Empty,
           "auto: no preview and no LibRaw is empty");
}

static void testPolicyStoreAndCacheKey()
{
    using mviewer::core::clearRawFullDecode;
    using mviewer::core::rawCacheKeyAgrees;
    using mviewer::core::rawCacheKeySuffix;
    using mviewer::core::rawDisplayPolicy;
    using mviewer::core::rawFullDecodeRequested;
    using mviewer::core::recalledRawSource;
    using mviewer::core::rememberRawSource;
    using mviewer::core::requestRawFullDecode;
    using mviewer::core::setRawDisplayPolicy;

    setRawDisplayPolicy(RawDisplayPolicy::PreferPreview);
    check(rawDisplayPolicy() == RawDisplayPolicy::PreferPreview, "default policy is prefer");
    check(rawCacheKeySuffix("photo.jpg").empty(), "jpeg keys never grow a RAW suffix");
    check(rawCacheKeySuffix("photo.CR2").empty(), "default RAW keys stay unsuffixed");
    check(rawCacheKeyAgrees("rev|size", ""), "an unsuffixed key agrees with the default");
    check(!rawCacheKeyAgrees("rev|rawfull=1", ""), "a full-decode key does not agree with default");

    setRawDisplayPolicy(RawDisplayPolicy::AlwaysFull);
    check(rawDisplayPolicy() == RawDisplayPolicy::AlwaysFull, "policy store accepts always");
    const std::string always = rawCacheKeySuffix("shot.nef");
    check(always == "|rawpol=2", "always-full suffixes only the policy");
    check(rawCacheKeySuffix("shot.jpg").empty(), "always-full does not suffix jpeg");
    check(rawCacheKeyAgrees("base" + always, always), "policy suffix agrees");
    check(!rawCacheKeyAgrees("base", always), "a bare key does not agree with a policy suffix");

    setRawDisplayPolicy(RawDisplayPolicy::AutoFullWhenPreviewSmall);
    check(rawCacheKeySuffix("shot.dng") == "|rawpol=1", "auto suffixes policy 1");

    setRawDisplayPolicy(RawDisplayPolicy::PreferPreview);
    requestRawFullDecode("explicit.nrw");
    check(rawFullDecodeRequested("explicit.nrw"), "full-decode override is recorded");
    check(rawCacheKeySuffix("explicit.nrw") == "|rawfull=1", "override suffixes rawfull");
    check(rawCacheKeySuffix("other.nrw").empty(), "override is per path");
    requestRawFullDecode("not-raw.jpg");
    check(!rawFullDecodeRequested("not-raw.jpg"), "non-RAW paths are not overridden");

    setRawDisplayPolicy(RawDisplayPolicy::AlwaysFull);
    const std::string both = rawCacheKeySuffix("explicit.nrw");
    check(both == "|rawpol=2|rawfull=1", "policy and override combine");
    check(rawCacheKeyAgrees("id" + both, both), "combined suffix agrees");
    check(!rawCacheKeyAgrees("id|rawpol=2", both), "a partial suffix does not agree");

    clearRawFullDecode("explicit.nrw");
    check(!rawFullDecodeRequested("explicit.nrw"), "clear drops the override");
    check(rawCacheKeySuffix("explicit.nrw") == "|rawpol=2", "policy suffix remains after clear");

    setRawDisplayPolicy(static_cast<RawDisplayPolicy>(9));
    check(rawDisplayPolicy() == RawDisplayPolicy::PreferPreview, "unknown policy values reset");
    check(rawCacheKeySuffix("explicit.nrw").empty(), "cleared override leaves the default key");

    rememberRawSource("key-a", RawSource::FullDemosaic);
    check(recalledRawSource("key-a") == RawSource::FullDemosaic, "raw source is recalled by key");
    check(recalledRawSource("missing") == RawSource::NotRaw, "unknown keys recall NotRaw");
    rememberRawSource("key-a", RawSource::NotRaw);
    check(recalledRawSource("key-a") == RawSource::FullDemosaic, "NotRaw does not erase a memory");
}

int main()
{
    testPreferPreview();
    testAlwaysFull();
    testAutoSmallPreview();
    testPolicyStoreAndCacheKey();
    std::printf("test_raw_display: %s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
