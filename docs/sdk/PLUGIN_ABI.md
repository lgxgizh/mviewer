# Plugin ABI (frozen for v1.x)

> Status: **FROZEN** as of M14.2. The triple below is the contract every
> `v1.x` plugin is validated against. A plugin built against the v1.0.0 SDK
> will load on any `v1.x` host **without recompilation**.

This document is the single source of truth for binary compatibility between
MViewer and its plugins. It complements [`PLUGIN_SDK.md`](PLUGIN_SDK.md), which
describes the *API* (interfaces, C exports, build contract).

## The ABI descriptor

Every plugin exports a descriptor via a single C symbol:

```cpp
extern "C" const PluginABI* mviewer_plugin_abi();
```

`PluginABI` (defined in `src/core/plugin/PluginABI.h`):

```cpp
struct PluginABI
{
    uint32_t apiVersion = MVIEWER_API_VERSION;   // 1
    uint32_t abiVersion = MVIEWER_ABI_VERSION;  // 1
    uint32_t sdkVersion = MVIEWER_SDK_VERSION;   // 10000 == 1.0.0
};
```

### Field semantics

| Field | Meaning | Gate |
|-------|---------|------|
| `apiVersion` | Plugin **API contract** — the `Analyzer`/`Decoder`/`Exporter` interfaces, exported C symbols, and capability flags. | Plugin `apiVersion` **≤** host `apiVersion` is accepted (backward compatible). A newer plugin is rejected. |
| `abiVersion` | **Binary** ABI level — struct layouts, calling convention, the `std`/Qt boundary, and the compiler/Qt build. | **Must match exactly.** A mismatch means the plugin must be recompiled against the host's SDK. |
| `sdkVersion` | SDK **release** the plugin was built against, encoded as `major*10000 + minor*100 + patch` (e.g. `10000` == `1.0.0`). | Informational only. A mismatch produces a **warning**, never a load failure. |

## How the host enforces it

At load time (`PluginLoader::loadPlugin`) the host:

1. Resolves `mviewer_plugin_abi()`.
2. Calls `pluginABICompatible(hostPluginABI(), *pluginAbi)`.
3. If incompatible (`abiVersion` mismatch, or plugin `apiVersion` newer than host),
   the plugin is **rejected** with a diagnostic error and the library handle is
   closed — no instance is created, nothing is registered.
4. If `sdkVersion` differs, a non-fatal warning is logged and loading proceeds.

Legacy plugins that still export only `mviewer_plugin_api_version()` (no
`mviewer_plugin_abi`) fall back to the old single-version check. New plugins
**must** export `mviewer_plugin_abi()`.

## Bump policy

- **`abiVersion`** — bump only when the binary layout actually changes
  (struct size/order, calling convention, a different Qt/compiler build,
  a changed `std` boundary). Bumping it **breaks** every existing plugin; that
  is a major release event (v2.0), not a patch. **For the entire v1.x line
  `abiVersion` stays `1`.**
- **`apiVersion`** — bump when an interface or exported symbol changes in a way
  that an old plugin could not satisfy. Adding *new* optional symbols/capabilities
  without removing old ones does **not** require a bump.
- **`sdkVersion`** — set from `MVIEWER_SDK_VERSION` at build time; tracks the SDK
  release, never gates loading.

## Authoring a v1.x plugin (ABI contract)

Minimal required exports (the host probes `create*` in the order Analyzer →
Decoder → Exporter to learn the plugin's kind):

```cpp
extern "C" {
    Analyzer   *createAnalyzer();          // Analyzer plugin
    void        destroyAnalyzer(Analyzer*);
    IDecoder   *createDecoder();           // Decoder plugin (M14.3)
    void        destroyDecoder(IDecoder*);
    IExporter  *createExporter();          // Exporter plugin (M14.3)
    void        destroyExporter(IExporter*);
    const char *pluginName();              // matches the impl's name()
    const PluginABI *mviewer_plugin_abi(); // MUST: frozen triple
    // optional:
    uint32_t    mviewer_plugin_capabilities();
}
```

`mviewer_plugin_abi()` should return a `static` `PluginABI` initialized from the
macros shipped with the SDK:

```cpp
extern "C" const PluginABI* mviewer_plugin_abi()
{
    static const PluginABI abi; // defaults to MVIEWER_API/ABI/SDK_VERSION
    return &abi;
}
```

Because the host and the plugin both read the same frozen macros from the v1.0.0
SDK, `abiVersion` is guaranteed equal and the plugin loads on any v1.x host.

## Test coverage

`test_pluginabi` (ctest `pluginabi_tests`) verifies:

- the host triple is `{api=1, abi=1, sdk=10000}`;
- identical descriptors are compatible;
- `abiVersion` mismatch is rejected;
- a plugin `apiVersion` newer than the host is rejected;
- an older `apiVersion` is accepted;
- `sdkVersion` mismatch does **not** block but warns;
- the shipped `example_analyzer` plugin loads with a matching ABI;
- the test-only `example_analyzer_badabi` plugin (which exports `abiVersion=999`)
  is rejected by the freeze gate;
- `ctest pluginexamples_tests` loads the Analyzer, Decoder (`example_decoder`,
  PPM) and Exporter (`example_exporter`, PNG/BMP) example plugins, proving all
  three kinds self-register through the same ABI-gated loader and are usable
  end-to-end (decode a PPM, export the result to PNG).
