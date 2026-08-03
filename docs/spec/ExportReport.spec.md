# Compare Export Report Specification

## Scope

`CompareReportBundle` is the Qt-free core snapshot used to export a trusted
multi-image Compare result. It describes the exact ordered `ImageFrame` list
that was supplied to the builder. The frames are expected to already contain
their per-image adjustments; adjustment values are recorded as provenance and
are not applied by the builder.

## Construction

`buildCompareReportBundle(adjustedImages, referenceIndex, threshold, roi,
adjustments)` emits one `CompareReportPair` for every image except
`referenceIndex`. Each pair stores the target index/path, the reference index
and path (`imageA`), comparability, PSNR, SSIM, threshold-aware full-image
`DifferenceEngine::DiffStats`, and optional ROI stats. A pair is comparable only
when both frames are valid, have identical dimensions, and produce a valid
diff map. Dimension mismatches are retained as explicit `comparable: false`
pairs and do not receive misleading metrics. ROI stats are retained only when
the clipped ROI contains at least one pixel.

The bundle retains a diff heatmap in memory for a later UI or encoder boundary;
binary image data is never written by `toJson()` or `toCsv()`.

The adjustment state has one entry per input image and records brightness,
contrast, gamma, red/blue white-balance gains, rotation, and crop state. If the
caller provides fewer states, missing entries are identity states.

## JSON

JSON contains `images` in input order, `referenceIndex`, `threshold`, `roi`,
`adjustments`, and `targets`. Every target includes `index`, `path`,
`referenceIndex`, `imageA`, `comparable`, `psnr_dB`, `ssim`, `fullDiffStats`,
and `roiDiffStats` (or `null` when no ROI was requested or the pair is not
comparable). For `comparable: false`, all four metric/stat fields are `null`.
Non-finite floating-point values are also serialized as JSON `null`, never as
bare `inf` or `nan`. Windows backslashes, quotes, and control characters are
escaped according to JSON string rules.

## CSV

CSV has one stable 17-column header followed by one 17-column row per target.
It includes the reference and target identity, comparability, PSNR/SSIM, all
full-image stats, and all ROI stats. For an incomparable target, every metric
and statistic cell is empty; missing ROI stats and non-finite values are also
empty fields. Fields containing commas, quotes, or line breaks use standard CSV
quoting with doubled quotes.

## Compatibility

The existing two-image `CompareReport`, `buildCompareReport`, and
`compareDiffImage` APIs remain available and unchanged.
