# MViewer Plugin Specification

## Overview

The plugin system provides extensibility without modifying the core application. A plugin can contribute an analyzer, an image decoder, an exporter, an importer or a compare algorithm, and all kinds are loaded through the same ABI-gated loader. The system is designed to be lightweight, versioned, and isolated from the core browsing performance.

**Status:** Implemented and frozen. The Plugin SDK ABI v1 is frozen by [ADR 013](adr/013-p2-plugin-sdk-frozen.md); the reference plugins in `plugins/example/` ship with the build, and the plugin gates (`pluginregistry_tests`, `pluginabi_tests`, `pluginexamples_tests`) run as part of `.\build.ps1 Test`. Every plugin declares the frozen ABI triple `apiVersion` 1 / `abiVersion` 1 / `sdkVersion` 10000 (SDK 1.0.0) from `src/core/plugin/PluginABI.h`.

---

## Design Goals

1. **Non-intrusive** — Plugins cannot degrade core browsing performance
2. **Versioned** — ABI stability across minor versions
3. **Discoverable** — Automatic plugin detection and loading
4. **Isolated** — A throwing analyzer is caught and logged by `AnalyzerRegistry::runAnalyzer` instead of failing the batch; a plugin that corrupts memory can still take the process down (see Plugin Security)
5. **Simple** — Minimal boilerplate for plugin authors

---

## Plugin Types

A plugin's kind is decided by which `create*` export it provides. The host probes
the exports in a fixed order (Analyzer → Decoder → Exporter → Importer → Compare
Algorithm) and registers the factory it finds with the matching registry
(`src/core/plugin/PluginManager.cpp`).

### 1. Analyzer Plugins

Add an analysis algorithm. Base class `Analyzer`
(`src/core/analyzer/Analyzer.h`); registered into `AnalyzerRegistry`.

```cpp
class Analyzer {
public:
    virtual ~Analyzer() = default;

    virtual std::string name() const = 0;   // stable id, e.g. "example.mean_luminance"
    virtual std::string description() const = 0;
    virtual bool analyze(const ImageFrame& frame) = 0;
    virtual bool analyzeRegion(const ImageFrame& frame,
                               const mviewer::domain::Selection& region) = 0;

    // Optional reporting / capability hooks (each has a default).
    virtual std::string resultText() const;
    virtual std::unordered_map<std::string, double> resultMetrics() const;
    virtual AnalyzerCapability capabilities() const;
    virtual AnalyzerInfo info() const;
};
```

### 2. Decoder Plugins

Add support for new image formats. Base interface `IDecoder`
(`src/core/image/decoder/IDecoder.h`); registered into `DecoderRegistry`.

```cpp
class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual bool canDecode(const std::string& path) const = 0;
    virtual ImageData decodeFull(const std::string& path) const = 0;
    virtual ImageData decodeScaled(const std::string& path, int maxEdge) const = 0;
    virtual ImageData decodeScaled(const std::string& path, int maxEdge,
                                   mviewer::domain::ImageMetadata& outMeta) const;
    virtual ImageData decodeFull(const std::string& path,
                                 mviewer::domain::ImageMetadata& outMeta) const = 0;
    virtual std::vector<std::string> extensions() const = 0;  // lowercased, no dot
    virtual const char* name() const = 0;
};
```

Every decode returns an `ImageData` value: a null buffer means failure. There is
no `std::expected` in the decoder contract.

### 3. Exporter Plugins

Add output formats for batch conversion. Base interface `IExporter`
(`src/core/export/IExporter.h`); registered into `ExporterRegistry`.

```cpp
class IExporter {
public:
    virtual ~IExporter() = default;

    virtual std::string name() const = 0;                     // e.g. "png-exporter"
    virtual std::string description() const = 0;
    virtual std::vector<std::string> extensions() const = 0;  // e.g. {"png","bmp"}
    virtual bool exportImage(const ImageData& img, const std::string& outPath) = 0;
};
```

### 4. Importer Plugins

Turn an external catalog / project / folder layout into a
`mviewer::domain::Workspace` (folders + image metadata, no pixels). Base
interface `IImporter` (`src/core/import/IImporter.h`); registered into
`ImporterRegistry`.

```cpp
class IImporter {
public:
    virtual ~IImporter() = default;

    virtual std::string name() const = 0;                     // e.g. "folder-importer"
    virtual std::string description() const = 0;
    virtual std::vector<std::string> extensions() const = 0;  // empty = any path
    virtual bool canImport(const std::string& path) const = 0;
    virtual mviewer::domain::Workspace importWorkspace(const std::string& path) const = 0;
};
```

### 5. Compare Algorithm Plugins

Add third-party compare metrics and an optional heatmap. Base interface
`ICompareAlgorithm` (`src/core/compare/ICompareAlgorithm.h`, scope
`mviewer::core`); the loaded id is kept in
`PluginManager::PluginEntry::compareAlgorithmId`.

```cpp
class ICompareAlgorithm {
public:
    virtual ~ICompareAlgorithm() = default;

    virtual std::string name() const = 0;  // stable id, e.g. "example.psnr_plus"
    virtual std::string displayName() const = 0;
    virtual CompareAlgorithmResult run(const ImageFrame& reference,
                                       const std::vector<const ImageFrame*>& candidates) = 0;
};
```

There is no UI-extension plugin kind and no metadata-handler plugin kind:
toolbar / panel contributions and metadata parsing are not extension points in
this SDK.

---

## Plugin Interface

### Exported C symbols

A plugin is a shared library, not a polymorphic `IPlugin` object handed to the
host. It exports the frozen ABI descriptor plus exactly one kind-specific
`create*` / `destroy*` pair (`src/core/plugin/PluginABI.h`; the full contract is
in [`docs/sdk/PLUGIN_ABI.md`](sdk/PLUGIN_ABI.md)):

```cpp
extern "C" {

/// MUST (M14.2): the frozen ABI triple.
__declspec(dllexport)
const PluginABI* mviewer_plugin_abi();

/// Display name; matches the implementation's name().
__declspec(dllexport)
const char* pluginName();

/// Kind-specific pair — exactly one kind per library, e.g.:
__declspec(dllexport) Analyzer* createAnalyzer();
__declspec(dllexport) void      destroyAnalyzer(Analyzer*);

/// Optional legacy single-version export (loader fallback).
__declspec(dllexport) int mviewer_plugin_api_version();

} // extern "C"
```

### ABI descriptor

```cpp
struct PluginABI {
    uint32_t apiVersion = MVIEWER_API_VERSION;  // 1 — API contract
    uint32_t abiVersion = MVIEWER_ABI_VERSION;  // 1 — binary compatibility
    uint32_t sdkVersion = MVIEWER_SDK_VERSION;  // 10000 == SDK 1.0.0
};
```

`pluginABICompatible()` requires an exact `abiVersion` match and a plugin
`apiVersion` no newer than the host; a differing `sdkVersion` only produces the
warning returned by `pluginABIWarnings()`. A rejected plugin is never
instantiated and nothing is registered from it.

### Capabilities

```cpp
enum class PluginCapability : uint32_t {
    None        = 0,
    SingleImage = 1 << 0,
    Region      = 1 << 1,
    Batch       = 1 << 2,
    RAW         = 1 << 3,
};
```

`mviewer_plugin_capabilities()` is declared in `PluginABI.h` as an optional
export, but the loader does not resolve it. The capability query that exists
today is the analyzer one: `Analyzer::capabilities()` returning
`AnalyzerCapability`, queried through `AnalyzerRegistry::capabilitiesOf()` and
`AnalyzerRegistry::queryByCapability()`.

---

## Plugin Discovery

### Search Paths

| Platform | Path |
| ---------- | ------ |
| All | `<exe dir>/plugins` — resolved next to the executable and created on first run (`src/application/Startup.cpp`) |
| All | Extra directories listed in the Plugin Settings search-path list (QSettings `plugins/searchPaths`; the default entry is `<exe dir>/plugins`) |

There is no `%APPDATA%` / `~/.local/share` fallback and no `MVIEWER_PLUGIN_PATH`
environment variable. Because the plugin home is derived from
`QCoreApplication::applicationDirPath()`, a launch through a shortcut or the
Start Menu finds the same directory regardless of the working directory.

### Discovery Process

1. Scan the search path(s) for `.dll` (Windows) or `.so`/`.dylib` files
   (`PluginManager::scanDirectory()`)
2. Load each library and resolve `mviewer_plugin_abi()`, the optional legacy
   `mviewer_plugin_api_version()`, `pluginName()` and the `create*` / `destroy*`
   pairs
3. Validate the descriptor with `pluginABICompatible()`; on an `abiVersion`
   mismatch (or a plugin `apiVersion` newer than the host) the handle is closed
   and the plugin is rejected
4. Probe the exports in the order Analyzer → Decoder → Exporter → Importer →
   Compare Algorithm; the first `create*` found determines the kind, and the
   probe instance's `name()` becomes the plugin id
5. Register the factory with the matching registry (`AnalyzerRegistry`,
   `DecoderRegistry`, `ExporterRegistry`, `ImporterRegistry`)
6. Keep the library handle in `PluginManager` and record a `PluginEntry` for the
   Plugin Settings UI

### Entry Point

```cpp
// C linkage for ABI stability.
extern "C" {

/// Kind-specific create/destroy pair — exactly one kind per library.
__declspec(dllexport)  // Windows; __attribute__((visibility("default"))) on Linux
Analyzer* createAnalyzer();
__declspec(dllexport)
void destroyAnalyzer(Analyzer* analyzer);

/// Display name; matches the implementation's name().
__declspec(dllexport)
const char* pluginName();

/// Frozen ABI triple — required from v1.x on.
__declspec(dllexport)
const PluginABI* mviewer_plugin_abi();

} // extern "C"
```

There is no `IPlugin* mviewer_plugin_create()` / `mviewer_plugin_destroy(IPlugin*)`
entry point: the host never receives a polymorphic plugin object, only the
kind-specific interface instance returned by `create*`.

---

## Plugin Lifecycle

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ Discover │───▶│  Load    │───▶│ Register │───▶│  Active  │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
                                                      │
                                                      ▼
                                               ┌──────────┐
                                               │  Unload  │
                                               └──────────┘
```

### States

| State | Description |
| ------- | ------------- |
| Discovered | Candidate file found by `PluginManager::scanDirectory()` |
| Loaded | Library opened (`LoadLibraryW` / `dlopen`) and the ABI gate passed |
| Registered | A `create*` probe returned an instance whose `name()` became the plugin id |
| Active | The factory is registered with its registry and usable by the app |
| Disabled | User-disabled in Plugin Settings (QSettings `plugins/disabled`); it stays loaded and only its UI entry is marked |
| Error | Open / ABI gate / kind probe failed: the handle is closed and `lastError()` reports why |

### Lifecycle Events

| Event | When | What happens |
| ------- | ------ | --------------- |
| `load(path)` | Startup (`startupPlugins`) or Plugin Settings "rescan" | Open the library, resolve symbols, run the ABI gate |
| Kind probe | Immediately after the gate | `create*` is called once, the instance's `name()` becomes the id, the probe instance is destroyed |
| Registration | Probe succeeded | The factory is registered with the matching registry; the handle is kept in `PluginManager` |
| `unload(path)` / `unloadAll()` | Plugin Settings / application exit | The factory is unregistered from its registry (no dangling entry into the plugin module); the library handle is deliberately **not** closed — unloading a Qt-linking DLL mid-process crashes on Windows |

There is no plugin-side `initialize()` / `shutdown()` callback: an instance is
created on demand through the plugin's `create*` export and destroyed through the
matching `destroy*` export, so the plugin supplies its own destructor semantics.
The host owns the library handle, so the module stays loaded for the process
lifetime.

---

## Plugin API Versioning

### Version Scheme

Three integers travel with every plugin (`src/core/plugin/PluginABI.h`):

| Field | Meaning | Gate |
| ------- | --------- | ------ |
| `apiVersion` | Plugin API contract — the Analyzer / Decoder / Exporter interfaces, exported C symbols and capability flags | Plugin `apiVersion` ≤ host is accepted; a newer plugin is rejected |
| `abiVersion` | Binary ABI level — struct layouts, calling convention, the std/Qt boundary and the compiler/Qt build | Must match the host exactly; a mismatch means the plugin must be recompiled |
| `sdkVersion` | SDK release the plugin was built against (`major*10000 + minor*100 + patch`) | Informational only; a mismatch warns and never blocks loading |

Current host values: `MVIEWER_API_VERSION` = 1, `MVIEWER_ABI_VERSION` = 1,
`MVIEWER_SDK_VERSION` = 10000 (SDK 1.0.0). For the entire v1.x line
`abiVersion` stays 1, so a plugin built against the v1.0.0 SDK loads on any
v1.x host without recompilation.

### Breaking Changes Policy

- `abiVersion` is bumped only when the binary layout itself changes; that is a
  major-release event (v2.0) that requires every plugin to be rebuilt.
- `apiVersion` is bumped when an interface or exported symbol changes in a way
  that an old plugin could not satisfy. Adding new optional symbols does not
  require a bump.
- The legacy `mviewer_plugin_api_version()` export is still accepted as a
  single-version fallback for plugins that do not export
  `mviewer_plugin_abi()`.

The full bump policy lives in [`docs/sdk/PLUGIN_ABI.md`](sdk/PLUGIN_ABI.md) and
[ADR 013](adr/013-p2-plugin-sdk-frozen.md).

---

## Plugin Security

### Current Model (In-Process)

Plugins run in the same process as MViewer. This provides maximum performance but requires trust.

**Implications:**

- A misbehaving plugin can crash the application
- Plugins have access to the same memory space
- No sandboxing (by design, for performance)

### Mitigations

1. **Validation** — The frozen ABI descriptor and the exported entry points are verified before any instance is created (`pluginABICompatible()` plus the kind probe)
2. **Build contract** — Plugin, host and `mviewer_core` must be built with the same compiler, Qt and `c++20` settings, and `mviewer_core` must stay a SHARED library so host and plugin share one vtable
3. **Trust model** — Installing a plugin is equivalent to running code inside MViewer; there is no permission system. Only load plugins you trust.

Plugin signing and permission-based approval are not implemented.

### Future: Out-of-Process (Optional)

For untrusted plugins, an out-of-process execution model may be added:

- Plugin runs in separate process
- IPC via shared memory + message queue
- Higher latency but better isolation
- Suitable for export filters and batch operations
- Not suitable for decode plugins (latency-sensitive)

---

## Plugin Configuration

### Settings

Plugins do not get a settings namespace of their own. What MViewer persists is
listed in `src/pluginsettings.cpp`:

| QSettings key | Type | Meaning |
| --------------- | ------ | --------- |
| `plugins/searchPaths` | `QStringList` | Directories scanned for plugin libraries; defaults to `<exe dir>/plugins` |
| `plugins/disabled` | `QStringList` | Plugin display names (`pluginName()`) the user disabled; the plugin stays loaded and the UI marks it disabled |

### UI Integration

- The Plugin Settings dialog lists every loaded plugin with its kind (analyzer / decoder / exporter / importer) and path
- The search-path list is editable; "rescan" loads newly dropped libraries without restarting the app
- A plugin cannot ship its own settings widget: there is no host service API for that

---

## Example Plugin: Analyzer

The reference analyzer (`plugins/example/ExampleAnalyzerPlugin.cpp`) is a
complete, buildable plugin — use it as the template. The decoder
(`ExampleDecoderPlugin.cpp`), exporter (`ExampleExporterPlugin.cpp`) and
importer (`ExampleImporterPlugin.cpp`) examples follow the same shape with the
matching interface and exports.

```cpp
// example_analyzer_plugin.cpp
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerCapability.h"
#include "core/image/ImageFrame.h"
#include "core/plugin/PluginABI.h"
#include "domain/Selection.h"

#define MVIEWER_PLUGIN_EXPORT __declspec(dllexport)  // __attribute__((visibility("default"))) on Linux

class MeanLuminanceAnalyzer : public Analyzer {
public:
    std::string name() const override { return "example.mean_luminance"; }
    std::string description() const override {
        return "Example plugin: mean luminance of a frame or region";
    }

    AnalyzerCapability capabilities() const override {
        return AnalyzerCapability::SingleImage | AnalyzerCapability::RegionOfInterest;
    }

    bool analyze(const ImageFrame& frame) override {
        m_mean = computeMean(frame.pixels(), 0, 0, frame.width(), frame.height());
        return !frame.pixels().isNull();
    }

    bool analyzeRegion(const ImageFrame& frame,
                       const mviewer::domain::Selection& region) override {
        // The shipped example clips `region` to the frame and computes the mean
        // over that rectangle; abbreviated here.
        return analyze(frame);
    }

    std::string resultText() const override {
        return "mean luminance = " + std::to_string(m_mean);
    }

private:
    static double computeMean(const ImageData& img, int x0, int y0, int w, int h);
    double m_mean = 0.0;
};

extern "C" {

MVIEWER_PLUGIN_EXPORT Analyzer* createAnalyzer() {
    return new MeanLuminanceAnalyzer();
}

MVIEWER_PLUGIN_EXPORT void destroyAnalyzer(Analyzer* analyzer) {
    delete analyzer;
}

MVIEWER_PLUGIN_EXPORT const char* pluginName() {
    return "example.mean_luminance";
}

// M14.2: declare the frozen ABI triple so the host can verify compatibility.
MVIEWER_PLUGIN_EXPORT const PluginABI* mviewer_plugin_abi() {
    static const PluginABI abi;  // defaults to {api=1, abi=1, sdk=10000}
    return &abi;
}

MVIEWER_PLUGIN_EXPORT int mviewer_plugin_api_version() {  // legacy fallback
    return MVIEWER_API_VERSION;
}

} // extern "C"
```

---

## Plugin Distribution

### Official Plugins

- Maintained by MViewer core team
- The reference plugins in `plugins/example/` are built by `.\build.ps1 Release` and land next to the MViewer binaries
- The release package itself ships without plugins; the `<exe dir>/plugins` home is created empty on first run

### Community Plugins

- Third-party maintained
- Distributed via GitHub releases
- Installed by copying the library into `<exe dir>/plugins`, or into any directory added to the Plugin Settings search-path list; there is no download-and-update mechanism

### Plugin Registry (Future)

- Central repository of community plugins
- Search and install from within MViewer
- Rating and review system
- Automatic update notifications

---

## Performance Requirements

| Requirement | Target |
| ------------- | -------- |
| Plugin discovery | < 50ms |
| Plugin load | < 100ms |
| Plugin registration (kind probe) | < 200ms |
| Decode plugin overhead | < 1ms per call |
| Plugin crash isolation | Target only: in-process plugins are not isolated, and a memory-corrupting plugin can take the process down (see Plugin Security) |
| Memory overhead per plugin | < 10MB baseline |

### Constraints

- Plugins must not block the UI thread
- Decode plugins must be stateless (thread-safe)
- Plugin calls must have bounded execution time
- Long-running operations (export, batch) must report progress

---

## API Version History

### Version 1 (current, frozen)

- `Analyzer` (`core/analyzer/Analyzer.h`) — analysis algorithms, full frame + region
- `IDecoder` (`core/image/decoder/IDecoder.h`) — image format support
- `IExporter` (`core/export/IExporter.h`) — export / conversion support
- `IImporter` (`core/import/IImporter.h`) — external catalog → `Workspace`
- `ICompareAlgorithm` (`core/compare/ICompareAlgorithm.h`) — compare metrics + optional heatmap
- Frozen ABI descriptor `mviewer_plugin_abi()` (`apiVersion` 1 / `abiVersion` 1 / `sdkVersion` 10000)
- Discovery by directory scan next to the executable; in-process execution only

### Not in the SDK

- Metadata-handler and UI-extension plugin kinds do not exist
- Out-of-process execution, plugin signing and a built-in plugin registry are
  ideas only; none of them is implemented
