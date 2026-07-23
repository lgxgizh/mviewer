// M14.2: Plugin ABI freeze — compatibility gate unit tests.
//
// This test verifies the frozen ABI triple (apiVersion / abiVersion /
// sdkVersion) and the compatibility predicate the loader enforces. It is
// deliberately free of any plugin-DLL loading: loading a Qt-linking plugin
// and then tearing the test process down is unsafe on Windows (see
// PluginManager::unload), and the *positive* loader path is already covered
// end-to-end by test_pluginregistry (which loads example_analyzer, a v1.x
// ABI-compatible plugin, and creates+analyzes it). The *negative* path
// (rejecting an incompatible abiVersion) is exactly pluginABICompatible(),
// which this test exercises directly — including the abiVersion=999 case that
// the dedicated example_analyzer_badabi plugin declares.
#include "core/plugin/PluginABI.h"

#include <iostream>
#include <string>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

int main()
{
    const PluginABI host = hostPluginABI();

    // The frozen triple for v1.0.0.
    CHECK(host.apiVersion == 1, "host apiVersion == 1");
    CHECK(host.abiVersion == 1, "host abiVersion == 1 (v1.x ABI frozen)");
    CHECK(host.sdkVersion == 10000, "host sdkVersion == 10000 (1.0.0)");

    // Identical descriptors are compatible.
    PluginABI same = host;
    CHECK(pluginABICompatible(host, same), "identical ABI compatible");

    // ABI mismatch must be rejected (hard gate) — the exact case
    // example_analyzer_badabi declares (abiVersion 999).
    PluginABI badAbi = host;
    badAbi.abiVersion = 999;
    CHECK(!pluginABICompatible(host, badAbi), "abi mismatch rejected");

    // Plugin API newer than host must be rejected.
    PluginABI newerApi = host;
    newerApi.apiVersion = host.apiVersion + 1;
    CHECK(!pluginABICompatible(host, newerApi), "plugin api newer than host rejected");

    // Plugin API older than host accepted (backward compatibility).
    PluginABI olderApi = host;
    olderApi.apiVersion = host.apiVersion - 1;
    CHECK(pluginABICompatible(host, olderApi), "plugin api older than host accepted");

    // SDK mismatch must NOT block, but must warn.
    PluginABI oldSdk = host;
    oldSdk.sdkVersion = 1;
    CHECK(pluginABICompatible(host, oldSdk), "sdk mismatch does not block");
    CHECK(!pluginABIWarnings(host, oldSdk).empty(), "sdk mismatch warns");
    CHECK(pluginABIWarnings(host, same).empty(), "aligned sdk has no warning");

    if (g_fail)
    {
        std::cerr << g_fail << " FAILED\n";
        return 1;
    }
    std::cout << "pluginabi_tests: all passed\n";
    return 0;
}
