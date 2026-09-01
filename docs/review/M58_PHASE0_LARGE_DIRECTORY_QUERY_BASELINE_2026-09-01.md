# M58 Phase 0 — Large-directory query baseline (2026-09-01)

## Scope and evidence boundary

This baseline records the pre-M58 query/index path and the executable evidence
available in this Windows build environment.  The synthetic gate is
`m58_large_directory_query_tests` (10,000 and 50,000 metadata entries).  The
native first-paint, keystroke cadence, and long-session feel require a real
interactive desktop and are therefore explicitly marked **MANUAL/BLOCKED**;
they are not represented as invented numbers.

## Baseline observations

| Area | Before M58 | Evidence/status |
| --- | --- | --- |
| Browse filter state | Individual Qt members; each setter could rebuild immediately | Code audit of `thumbnailpanel_filters.cpp`; automated regression added in M58 |
| Sort/type changes | `setSortMode`, `setSortAscending`, and `setTypeFilter` called `setDirectory()` and rescanned | Reproduced by source audit; fixed in M58 to sort/filter the in-memory listing |
| Gallery filename typing | MainWindow connected `textChanged` directly to `ThumbnailPanel::setFilter` | Source audit; M58 uses a real coalescing debounce |
| Rating/tag lookup | One mutex-protected singleton lookup per candidate row | Source audit; M58 snapshots each store once per query |
| Metadata callbacks | One queued callback per indexed file | Source audit; M58 adds bounded batch callbacks (256 entries) |
| SearchPanel | Search and creation of every `QTableWidgetItem` ran in one UI turn | Source audit; M58 evaluates an immutable index off-thread and fills rows in chunks |
| 10k/50k deterministic search | Not previously covered by a dedicated M58 gate | **NEW** `m58_large_directory_query_tests` |
| Native GUI first paint / typing latency | No trustworthy native recorder in this session | **MANUAL/BLOCKED** — run `m55_interactive_baseline.exe` on a desktop session |
| 100MP / hostile metadata corpus | Not part of this query milestone | **MANUAL/BLOCKED** — retain M47/M57 qualification boundary |

## Reproduction command

```powershell
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test
```

The M58 gate prints `M58 large_query entries=10000` and
`M58 large_query entries=50000` and fails on a result-count or snapshot-size
regression.  Native GUI rows must be recorded separately rather than inferred
from this hermetic core test.
