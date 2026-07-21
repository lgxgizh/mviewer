# Example Plugins (Analyzer / Decoder / Exporter)

Complete, minimal MViewer plugins — the reference implementations for the
[Plugin SDK](../docs/sdk/PLUGIN_SDK.md). They cover all three plugin kinds
supported by the unified loader:

- **Analyzer** — `ExampleAnalyzerPlugin.cpp` computes the mean luminance of a
  frame (or a rectangular `Selection` region).
- **Decoder** — `ExampleDecoderPlugin.cpp` decodes the uncompressed PPM (P6)
  format, demonstrating the `IDecoder` contract.
- **Exporter** — `ExampleExporterPlugin.cpp` writes an `ImageData` to PNG/BMP
  via Qt's `QImage`, demonstrating the `IExporter` contract.

All three share the same C ABI: the frozen `PluginABI` triple
(`mviewer_plugin_abi()`), the legacy `mviewer_plugin_api_version()`, plus
`pluginName()` and a kind-specific `create*` / `destroy*` pair.

## Files

- `ExampleAnalyzerPlugin.cpp` — Analyzer plugin source.
- `ExampleDecoderPlugin.cpp` — Decoder plugin source (PPM).
- `ExampleExporterPlugin.cpp` — Exporter plugin source (PNG/BMP).
- `BadAbiAnalyzerPlugin.cpp` — test-only plugin with `abiVersion=999` (exercises
  the M14.2 freeze gate; never shipped).
- `CMakeLists.txt` — builds `example_analyzer` / `example_decoder` /
  `example_exporter` / `example_analyzer_badabi` and co-locates them next to the
  MViewer binaries.

## Build

From the MViewer repo root, after a normal build of MViewer:

```powershell
.\build.ps1 Release      # builds mviewer_core + the example plugins
```

The `.dll`s land in `build_msvc/bin/` (same dir as the app + tests).

## What it proves

`ctest pluginexamples_tests` loads all three plugins and verifies the full
round-trip for each kind:

```
build -> load -> self-register with the matching registry
     (AnalyzerRegistry / DecoderRegistry / ExporterRegistry)
     -> use: analyze() / decode a PPM / export the result to PNG
```

`ctest pluginregistry_tests` additionally exercises the Analyzer round-trip
(create → analyze → region analyze). If those tests are green, the Plugin ABI
works.

## Use one as your template

Copy the file for the kind you need, rename the implementation class, change
`name()` / `pluginName()`, and implement the interface's methods. Keep the
`extern "C"` exports. Rebuild. Drop the `.dll` next to MViewer. It registers
itself automatically — no code change in MViewer.

## ABI rules (don't skip)

Same compiler + Qt + `c++20` as `mviewer_core`; `mviewer_core` must stay a
SHARED library (one vtable shared host↔plugin). See `docs/sdk/PLUGIN_SDK.md`
§3 for the full contract.
