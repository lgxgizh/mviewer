# M60 Phase 0 — Linked ROI Measurement Baseline

Date: 2026-09-02
Scope: existing ROI infrastructure and acceptance boundaries before the M60
implementation.

## Existing behavior

The repository already had a Qt-free `Selection` value, right-button ROI input
in `RawImageView`, `CompareWorkspace::applySelectionToAll`, synchronized
`CompareEngine` selection, ROI-aware histogram/difference paths,
`CompareSession` persistence, `RGBMeanAnalyzer::analyzeRegion`, and
`computePreviewStatsROI`. This was a useful foundation, not a complete
measurement workflow.

## Gaps identified

- Selection was only delivered after release, so the sibling pane did not show
  a live drag preview.
- No explicit equal-dimension gate prevented ambiguous linked ROIs.
- The statistics UI had no source-RGB ROI table, ratio policy, pixel count, or
  pair delta.
- Format handling and ratio invalidation were not represented by one small
  authoritative result value.
- There was no generation-guarded ROI measurement batch or region-read path for
  source-backed large images.
- Existing mode/session/navigation behavior needed an explicit linked/local
  policy and a clear nonblocking unequal-dimensions message.

## Phase 0 decisions

1. Keep `Selection` as the sole canonical geometry and use half-open source
   coordinates.
2. Make source-domain `computeROIChannelStats` the shared means/ratio path;
   retain the analyzer as a consumer rather than duplicating math in widgets.
3. Separate high-frequency geometry preview from coalesced asynchronous stats.
4. Require identical source dimensions for linked measurement; never perform
   proportional mapping.
5. Treat source/analysis pixels as independent from M59 display color
   management and label the panel `Source RGB`.
6. Use bounded `decodeRegion` for source-backed ROI work where the provider
   supports it, and document the existing 8-bit analysis boundary honestly.

The implementation and final qualification are documented in the M60 RFC and
closure review.
