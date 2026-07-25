# ADR 012 — P0: SelectionModel as Single Source of Truth; Hover Stays Local

**Date:** 2026-07-25
**Status:** Accepted
**Context:** P0 Product Polish — SelectionModel Unification

## Decision

1. **SelectionModel is the sole SSOT** for the app-wide current image and
   selection set. Every consumer (CompareWorkspace, MetadataPanel, Export,
   Batch, Delete, Rename) must derive the current image from SelectionModel,
   not from a local copy. Panels that need to react to image changes must
   connect to `SelectionModel::currentImageChanged` rather than being
   manually pushed by MainWindow.

2. **CompareWorkspace writes back to SelectionModel** when the user locks a
   reference cell (`onFocusRequested`), and when the visible window changes
   (`nextPair` / `prevPair`). This keeps the global current image consistent
   with what the user is actively viewing in Compare.

3. **Hover state stays local.** The transient visual state of hovering over
   a thumbnail or compare cell is a rendering concern of each widget, not
   application state. Adding hover to SelectionModel would add unnecessary
   signal noise and coupling without meaningful benefit for any downstream
   consumer. Focus is a per-widget concern in Qt; we do not unify it.

## Rationale

- The reviewer's concern was that "every widget maintains its own
  selected/hover/current/focus" — the real correctness risk is having
  multiple *current images*, not multiple widgets tracking hover.
- Placing hover in SelectionModel would require every widget to filter out
  signals it doesn't care about. This is a higher cost than the local
  approach with no compensating value.
- CompareWorkspace is the trickiest case: its "current" is really the
  locked reference cell, and writing that back to global SelectionModel is a
  single `setCurrentImage()` call with a trivial guard, not a redesign.

## Consequences

**Positive:**
- Current image is always unique. No more "which panel has which copy?"
- MetadataPanel / AnalysisPanel auto-react to SelectionModel changes without
  MainWindow needing to push every update manually.
- Compare ↔ Browse flow stays consistent: lock a reference image in Compare
  → go back to Browse → Metadata / Analysis / Export all see that same image.

**Negative / Neutral:**
- Two redundant push paths may exist briefly (e.g. MetadataPanel receives
  both `SelectionModel::currentImageChanged` and a direct `setImage()` from
  MainWindow), but these are harmless (idempotent updates).
- Hover continues to be `WA_Hover` / local `m_hoverIdx` per widget — this is
  already the de facto approach and is not a regression.

## Rejected Alternatives

- **Global HoverManager:** would duplicate Qt's built-in hover events;
  rejected as over-engineering.
- **Making CompareWorkspace fully "push" its state without write-back:**
  would mean the app has two current images (the viewer's and Compare's),
  violating the SSOT goal. Write-back is the minimal bidirectional bridge.
