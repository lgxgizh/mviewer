# M4 RFC — Review Checklist (pre-implementation gate)

**Reviewed:** 2026-07-17 (second review pass)
**Verdict:** ⛔ NOT approved for implementation yet — 1 blocking gap (Diff async)
+ 2 clarifications required. The RFC structure is sound; these are doc fixes
only. No implementation code may start until the gap is closed in the RFC.

**Method:** every item below was checked against the actual source on disk
(`src/core/compare/*`, `src/domain/CompareSession.h`, `src/compareworkspace.*`,
`src/core/job/Job.h`, `src/core/EventBus.*`), not assumed from the review text.

---

## A. CompareSession lifecycle — is it explicit?  ✅ (clarify in RFC)

**Code reality:**
- `CompareEngine` is owned **by value** inside `CompareWorkspace`
  (`CompareEngine m_engine;` — a QWidget member). Not a raw pointer to a Widget,
  not a dangling ptr. Ownership is: QWidget owns Engine owns frames.
- Frames are `std::vector<shared_ptr<ImageFrame>>` (ref-counted, never raw).
- `CompareEngine::session()` returns a **by-value `CompareSession` copy** — it is
  a *serialization snapshot*, NOT the live state owner. Live sync/selection/
  viewport state lives in `SyncController` / `SelectionController` /
  `ViewportController` (all core, no Qt).

**Finding:** correct per ADR-003/009 (Session = state snapshot; controllers own
live state). The RFC must **state this explicitly** so a future agent does not
"fix" it by making `CompareSession` the live owner (which would couple domain to
mutable engine state). Add a one-line lifecycle note to RFC §2.

## B. ViewState location — is zoom/pan/roi detached from Qt Widget?  ✅ (minor gap)

**Code reality:**
- `SyncController` (core, zero Qt) owns `SyncTransform {scale, offset}` and
  per-cell `CellState`. No `QWidget`, no `QPoint` in the transform math.
- `CompareSession` carries flat `sharedScale/sharedOffsetX/...` snapshot fields.

**Finding:** ViewState is correctly **not** in the View. But there is **no
explicit `CompareViewState` sub-struct** — the user's proposed shape
(`CompareSession.ViewState { zoom, pan, roi, mode }`) is not yet a named type;
the live copy is split across controllers. RFC should either (a) document the
current split as intentional, or (b) propose a future `ViewState` aggregation.
Recommend (a) for M4 (no new type churn) — note it in RFC §2.

## C. Diff/Blink — does it run off the UI thread via JobSystem?  ❌ BLOCKING GAP

**Code reality:**
- `CompareEngine::differenceMap(idx, base)` calls
  `DifferenceEngine::differenceMap(a.pixels(), b.pixels())` **synchronously**,
  on whatever thread invokes it. If the UI calls it directly → **main-thread
  block** on large images. ❌
- `BlinkController` is timer-driven (QTimer in the controller) — that part is
  already off the compute path (just toggles an index). OK.
- `JobSystem` (`src/core/job/Job.h`) and `TaskScheduler` exist and are the
  sanctioned async path.

**Required RFC change:** §3 maturity scope must mandate
`differenceMap` → submitted through `JobSystem`/`TaskScheduler` (Analysis or
Background pool), returning a `DiffResult` via callback/EventBus, never computed
on the UI thread. Add an explicit acceptance line under §6:
`[ ] Diff of two 50 MP images does not block the UI thread (computed via JobSystem).`

**Until this is in the RFC, M4 is not approved for implementation.**

## D. Pixel Inspector data path — does it read ImageFrame, not QImage?  ✅

**Code reality:**
- `inspectPixel(imgX,imgY)` → `ImageFrame::pixels()` → `ImageData` →
  `ImageBuffer::view()` → raw pixel read (`data + y*stride + x*ch`). **No QImage
  in the read path.** ✓
- UI converts `ImageFrame::pixels()` → QImage only at *paint* time via
  `mvcore::toQImage` (`compareworkspace.cpp:25`) — correct boundary.
- `PixelController` is domain-free (core/compare, no Qt) and reusable.

**Finding:** path is future-proof for Tile/RAW. RFC §2 data-flow already states
this; keep. ✅

---

## 6-point checklist (user's explicit list)

1. **CompareSession lifecycle explicit?** → See A. Add 1 line to RFC. ✅-clarify
2. **ViewState detached from Qt Widget?** → See B. Live state in core
   controllers, not View. ✅ (note minor `ViewState` naming gap)
3. **Diff/Blink async via JobSystem?** → See C. **Diff is NOT async → BLOCKING.**
   Blink is timer-driven (OK). Fix RFC §3/§6.
4. **ROI independent data model?** → `domain/Selection.h` is the ROI type;
   analyzers/diff/crop consume `Selection`, never `QRect`. ✅ (RFC §3 already
   extends to `RectangleROI`/`PolygonROI` — note these do not yet exist in code;
   mark as M4 work, not done.)
5. **PixelInspector supports future Tile/RAW?** → See D. Reads `ImageFrame`
   pixels via `ImageBuffer`; Tile/RAW only change the backing store, not the
   accessor. ✅
6. **Command/EventBus correctly integrated?** → `CommandStack`/`CommandRegistry`
   exist (Crop/Compare/Rotate/OpenDirectory/Label/Rename/Delete/ToggleHistogram).
   `EventBus` exists. **Gap:** RFC §3.5 says "panel subscribes to pixelInfo" but
   does not name `EventBus` as the transport, and Diff-result delivery (C) should
   use it. Recommend RFC name `EventBus` explicitly for sync/pixel/diff
   notifications.
7. **Qt types leaked into Core/Domain?** → `CompareEngine.cpp`, `PixelController.h`,
   `DifferenceEngine.h`, `ImageBuffer.h` contain **zero Qt types** ✅. `domain/*`
   is Qt-free ✅. Verified by grep.
8. **Acceptance tests defined?** → `docs/rfc/M4_ACCEPTANCE_SPEC.md` exists
   (A–H, 8 groups). Section H2 (no QWidget decode) is the key regression guard.
   ✅ But add the Diff-async check from C to the spec's section C.

---

## Required doc edits before approval (NO code)

1. **RFC §3** — add: "`differenceMap` must be computed via `JobSystem` /
   `TaskScheduler` (Analysis/Background pool); UI receives `DiffResult` through
   `EventBus`. No synchronous diff on the UI thread."
2. **RFC §6** — add acceptance: "Diff of two 50 MP images is non-blocking
   (JobSystem)."
3. **RFC §2** — add 1-line lifecycle note (A) + name `EventBus` for sync/pixel/
   diff notifications (point 6).
4. **RFC §3** — mark `RectangleROI`/`PolygonROI` as M4 *work* (not yet in code).
5. **M4_ACCEPTANCE_SPEC.md §C** — add the async-diff check.

## Out of scope (unchanged, per user)
❌ Repository/Cache/Scheduler refactor · ❌ Plugin ABI · ❌ Perfetto · ❌ GPU
Render. Infra is sufficient; do not expand.
