# M56 Phase 0 — Live-folder Coherence Baseline

Date: 2026-08-29 · Milestone: M56 · Control tree: `555c3a6`

## Scope

This baseline records the live-folder behavior before M56. The control tree
already had asynchronous Browse scans and the M55 bounded thumbnail path, but
it had no committed directory snapshot, mutation-only event channel, or
active-directory watcher independent from the navigation tree.

## Reproduction matrix

| Scenario | Control behavior | User-visible risk |
|---|---|---|
| Open A, expand B in the directory tree, then mutate A | `DirectoryTree` owned one `m_watchedPath`; expanding another node replaced it | A could stop receiving change hints while it remained the active Browse directory |
| File create/delete/overwrite in active A | Tree change re-emitted `directoryChanged`; MainWindow routed it through `ThumbnailPanel::setDirectory()` | A filesystem mutation re-entered the committed navigation boundary and rebuilt the gallery |
| F5 in active A | Tree refresh, panel refresh, ImageList dirtying, and metadata reindex were all coupled | One explicit refresh caused several overlapping refresh paths and selection/scroll churn |
| Overwrite with unchanged size or coarse timestamp | Thumbnail and metadata identities used second-level or insufficient source identity | Old pixels or metadata could survive a same-size/same-second overwrite |
| Sidecar add/change | No separate sidecar delta existed | A sidecar mutation could be treated as a folder-level refresh instead of an item-level metadata update |
| Directory temporarily disappears | No unavailable snapshot state existed | An unavailable directory could be confused with a genuinely empty directory |

## Baseline evidence

The following are source-level findings from the control tree, not synthetic
performance claims:

- `DirectoryTree::watchPath()` removed the previous watcher before registering
  the newly expanded path.
- `DirectoryTree::onDirectoryChanged()` emitted the same navigation signal used
  for a committed A → B transition when the changed path was current.
- `ThumbnailPanel::refresh()` called `setDirectory()`, which cleared the model,
  superseded thumbnail demand, and restarted the asynchronous folder scan.
- `ThumbnailCache` and `MetadataReader` used source identity with insufficient
  timestamp precision for a reliable overwrite boundary.
- `ImageRepository` invalidated only the key generated from the current stat;
  a remembered previous revision could remain live after an overwrite.
- `ImageViewer` had no mutation-only source refresh or path-identity migration
  operation, so a rename/overwrite could desynchronize the open viewer from
  the Browse sequence.

M55's independent Browse observations remain the closest control measurements:
the M55 baseline recorded cold 10k/50k scan-to-stable times of 2,476/2,584 ms
and 11,820/12,158 ms. Those numbers measure the pre-existing full scan path and
are retained for context; M56 does not claim an apples-to-apples latency delta
until a real filesystem watcher workload is isolated.

## Acceptance risks frozen for M56

1. Navigation intent and filesystem mutation must be separate event classes.
2. A notification storm must produce bounded physical scans and latest-wins
   delivery, with a dirty-again pass when a scan overlaps new hints.
3. Added/removed/modified/renamed rows must preserve current selection,
   neighbor navigation, sort/filter state, and the visible scroll anchor.
4. A sidecar mutation must not import the whole directory.
5. Incomplete writes must settle through a bounded stability retry and shutdown
   must not deliver into a destroyed QObject or model.
6. Unavailable must remain distinct from an available empty directory.

The final closure and the focused/full gate results are recorded in
`M56_LIVE_FOLDER_COHERENCE_CLOSURE_2026-08-29.md`.
