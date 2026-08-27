# Release Checklist — P5 Engineering Gate

This checklist ties the review's P5 (Crash / Benchmark / Release) into one
verifiable release gate. Run it before shipping a build.

## 1. Build (must be green)

```powershell
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Release
```

A clean Release build is the baseline. Never ship a build that does not
compile end-to-end.

## 2. Unit + integration tests

```powershell
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test
```

The canonical command builds and runs the complete CTest gate and propagates
any failing CTest exit code. Do not package after a non-zero result. Asset- and
real-display UX sign-off still run separately where required.

For M36, local evidence must also include `m36_display_tests`, ICC/profile
parity, Compare lifetime/session tests, and bounded batch-analysis
cancellation/lifecycle checks. Run both `MVIEWER_GPU=0` and `MVIEWER_GPU=1`
when a GPU-capable host is available; CPU/GPU tile materialization must use
identical display-ready bytes.

## 3. Headless self-test (the one-command release smoke)

```powershell
MViewer.exe --selftest
```

Exits `0` when the core decode → metadata roundtrip succeeds, non-zero on
failure. This is the single command a CI/release job runs to prove the decode
path is intact without a display (`QT_QPA_PLATFORM=offscreen`).

## 4. Benchmark (regression guard)

```powershell
mviewer_bench --smoke      # quick sanity
mviewer_bench --enforce     # full regression assertion
```

The `--enforce` mode is the gating benchmark CTest. A regression past the
accepted threshold must block the release.

## 5. Crash diagnostics (always on)

The Windows handler is installed during startup and writes a minidump plus a
sibling `.txt` report under the per-user AppData location:
`%LOCALAPPDATA%/MViewer/crash-reports/` (the exact path is resolved by Qt's
`AppDataLocation`). No environment variable is required, and crash output is
never written into the working directory. The handler directory is prepared
before SEH registration and its exception path uses fixed-buffer Win32 I/O.

The automated `crashhandler_tests` test launches a child process that raises a
real SEH exception and verifies the `.dmp`/`.txt` pair. Opening the dump in
WinDbg and checking the user-facing location remain physical Windows review
items.

## 6. Package

```powershell
scripts/package_release.ps1   # portable zip + NSIS installer + strict manifest/gate
```

The package script is strict: it fails if NSIS, the exact versioned installer,
Qt SQL/plugin/runtime dependencies, or the packaged `--selftest` is missing.
`dist/MViewer-<ver>-portable.zip` is inspected for development files and
launched offscreen; `dist/MViewer-<ver>-Setup.exe` must carry the same four-part
PE version as the CMake version identity.

## 7. Release metadata (M14.8 / M51)

After packaging, confirm:

```
dist/SHA256SUMS.txt      # one hash line per shipping artifact
dist/RELEASE_NOTES.md    # auto-extracted from CHANGELOG.md
```

Or re-run standalone:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/release_manifest.ps1 -Version 1.0.3
```

Attach both files to the GitHub Release alongside the zip/installer.

## 8. Strict Release Candidate command

Run the complete hard gate from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_release_checklist.ps1 `
  -ReleaseCandidate -Version 1.0.13
```

`-ReleaseCandidate` enables packaging automatically and converts missing
artifacts, installer/version mismatches, checksum errors, dependency gaps,
crash smoke failures, and packaged selftest failures into hard FAIL results.
It also disallows `-SkipBench`; no WARN or SKIP can produce a passing RC report.

## Notes

- P6 (GPU / RAW) is delivered as fallback-safe capability: RAW opens via an
  embedded-preview extractor; the GPU tier is capability-gated and falls back to
  the verified CPU compositor. No external RAW/demosaic or GL dependency was
  added, keeping the release self-contained.
