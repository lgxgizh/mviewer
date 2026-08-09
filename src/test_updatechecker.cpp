#include "core/update/UpdateChecker.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
int g_failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
    {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

struct CheckResult
{
    mviewer::core::UpdateInfo info;
    std::string requestedUrl;
    int callbackCount = 0;
};

CheckResult checkUrl(const std::string &currentVersion, const std::string &endpoint,
                     std::string response, std::string fetchError = {})
{
    CheckResult result;
    mviewer::core::UpdateChecker checker(
        currentVersion,
        [&result, response = std::move(response), fetchError = std::move(fetchError)](
            const std::string &url, std::string &error)
        {
            result.requestedUrl = url;
            error = fetchError;
            return response;
        });
    checker.checkUrl(endpoint,
                     [&result](const mviewer::core::UpdateInfo &info)
                     {
                         ++result.callbackCount;
                         result.info = info;
                     });
    check(result.callbackCount == 1, "checkUrl invokes its callback exactly once");
    return result;
}

void testGitHubUrlAndJson()
{
    CheckResult result;
    mviewer::core::UpdateChecker checker(
        "7.2.2",
        [&result](const std::string &url, std::string &)
        {
            result.requestedUrl = url;
            return std::string(
                R"({ "name": "Release", "html_url": )"
                R"("https:\/\/github.com\/acme\/viewer\/releases\/tag\/)"
                R"(v7.2.3?from=app\u0026channel=stable", "tag_name" : "v7.2.3" })");
        });
    checker.checkGitHub("acme/viewer",
                        [&result](const mviewer::core::UpdateInfo &info)
                        {
                            ++result.callbackCount;
                            result.info = info;
                        });

    check(result.callbackCount == 1, "checkGitHub invokes its callback exactly once");
    check(result.requestedUrl ==
              "https://api.github.com/repos/acme/viewer/releases/latest",
          "checkGitHub constructs the canonical latest-release API URL");
    check(result.info.error.empty() && result.info.latestVersion == "v7.2.3",
          "GitHub JSON parses tag_name with whitespace and reordered fields");
    check(result.info.hasUpdate, "a newer GitHub tag is reported as an update");
    check(result.info.releaseUrl ==
              "https://github.com/acme/viewer/releases/tag/v7.2.3?from=app&channel=stable",
          "html_url is preferred and normal JSON escapes are decoded");
}

void testVersionOrdering()
{
    const auto newer = checkUrl("7.2.3", "https://updates.example.test/latest",
                                R"({"tag_name":"v7.3.0"})");
    check(newer.info.error.empty() && newer.info.hasUpdate,
          "newer dotted numeric release version reports an update");

    const auto equal = checkUrl("7.2.3", "https://updates.example.test/latest",
                                R"({"tag_name":"7.2.3"})");
    check(equal.info.error.empty() && !equal.info.hasUpdate,
          "equal dotted numeric release version reports no update");

    const auto older = checkUrl("7.2.3", "https://updates.example.test/latest",
                                R"({"tag_name":"V7.1.9"})");
    check(older.info.error.empty() && !older.info.hasUpdate,
          "older dotted numeric release version reports no update");

    const auto trailingZero = checkUrl("7.2", "https://updates.example.test/latest",
                                       R"({"tag_name":"7.2.0"})");
    check(trailingZero.info.error.empty() && !trailingZero.info.hasUpdate,
          "7.2 and 7.2.0 compare as equal");

    const auto invalidCurrent = checkUrl("development", "https://updates.example.test/latest",
                                         R"({"tag_name":"7.2.0"})");
    check(!invalidCurrent.info.error.empty() && !invalidCurrent.info.hasUpdate,
          "an invalid current version returns an error through the callback");
}

void testInvalidResponses()
{
    const auto malformed = checkUrl("7.2.3", "https://updates.example.test/latest",
                                    R"({"tag_name":"release-latest"})");
    check(!malformed.info.error.empty() && !malformed.info.hasUpdate,
          "malformed release tags return an error without an update");

    const auto overflow = checkUrl(
        "7.2.3", "https://updates.example.test/latest",
        R"({"tag_name":"999999999999999999999999999999999999999999.2.3"})");
    check(!overflow.info.error.empty() && !overflow.info.hasUpdate,
          "overflowing release tags return an error instead of throwing");

    const auto missing = checkUrl("7.2.3", "https://updates.example.test/latest",
                                  R"({"name":"Release without tag"})");
    check(!missing.info.error.empty() && missing.info.latestVersion.empty(),
          "a response without tag_name returns a parse error");

    const auto invalidJson = checkUrl("7.2.3", "https://updates.example.test/latest",
                                      R"({"tag_name":)");
    check(!invalidJson.info.error.empty(), "invalid JSON returns a parse error");
}

void testFetchError()
{
    const auto result = checkUrl("7.2.3", "https://updates.example.test/latest", {},
                                 "offline test failure");
    check(result.requestedUrl == "https://updates.example.test/latest",
          "the injected fetcher receives the requested custom endpoint");
    check(result.info.error == "offline test failure" && !result.info.hasUpdate,
          "fetch errors are returned unchanged");
}

void testReleaseUrlPolicy()
{
    const auto customWithUrl = checkUrl(
        "7.0.0", "https://updates.example.test/latest",
        R"({"tag_name":"7.1.0","html_url":"https://downloads.example.test/mviewer/7.1.0"})");
    check(customWithUrl.info.releaseUrl == "https://downloads.example.test/mviewer/7.1.0",
          "custom endpoints use the response html_url");

    const auto customWithoutUrl = checkUrl("7.0.0", "https://updates.example.test/latest",
                                           R"({"tag_name":"7.1.0"})");
    check(customWithoutUrl.info.releaseUrl.empty(),
          "custom endpoints without html_url do not fabricate a release link");

    const auto canonicalFallback = checkUrl(
        "7.0.0", "https://api.github.com/repos/acme/viewer/releases/latest",
        R"({"tag_name":"v7.1.0"})");
    check(canonicalFallback.info.releaseUrl ==
              "https://github.com/acme/viewer/releases/tag/v7.1.0",
          "canonical GitHub API endpoints safely derive a missing release link");

    const auto lookalike = checkUrl(
        "7.0.0", "https://updates.example.test/repos/acme/viewer/releases/latest",
        R"({"tag_name":"v7.1.0"})");
    check(lookalike.info.releaseUrl.empty(),
          "non-GitHub lookalike endpoints never derive a release link");

    const auto httpUrl = checkUrl(
        "7.0.0", "https://updates.example.test/latest",
        R"({"tag_name":"7.1.0","html_url":"http://downloads.example.test/7.1.0"})");
    check(httpUrl.info.releaseUrl.empty(), "HTTP release links are rejected");

    for (const std::string &unsafeUrl : {"javascript:alert(1)", "file:///tmp/release",
                                         "../relative/release"})
    {
        const auto unsafeCustom = checkUrl(
            "7.0.0", "https://updates.example.test/latest",
            "{\"tag_name\":\"7.1.0\",\"html_url\":\"" + unsafeUrl + "\"}");
        check(unsafeCustom.info.releaseUrl.empty(),
              "custom endpoints reject non-HTTPS release links");
    }

    const auto unsafeCanonical = checkUrl(
        "7.0.0", "https://api.github.com/repos/acme/viewer/releases/latest",
        R"json({"tag_name":"v7.1.0","html_url":"javascript:alert(1)"})json");
    check(unsafeCanonical.info.releaseUrl ==
              "https://github.com/acme/viewer/releases/tag/v7.1.0",
          "canonical GitHub endpoints fall back when html_url is unsafe");

    const auto httpCanonical = checkUrl(
        "7.0.0", "https://api.github.com/repos/acme/viewer/releases/latest",
        R"({"tag_name":"v7.1.0","html_url":"http://github.com/acme/viewer"})");
    check(httpCanonical.info.releaseUrl ==
              "https://github.com/acme/viewer/releases/tag/v7.1.0",
          "canonical GitHub endpoints fall back when html_url is not HTTPS");
}

void testEndpointPolicy()
{
    for (const std::string &endpoint : {"http://updates.example.test/latest",
                                        "file:///tmp/update.json", "../update.json"})
    {
        int fetchCount = 0;
        int callbackCount = 0;
        mviewer::core::UpdateInfo result;
        mviewer::core::UpdateChecker checker(
            "7.0.0",
            [&fetchCount](const std::string &, std::string &)
            {
                ++fetchCount;
                return std::string(R"({"tag_name":"7.1.0"})");
            });
        checker.checkUrl(endpoint,
                         [&callbackCount, &result](const mviewer::core::UpdateInfo &info)
                         {
                             ++callbackCount;
                             result = info;
                         });
        check(fetchCount == 0, "non-HTTPS endpoints are rejected before fetching");
        check(callbackCount == 1 && !result.error.empty() && !result.hasUpdate,
              "rejected endpoints callback exactly once with an error");
    }
}

void testFetcherExceptions()
{
    for (const bool throwKnown : {true, false})
    {
        int callbackCount = 0;
        mviewer::core::UpdateInfo result;
        mviewer::core::UpdateChecker checker(
            "7.0.0",
            [throwKnown](const std::string &, std::string &) -> std::string
            {
                if (throwKnown)
                    throw std::runtime_error("offline fetch exception");
                throw 42;
            });
        checker.checkUrl("https://updates.example.test/latest",
                         [&callbackCount, &result](const mviewer::core::UpdateInfo &info)
                         {
                             ++callbackCount;
                             result = info;
                         });
        check(callbackCount == 1 && !result.error.empty() && !result.hasUpdate,
              "fetcher exceptions callback exactly once with an error");
    }
}
} // namespace

int main()
{
    testGitHubUrlAndJson();
    testVersionOrdering();
    testInvalidResponses();
    testFetchError();
    testReleaseUrlPolicy();
    testEndpointPolicy();
    testFetcherExceptions();
    std::printf("test_updatechecker: %s (%d failures)\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
