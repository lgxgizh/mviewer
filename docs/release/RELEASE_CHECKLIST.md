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
cd build_msvc
ctest -R "rawdecode_tests|crashhandler_tests|gputile_tests|raw_metadata_tests|decoder_tests|selftest|export_pipeline_tests|flags_tests|analyzer_registry_tests"
```

All listed suites must pass. (Asset- and display-gated suites such as
`assets_acceptance` / `product_workflow_gate` run separately and need the full
test corpus + a real display.)

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
scripts/package_release.ps1   # portable zip + NSIS installer
```

Verify `dist/MViewer-<ver>-portable.zip` launches offscreen with no missing
dependency errors, and `dist/MViewer-<ver>-Setup.exe` installs/uninstalls
cleanly.

## Notes

- P6 (GPU / RAW) is delivered as fallback-safe capability: RAW opens via an
  embedded-preview extractor; the GPU tier is capability-gated and falls back to
  the verified CPU compositor. No external RAW/demosaic or GL dependency was
  added, keeping the release self-contained.
