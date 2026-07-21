# M14 — Release Readiness Audit

- **Date:** 2026-07-21
- **Author:** Agent (Hermes review cycle)
- **Scope (per review P0-1):** README, ROADMAP, CHANGELOG, STATUS, LICENSE, SDK, CI, Installer, Release
- **Principle:** No new features. Audit only — generate PASS / FAIL / TODO per item and a consolidated remediation plan.
- **Verdict:** ❌ **NOT READY** for a clean `v1.0.0 RC` tag at audit time. The *engine* is release-grade, but the release metadata and the project-facing docs were internally inconsistent. Five meta-blockers (B1–B5) were found and **resolved in the M14.1 follow-through**; B6 (Plugin ABI versioning) was **resolved in M14.2**.

---

## Executive summary — Meta-blockers

| ID | Severity | Blocker | Where | Status |
|----|----------|---------|-------|--------|
| B1 | FAIL | Version string chaos: `0.11.0` vs `1.0.0` | `README.md`, `release.yml` | ✅ Fixed (M14.1) |
| B2 | FAIL | RAW status self-contradicts (shipped vs deferred) | `CHANGELOG.md`, `STATUS.md`, `ROADMAP_PUBLIC.md` | ✅ Fixed (M14.1) |
| B3 | FAIL | `STATUS.md` frozen at "v0.1 浏览器原型" | `STATUS.md` | ✅ Fixed (M14.1) |
| B4 | FAIL | Duplicate + broken packaging scripts at repo root | `pack_installer.ps1`, `pack_portable.ps1` | ✅ Fixed (M14.1) |
| B5 | WARN | Qt version mismatch README (`6.11`) vs CI (`6.8.0`) | `README.md` vs `ci.yml` | ✅ Fixed (M14.1) |
| B6 | ✅ RESOLVED | SDK lacked `apiVersion`/`sdkVersion`/`abiVersion` | `docs/sdk/PLUGIN_SDK.md` (→ M14.2) | ✅ Done (M14.2) |
| B7 | TODO | Release automation missing SHA256 + auto-changelog | `release.yml` (→ M14.8 / P8) | ⏳ Pending |

**Bottom line:** the code compiles, tests pass, four CI workflows exist, an NSIS installer and a portable zip ship. The audit caught four inconsistent "current version" strings and a self-contradicting RAW status; all were reconciled. The Plugin ABI is now frozen for the v1.x line.

---

## Per-item audit

### 1. README — ✅ PASS (was FAIL)
- **Version:** distribution command now uses `1.0.0` (`README.md`, matches `CMakeLists.txt` / `RELEASE_v1.0.0.md`). Fixed from `0.11.0`.
- **Qt:** Tech Stack now states `Qt 6 Widgets (CI: 6.8.0; local dev: 6.11.1)`, aligned with the CI matrix. Fixed from a bare `Qt 6.11`.
- **Good:** Overview / architecture / feature list consistent with `STATUS` and the review. Link to `docs/roadmap.md` is valid.

### 2. ROADMAP — ✅ PASS (was FAIL)
- `ROADMAP_PUBLIC.md` "Explicitly deferred" previously asserted RAW "doesn't read RAW containers". Updated: basic RAW opening is **shipped (P6)** via `RawDecoder` embedded-JPEG preview; full demosaic remains a post-1.0 libraw enhancement.
- Milestone list and the RC-vs-1.0 framing are coherent.

### 3. CHANGELOG — ✅ PASS (was FAIL)
- M13 entry previously said "RAW decoder remains deferred … `TODO(M7): RAW`". Repaired to state RAW preview shipped in P6; `DecoderRegistry` no longer carries the deferral.
- Version header is `1.0.0` — consistent with `CMakeLists.txt`.
- A new `M14.2` entry records the Plugin ABI freeze under `[Unreleased]`.

### 4. STATUS — ✅ PASS (was FAIL)
- Rewritten as a `v1.0.0 RC` snapshot: positioning, frozen architecture, shipped capabilities, deferred/future items, known gaps, CI matrix, release process. No longer claims "v0.1 浏览器原型" or a static `mviewer_core`.

### 5. LICENSE — ✅ PASS
- MIT, `Copyright (c) 2026 MViewer Contributors`. Matches the CI packaging glob `LICENSE*`. No action.

### 6. SDK — ✅ PASS (exists + ABI frozen in M14.2)
- `docs/sdk/PLUGIN_SDK.md` is thorough: interfaces, C exports, build contract, ABI rules §3, restrictions.
- **ABI frozen (M14.2):** `PluginABI` now carries `apiVersion` / `abiVersion` / `sdkVersion`; the loader enforces it and `pluginabi_tests` proves the gate end-to-end (including `example_analyzer_badabi` with `abiVersion=999`). The review P0-2 requirement is satisfied. `docs/sdk/PLUGIN_ABI.md` is the frozen contract.
- **Coverage:** `plugins/example` (built via `add_subdirectory(plugins/example)`) now ships **Analyzer + Decoder (PPM) + Exporter (PNG/BMP)** reference plugins; loaded by `pluginregistry_tests` / `pluginexamples_tests` and ABI-tested by `pluginabi_tests`. Decoder/Exporter plugin surfaces are live (`DecoderRegistry` + `ExporterRegistry`).
- **M14.3 DONE:** Analyzer/Decoder/Exporter example plugins ship and `pluginexamples_tests` loads all three end-to-end; CI runs `pluginabi_tests` + `pluginexamples_tests`.

### 7. CI — ✅ PASS (mostly) / 🔲 TODO (SHA256, changelog)
- `ci.yml`: gating = Format + Build + Test + Package + clazy; clang-tidy/ASan advisory; nightly fan-out separate. Sound.
- `release.yml`: builds Release, packages via `scripts/package_release.ps1`, uploads `Setup.exe` + portable zip + notes to a GitHub Release. Default dispatch version corrected to `1.0.0` (was `0.11.0`).
- `nightly.yml` and `perf-gate.yml` exist and are meaningful.
- **Minor:** the `ci.yml` package artifact is a raw `build_msvc/bin` zip (CI convenience), not the windeployqt-deployed portable. Acceptable.
- **TODO (M14.8 / P8):** add SHA256 manifest + auto-generated changelog.

### 8. Installer — ✅ PASS (functional) / ✅ Consolidated
- `installer/mviewer.nsi` is a correct, parametric NSIS script (version via `/DVERSION` + `/DVI_VERSION`, output via `/DOUTFILE`, staging via `/DAPP_DIR`). `scripts/package_release.ps1` passes exactly those defines → consistent.
- **Duplicate removed (M14.1):** root `pack_installer.ps1` (passed `/DDEPLOY_DIR`/`/DOUTDIR`/`/DAPPVERSION`, which `installer/mviewer.nsi` does not consume — broken) and `pack_portable.ps1` were deleted; all references now point to `scripts/package_*.ps1`.

### 9. Release — ✅ PASS (docs)
- `docs/release/RELEASE_v1.0.0-rc.md`, `RELEASE_v1.0.0.md`, `RELEASE_CHECKLIST.md` are coherent: RC-vs-real-1.0, packaging, verification gates, known gaps. Version = `1.0.0`. The installer reference was repointed to `scripts/package_release.ps1`.

---

## Consolidated remediation plan (recommended order)

| # | Action | Resolves | Status |
|---|--------|----------|--------|
| 1 | Set version to `1.0.0` in `README.md` and `release.yml` default | B1 | ✅ Done (M14.1) |
| 2 | Reconcile RAW: fix `CHANGELOG.md` M13 note, `ROADMAP_PUBLIC.md`, `STATUS.md` | B2 | ✅ Done (M14.1) |
| 3 | Rewrite `STATUS.md` as `v1.0.0 RC` snapshot | B3 | ✅ Done (M14.1) |
| 4 | Delete `pack_installer.ps1` + `pack_portable.ps1`; repoint refs | B4 | ✅ Done (M14.1) |
| 5 | Align README Qt prerequisite text with CI matrix | B5 | ✅ Done (M14.1) |
| 6 | (M14.2) Add `apiVersion`/`sdkVersion`/`abiVersion` to SDK + ABI compat test | B6 | ✅ Done (M14.2) |
| 7 | (M14.8) `release.yml`: SHA256 manifest + auto-changelog | B7 | ⏳ Pending |

After items 1–5 (M14.1) the audit flips README / ROADMAP / CHANGELOG / STATUS / Installer to **PASS**. B6 was closed by M14.2. B7 remains the only open release-automation item (tracked for M14.8 / P8).

## Sign-off gate

- **Code:** release-grade engine (decode/cache/scheduler/compare/analyze/plugin SDK all present and tested). ✅
- **Release metadata & docs:** ✅ consistent after M14.1 reconciliation.
- **Plugin ABI:** ✅ frozen for v1.x (M14.2).
- **Next:** M14.3 (SDK Example) is **done** — Analyzer/Decoder/Exporter reference plugins ship and `pluginexamples_tests` loads all three end-to-end (CI-verified). Remaining: M14.4 (Dataset Matrix).
