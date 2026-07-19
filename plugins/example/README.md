# Example Analyzer Plugin

A complete, minimal MViewer analyzer plugin — the reference implementation for
the [Plugin SDK](../docs/sdk/PLUGIN_SDK.md).

It computes the **mean luminance** of a frame (or a rectangular
`Selection` region) and reports it as a scalar. That's the whole thing: one
interface, three C exports, one build rule.

## Files

- `ExampleAnalyzerPlugin.cpp` — the plugin source.
- `CMakeLists.txt` — builds `example_analyzer.{dll,so,dylib}` and co-locates it
  next to the MViewer binaries so `test_pluginregistry` can load it by name.

## Build

From the MViewer repo root, after a normal build of MViewer:

```powershell
.\build.ps1 Release      # builds mviewer_core + example_analyzer.dll
```

`example_analyzer.dll` lands in `build_msvc/bin/` (same dir as the app + tests).

## What it proves

`ctest pluginregistry_tests` loads this plugin and verifies the full
round-trip:

```
build -> load -> self-register with AnalyzerRegistry
     -> create instance -> analyze() -> resultText()
     -> analyzeRegion() on a 10x10 Selection
```

If that test is green, the Plugin ABI works.

## Use it as your template

Copy this folder, rename `MeanLuminanceAnalyzer`, change `name()` /
`pluginName()` / `capabilities()`, and implement your own `analyze()`. Keep the
three `extern "C"` exports. Rebuild. Drop the `.dll` next to MViewer. It shows
up in the Analysis panel.

## ABI rules (don't skip)

Same compiler + Qt + `c++20` as `mviewer_core`; `mviewer_core` must stay a
SHARED library (one vtable shared host↔plugin). See `docs/sdk/PLUGIN_SDK.md`
§3 for the full contract.
