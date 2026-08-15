# Pixel Inspector Source-Backed Analysis Contract

Status: active (M43, 2026-08-15)

## Scope

`mviewer::core::sampleAnalysisPixel()` and the `ImageData` overload of
`mviewer::core::neighborhoodStats()` provide exact Compare analysis samples
without depending on Qt or on a viewport-sized display image.

## Contract

- Input pixels come from the decoded `ImageData` owned by `ImageFrame`.
- Coordinates are expressed in the adjusted pane's output space.
- Crop is clamped to source bounds, then rotation is interpreted as 0, 90,
  180 or 270 degrees clockwise.
- Point adjustments follow the existing `ImageAdjust` order and rounding:
  brightness, contrast, gamma, then red/blue gains.
- A sample is invalid when the source, crop, or adjusted coordinate is invalid;
  no backing buffer is dereferenced in that case.
- Neighborhood kernels use the same sampler for every position. Out-of-bounds
  positions are skipped, so 1×1/3×3/5×5/7×7 kernels clip at image edges.
- The API performs no full-resolution `QImage` materialization and does not
  apply ICC display conversion. ICC conversion belongs to display materializer
  paths only.
- RAW16 values remain exact only for an identity, untransformed source sample;
  transformed samples use the adjusted 8-bit analysis result.

## Verification

`src/core/test_pixelinspector.cpp` covers identity, crop, all four rotations,
adjustment equivalence, grayscale behavior, all four Inspector kernels, and
edge clipping. `src/test_compare_acceptance.cpp` covers source-vs-LOD
adversarial sampling and LOD replacement stability.
