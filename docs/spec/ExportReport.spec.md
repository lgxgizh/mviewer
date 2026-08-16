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

## Single-image Analyze report

`ReportContext` can carry an `AnalysisReport` snapshot for the currently
exported image. `AnalysisReport::analyzerId` is the producer id captured when
the result was published and `resultText` is the plain-text result shown by
the Analysis panel. The UI must read both values from the per-image
`AnalyzerModel` state; the global current analyzer selection must not be used
to relabel an older result. Results loaded from the legacy results-only
persistence format remain valid with an empty `analyzerId`.

`hasAnalysis` is true only when a non-empty result exists. A single-image
report without a result is still a valid report and never uses the old
`no compare data` error payload.

Report export follows the current Analysis request state. While an accepted
analyzer job is `Pending`, the Analysis-panel action is disabled and other
single-image report entry points reject the request with an explicit message,
so retained text from the previous producer cannot be exported as current.
`NoResult` and `Unavailable` are terminal states: export remains available for
the image, but the retained `AnalyzerModel` text is omitted and `hasAnalysis`
is false. Compare exports remain available while a single-image Analysis job
is pending because they use their own snapshot.

Workspace/project persistence stores the producer id beside each saved
analysis result (`analysisAnalyzerId`). Older results-only/workspace files may
omit that field and are still readable with an empty producer id; no current
analyzer selection is used to relabel such legacy text.

### JSON

When no Compare result is present, JSON has this stable shape:

```json
{
  "imagePath": "...",
  "analysis": {
    "analyzerId": "...",
    "resultText": "..."
  }
}
```

`analysis` is `null` when `hasAnalysis` is false. Paths and result text use
JSON escaping for quotes, backslashes, newlines, control characters, and
other delimiters. Compare bundle and legacy Compare contexts continue to use
their established serializers unchanged.

### CSV

Single-image CSV uses the stable header
`imagePath,analyzerId,resultText` and one data row. Empty analyzer/result
cells represent a report without an analysis result. Fields containing
commas, quotes, or line breaks use standard CSV quoting. Compare contexts
continue to use their established headers and rows.

### Markdown and HTML

Markdown renders image paths, analyzer ids, and result text in dynamically sized
fenced code blocks so backticks, headings, newlines, and other user text
cannot break the document structure. HTML renders an Analysis section with
escaped analyzer id and a preformatted, escaped result body. Histogram and
Compare diff PNGs remain worker-encoded base64 assets; no new UI-thread image
recomputation is introduced.
