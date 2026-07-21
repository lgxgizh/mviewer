# MViewer Plugin SDK

Write an analysis plugin for MViewer in C++ and have it recognized at runtime.
This document is the **stable contract** (M13 Phase 6). It is deliberately small:
one interface, three C exports, one build rule.

> Scope note: this SDK covers **analyzer plugins** (the `Analyzer` interface).
> Command / compare / export plugins are future surface; the loader is generic
> enough to host them later without changing the C ABI below.

## 1. The interface you implement

Include `core/analyzer/Analyzer.h` and derive from `Analyzer`:

```cpp
class Analyzer {
  public:
    virtual ~Analyzer() = default;
    virtual std::string name() const = 0;            // unique id, e.g. "acme.contrast"
    virtual std::string description() const = 0;
    virtual bool analyze(const ImageFrame &frame) = 0;
    virtual bool analyzeRegion(const ImageFrame &frame,
                               const mviewer::domain::Selection &region) = 0;
    virtual std::string resultText() const;          // optional, shown in UI
    virtual AnalyzerCapability capabilities() const; // optional flags
};
```

Capabilities (`core/analyzer/AnalyzerCapability.h`): `SingleImage`,
`MultiImage`, `RegionOfInterest`, `StatsOutput`. They let the host skip
analyzers that don't apply (e.g. PSNR needs two frames).

Pixel access in `analyze`:

```cpp
const ImageBuffer v = frame.pixels().view();
const int cpp   = v.channelsPerPixel();   // 1, 3, or 4
const uint8_t *row = v.data + y * v.stride();
const uint8_t *p   = row + x * cpp;
```

`ImageData` / `ImageBuffer` are value types in `core/image`; see the example
for a complete read loop. We deliberately expose a **stable, value-based pixel
view** rather than Qt types, so plugins never depend on Qt internals.

## 2. The C ABI you export

Every plugin is a shared library exporting these C symbols
(no name mangling, no version suffix). The **ABI descriptor** is mandatory
from M14.2 — the frozen contract (and how `abiVersion` is enforced) is in
[`PLUGIN_ABI.md`](PLUGIN_ABI.md).

```cpp
extern "C" {
    // --- one create/destroy pair, matching the plugin's kind ---
    Analyzer   *createAnalyzer();          // Analyzer plugin
    void        destroyAnalyzer(Analyzer*);
    IDecoder   *createDecoder();           // Decoder plugin (M14.3)
    void        destroyDecoder(IDecoder*);
    IExporter  *createExporter();          // Exporter plugin (M14.3)
    void        destroyExporter(IExporter*);
    const char *pluginName();              // matches the implementation's name()
    const PluginABI *mviewer_plugin_abi(); // M14.2: frozen ABI triple (required)
    // optional:
    uint32_t    mviewer_plugin_capabilities();
}
```

On Windows use `__declspec(dllexport)`; on ELF use default visibility. The
bundled `MVIEWER_PLUGIN_EXPORT` macro (see `plugins/example`) handles both.

The host (`PluginLoader` / `PluginManager`) probes the `create*` exports in the
order **Analyzer → Decoder → Exporter**, then registers the returned instance
into the matching registry (`AnalyzerRegistry` / `DecoderRegistry` /
`ExporterRegistry`) and calls the corresponding `destroy*` when the plugin is
unloaded. **You own the lifetime inside the plugin** — allocate in `create*`,
free in `destroy*`. The host wraps the instance in a `shared_ptr` whose deleter
invokes your `destroy*` so allocation and deallocation stay in the plugin module.

## 3. ABI stability rules (MUST hold)

These are the hard rules. Break any one and the plugin will misbehave or crash
on load — there is no compatibility shim.

1. **Same toolchain.** Build the plugin with the **same compiler + version**
   as `mviewer_core` (currently MSVC 19.x / `c++20`). Mixed toolchains =
   different CRT/`std` layout = undefined behavior.
2. **Same Qt build.** `mviewer_core` links Qt 6. If your plugin touches Qt, use
   the identical Qt 6 build (same minor version). The analyzer interface itself
   is Qt-free, so a pure-pixel plugin need not link Qt — but if it does, match it.
3. **`mviewer_core` stays SHARED.** Host and plugin must share **one**
   `Analyzer` / `ImageData` vtable. A static core would give each module its
   own copy and crash on cross-module `delete`. This is enforced by
   `WINDOWS_EXPORT_ALL_SYMBOLS` on `mviewer_core` — do not change it.
4. **C surface is frozen.** `createAnalyzer` / `destroyAnalyzer` /
   `createDecoder` / `destroyDecoder` / `createExporter` / `destroyExporter` /
   `pluginName` signatures will not change within a major version. The C++
   `Analyzer` / `IDecoder` / `IExporter` vtables may grow (new virtuals added at
   the **end**, with defaults) but existing overrides remain valid.
5. **Don't unload at runtime.** Keep plugins loaded for the app lifetime.
   Unloading a Qt-linking DLL on Windows at teardown is unsafe (CRT detach
   ordering) — the host keeps handles alive and releases them on process exit.

## 4. Build (in-tree, reference)

The bundled examples are the canonical build:

```
# from repo root (after a normal Release build of MViewer)
plugins/example/  ->  example_analyzer.{dll,so,dylib}      # Analyzer
                     example_decoder.{dll,so,dylib}       # Decoder  (PPM)
                     example_exporter.{dll,so,dylib}      # Exporter (PNG/BMP)
```

`plugins/example/CMakeLists.txt` links `mviewer_core` (PRIVATE) and the exporter
also links `Qt6::Gui` (for `QImage`); each `.dll` is dropped next to the app
binaries. The end-to-end loader contract for all three is verified by
`ctest pluginexamples_tests`. See `plugins/example/README.md`.

## 5. Build (third party / out-of-tree)

A plugin author needs only the **SDK header set** + the `mviewer_core` import
library / DLL (shipped with MViewer). Minimal recipe:

```cmake
add_library(my_analyzer SHARED my_analyzer.cpp)
target_include_directories(my_analyzer PRIVATE <mviewer-sdk>/include)  # SDK headers
target_link_libraries(my_analyzer PRIVATE <mviewer-sdk>/lib/mviewer_core.lib)
target_compile_features(my_analyzer PRIVATE cxx_std_20)
# same compiler / Qt / c++20 as mviewer_core
```

The SDK header set = `core/analyzer/Analyzer.h`, `AnalyzerCapability.h`,
`core/image/ImageFrame.h`, `ImageBuffer.h`, `domain/Selection.h`,
`domain/*` (pixel/format types). A packaged `mviewer-sdk.zip` (headers +
`mviewer_core` import lib + this doc) is the distribution form; produce it from
the release pipeline (see M13.3).

## 6. Load by MViewer

Drop the plugin next to the app (or in the configured plugin directory). At
startup `PluginManager::loadDirectory()` scans, `dlopen`/`LoadLibrary`s each
plugin, probes its `create*` exports to learn its kind, and self-registers the
instance into the matching registry (`AnalyzerRegistry` / `DecoderRegistry` /
`ExporterRegistry`). Analyzers appear in the Analysis panel; decoders are used
by the image-opening path; exporters by the export path — all automatically, no
code change in MViewer.

The end-to-end contract for all three kinds is enforced by
`ctest pluginexamples_tests` (build → load → self-register → use), the analyzer
round-trip by `pluginregistry_tests` (create → analyze → region analyze), plus
`pluginloader_tests` / `pluginmanager_tests` for the loader/manager internals.

## 7. Example plugins (M14.3 reference)

| Plugin | Kind | Interface | Demonstrates |
| --- | --- | --- | --- |
| `ExampleAnalyzerPlugin.cpp` | Analyzer | `Analyzer` | `analyze()` / `analyzeRegion()` + result metrics |
| `ExampleDecoderPlugin.cpp` | Decoder | `IDecoder` | decoding a simple uncompressed format (PPM P6) |
| `ExampleExporterPlugin.cpp` | Exporter | `IExporter` | encoding an `ImageData` to PNG/BMP via `QImage` |

All three export the frozen ABI triple (`mviewer_plugin_abi()`), the legacy
`mviewer_plugin_api_version()`, and the `pluginName()` + `create*`/`destroy*`
contract. Copy any one as the skeleton for your own plugin.

## See also

- `plugins/example/ExampleAnalyzerPlugin.cpp` — complete working Analyzer plugin.
- `plugins/example/ExampleDecoderPlugin.cpp` — complete working Decoder plugin.
- `plugins/example/ExampleExporterPlugin.cpp` — complete working Exporter plugin.
- `docs/adr/005-why-plugin-analysis.md` — why analysis is plugin-based.
- `src/core/plugin/PluginLoader.{h,cpp}`, `PluginManager.{h,cpp}` — host side.
- `src/core/export/IExporter.h`, `ExporterRegistry.{h,cpp}` — Exporter contract.
