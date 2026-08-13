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

## 5. Crash diagnostics (opt-in, production only)

Set `MVIEWER_CRASH_DUMP=1` in the shipped environment (or a wrapper script) so
unhandled exceptions write a minidump + `.txt` log to
`%TEMP%/mviewer-crash-reports/`. This is **off** by default and **never** set in
the test suite — it only activates in the field to make crashes diagnosable.

## 6. Package

```powershell
scripts/package_release.ps1   # portable zip + NSIS installer + M14.8 manifest
```

Verify `dist/MViewer-<ver>-portable.zip` launches offscreen with no missing
dependency errors, and `dist/MViewer-<ver>-Setup.exe` installs/uninstalls
cleanly.

## 7. Release metadata (M14.8)

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

## Notes

- P6 (GPU / RAW) is delivered as fallback-safe capability: RAW opens via an
  embedded-preview extractor; the GPU tier is capability-gated and falls back to
  the verified CPU compositor. No external RAW/demosaic or GL dependency was
  added, keeping the release self-contained.
