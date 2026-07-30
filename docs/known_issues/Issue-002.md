# Issue-002 — MainWindow double `setupUi()` orphans ImageViewer

- **Component:** ui/mainwindow
- **Status:** resolved (M22)
- **Root cause:** `MainWindow`'s constructor already calls `setupUi(this)`.
  Four callers (the legacy init paths) called `setupUi()` *again*, which
  constructs a **second** `ImageViewer` and leaks the first one. The offscreen
  workflow test (`workflow_ux_tests`) grabbed whichever viewer Qt happened to
  parent last — non-deterministic — so the Browse→Compare→Zoom→Restore flow
  failed about half the time depending on widget parenting order.
- **Fix:** Remove the redundant `setupUi()` calls; `MainWindow` owns exactly one
  `ImageViewer` created by its ctor. Verified with the offscreen workflow test
  run repeatedly.
- **Regression test:** `src/test_workflow_ux.cpp` (`workflow_ux_tests`) — the
  canonical Browse and Compare workflows are exercised headless; a recurrence of
  the orphaned-viewer flake would fail this test.
- **Related:** ADR-014 (TU-size guardrails) keeps `mainwindow.cpp` small so the
  init path stays reviewable.

## Lesson for future work
Never call `setupUi()` more than once for a widget. If multiple init paths
exist, funnel them through a single ctor-owned setup. Widget-parenting bugs are
order-dependent and invisible under a single-run manual test — only the repeated
offscreen workflow test catches them.
