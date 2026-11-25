# ADR 041: explicit plugin trust and in-process ABI calls

- Status: proposed (B07 r2 review; not implemented until native approval)
- Date: 2026-11-25

## Context

`startupPlugins()` calls `PluginManager::loadDirectory()` on `<exe>/plugins`.
`PluginLoader::loadFromDirectory()` does the same for its callers.
`directory_iterator` plus `is_regular_file()` follows a file symlink, so a
link is mapped. `dlopen` / `LoadLibraryExW` runs `.init_array` or `DllMain`
before `mviewer_plugin_abi()` can be read. That ABI comparison is already a
contract check. It is not, and cannot be, a proof that the file was trusted.

`PluginManager::load(path)` is the single-file entry tests already use.
`Analyzer::registerBuiltins()` registers in-process analyzers and does not
read `plugins/`. `plugins/disabled` is a display-name flag: the plugin stays
loaded. `plugins/searchPaths` is persisted, and Plugin Settings → Rescan is
the existing control that loads the directories the user pointed at. Unload
unregisters factories and does not `dlclose` (ADR 017).

`PluginLoader` already catches some analyzer callbacks. `PluginManager` calls
`pluginName()`, `create*`, `name()`, and `destroy*` with no C++ `try/catch`.
A null `pluginName()` would be passed to `std::string`. A missing `destroy*`
on a decoder, exporter, importer, or compare plugin is currently followed by
a host `delete` of plugin memory.

No Authenticode check exists in this tree. Linux CI cannot evaluate one.
Path-only persistence would map a replaced file whose path was previously
allowed, which still executes bytes that were never authorized.

## Decision

Unknown native modules are not mapped on discovery or startup.

- `scanDirectory` / `scanPluginCandidates` enumerate only direct children whose
  `symlink_status` is a regular file. Symlinks and junctions are omitted.
  The directory itself must be a real directory. A candidate whose canonical
  parent is not that directory is omitted.
- `loadDirectory` and `PluginLoader::loadFromDirectory` return without opening
  a library.
- `load` / `loadPlugin` map one regular file the caller named. They reject a
  symlink or junction before mapping. They do not write QSettings. Tests may
  call them directly. The settings UI does not call them except through
  `commitExplicitTrust` after Yes.
- Startup calls `loadTrusted` with `plugins/trustedSha256`. A candidate is
  hashed and mapped only when its lowercase SHA-256 hex is in that list.
  Hashing reads the file. It does not map it.
- Plugin Settings → Rescan only discovers regular files and records each path
  and SHA-256 in the dialog. It does not map them. Startup, scanning, and
  adding a search path are not consent.
- Trust and load is a separate confirmation. It names the selected paths and
  the SHA-256 of their current bytes, warns that native code runs in this
  process, and defaults to Cancel. Only Yes calls `commitExplicitTrust`.
  A hash is persisted only when that call mapped the file and the bytes still
  match the confirmed hash. An already loaded path is not mapped again and its
  stored hash is not replaced.
- `plugins/disabled` is unchanged.
- Built-in analyzers stay registered in-process.

ABI and callbacks:

- After an explicit map, a null ABI descriptor, a throwing version or ABI
  export, a null `pluginName()`, a null `create*`, or a missing `destroy*`
  is rejected. The host does not `delete` an object the plugin allocated.
- Those calls, and `name()` / `destroy*`, go through `invokePluginCallback` /
  `invokePluginVoid`.
- That `try/catch` stops a C++ exception from leaving the callback. It does
  not stop an access violation, `abort`, or any other non-C++ fault. A loaded
  plugin runs in this process. The catch is not a security sandbox.
- Unload still drops the registry entry and leaves the module mapped (ADR 017).
  That is lifetime behavior, not containment.

## Consequences

Dropping a DLL into `plugins/` no longer executes it on the next launch.
The user authorizes files with Trust and load, not with Rescan. SHA-256
identity is the cross-platform choice available in this tree. It is not a
signature, and a hash collision or a TOCTOU swap between the hash read and
`dlopen` is outside this check. Comparing the hash again after `load` only
decides whether to persist. It does not close that race. Windows reparse
points that `std::filesystem` does not report as a symlink or junction are
not additionally classified here.

`loadDirectory()` no longer loads a directory of plugins. Callers that want
execution use `load(path)`, `commitExplicitTrust`, or `loadTrusted` for hashes
already stored. Existing nonexistent-directory tests still observe an empty
result. A path that is already loaded is not given the hash of replacement
bytes.

## Not in this change

CI workflow files are untouched, including Compare/Viewer GL, Session ZIP,
HDR sanitizer/fuzz iteration, and TSan. `nightly.yml` is not modified.
Complexity caps 800 / 120 / 25 are unchanged. P1-13.3, Workspace, and whole
R-01 stay open. This ADR does not claim those gates were re-run.
