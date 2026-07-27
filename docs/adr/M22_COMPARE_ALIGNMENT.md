# ADR-M22.3: Compare Auto-Alignment before Diff Metrics

## Status

Proposed (DRAFT, companion to `docs/rfc/M22_PRODUCT_POLISH.md` §F3)

## Context

Comparing two renders of the same scene that differ by a small (often
integer-pixel) translation yields PSNR/SSIM/diff dominated by mis-registration
rather than real signal error — misleading for algorithm engineers.

## Decision

Add a self-contained `core/compare/Aligner` (Qt-free, unit-testable) that
estimates a 2D translation registering image B to image A via phase
correlation / normalized cross-correlation over a bounded search window on
luminance. The diff-metric path invokes it **only when the `autoAlignBeforeDiff`
preference is enabled**, aligns B to A, then feeds aligned frames to
PSNR/SSIM/diff. Off by default to preserve current deterministic behavior.

## Rationale

- Removes a major source of false diff error for the target users.
- New module, no change to `Analyzer` plugin interface or `DecoderRegistry`.
- Optional + default-off ⇒ zero regression risk for existing workflows.

## Consequences

- ✅ Aligned comparisons reflect real signal difference.
- ✅ Unit-testable on synthetic shifted data.
- ❌ Affine / non-translation warps are out of scope for v1 of the aligner.

## Related

- RFC M22 §F3
- `src/core/analyzer/` (diff metrics consumed), `src/compareworkspace.*`
