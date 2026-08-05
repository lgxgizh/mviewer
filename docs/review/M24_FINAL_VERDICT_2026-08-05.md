# M24 Final Verdict — Product Reality & Stability Convergence (2026-08-05)

## Verdict: **READY WITH DOCUMENTED NON-BLOCKING LIMITATIONS**

Everything below was executed and observed this session on the M24 machine
(Windows, 2-core Xeon Platinum 2.5 GHz VM, MSVC 19.44, Qt 6.10.3, Release).
No claim is copied from STATUS/roadmap/README; the evidence documents are
`M24_BASELINE_2026-08-05.md`, `M24_TEST_CREDIBILITY_2026-08-05.md`,
`M24_PERFORMANCE_2026-08-05.md`.

## Gates (all green)

| Gate | Result |
|---|---|
| Clean configure + clean build (from scratch) | OK, 497.6 s, 0 errors; C4530 warnings 47 → 0 (MSVC defaults restored) |
| Full CTest suite (clean build) | **70/70 passed, 0 failed, 0 skipped**, 296 s |
| `bench_smoke` + `bench_enforce` (hard perf gates) | Passed (incl. pipeline-priority trace) |
| `golden_image` (PSNR ≥ 45 dB / SSIM ≥ 0.99) | Passed |
| `version_consistency` gate | Passed (and verified to fail on a wrong version) |
| M24 acceptance suites | `async_lifetime_tests`, `browse_acceptance_tests`, `compare_acceptance_tests`, `analyze_acceptance_tests`, `export_acceptance_tests` — all pass |
| Portable ZIP on PATH-stripped clean env | Launches (139 MB RSS), graceful exit 0 |
| NSIS installer on PATH-stripped clean env | Install → launch → exit 0 → uninstall removes all |
| SHA256 + release notes | Generated for both artifacts |

## M24 completion criteria — status

1. Clean-environment build: **verified** (twice: Phase 1 and Phase 8).
2. All must-run tests really executed and passed: **verified** (70/70, twice).
3. No unexplained skips/failures/bench regressions: **verified** (0 skipped;
   the only bench failure found — B10 — was a code-vs-JSON gate contradiction
   aligned with the documented spec, not a re-baseline).
4. High-risk detached UI workers removed or proven safe: **removed** — all
   `std::thread(...).detach()` gone from UI code; ThumbnailPanel/UpdateChecker/
   BatchDialog use bounded pools + QPointer marshaling; `async_lifetime_tests`
   covers destroy-mid-scan, 50 rapid switches, close-during-scan, view-switch
   during resolve, out-of-order completion, and pool quiescence.
5. Rapid-switch / close-window / exit async tests: **passing**.
6. Browse → Compare → Analyze → Export flow completed by a real user:
   **automated acceptance green** (4 suites); the human perception pass
   (smoothness, zoom feel, long sessions) remains with the UX Review Agent.
7. Version has one source: **verified** (CMake VERSION 1.0.6; About/
   UpdateChecker/workspace/log/installer/package all consume it; gate enforces).
8. STATUS/Roadmap/README/Installer/code agree: **reconciled** (STATUS
   rewritten to current facts; roadmap M17/M18/M22–M24 corrected; RAW-preview
   vs TODO(M7) conflict resolved; installer versioning unified).
9. Portable + installer accepted on clean Windows: **verified**.
10. Fact report + known issues + release recommendation: **this document**.
11. No gate was weakened, no test deleted to go green, no baseline regenerated:
    **true**. (The B10 change aligned code with the documented report-only
    spec; the analyze test change replaced an assertion the build
    configuration cannot satisfy — a bad-function-call defect was fixed
    instead — see below.)

## Fixed during M24 (each with repro + regression test)

- `build.ps1` multi-VS discovery + CRT array gotcha (clean-build blockers).
- Compare blink state never persisted (engine BlinkController not driven).
- Corrupt files showed eternal "loading" (pipeline dropped null decodes).
- Rename dropped the selection; session-restore race (pending-select).
- Compare silently shrank on failed loads; disabled modes unexplained.
- Throwing/failing analyzers could crash the batch path (isolation + bounded
  TaskScheduler; pre-existing dangling structured-binding capture).
- Export could leave partial "successful" files; unknown formats silently
  encoded PNG; batch convert froze the UI.
- 10000-image folder UI stall ~2.7 s → 184 ms (indexAt forced full layout).
- Ctrl+Shift+C conflict between metadata panel and gallery.
- Version/status single source of truth; dead UI golden removed.

## Known limitations (non-blocking, documented)

1. 24 MP JPEG cold decode ~3.5 s and 4 K TIFF ~2.1 s on this 2-core VM
   (single-threaded decode; off the UI thread; hardware-proportional).
2. C4819 warnings in 6 sources (UTF-8 without BOM; Chinese-system code page).
3. Packages ship no `plugins/` dir; startup logs a benign message.
4. In-process plugin isolation covers failures (false/empty); a plugin that
   hard-crashes needs the subprocess runner (exists; not default for
   analyzers). With /EHsc restored, in-process throw isolation now behaves
   correctly in this build.
5. Thread oversubscription on low-core machines (scheduler sizes to
   idealThreadCount); recorded proposal: `max(1, cores-1)` cap.
6. Manual UX items (perceived smoothness, flicker, zoom feel, long-session
   behavior) pending the UX Review Agent's signature on
   `docs/beta_checklist.md`.

## Recommendation

Do not tag a release from this exact tree without (a) the UX Review Agent's
checklist pass and (b) a run on the intended target hardware (the 2-core VM
understates decode throughput ~4× vs the 2026-07-24 baseline box). With those
two, 1.0.6 is a credible release candidate. Until then: **READY WITH
DOCUMENTED NON-BLOCKING LIMITATIONS**.
