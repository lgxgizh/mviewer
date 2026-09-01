# M59 Color-managed presentation closure (2026-09-02)

## Verdict

Automated M59 closure is implemented in v1.0.16. The focused metadata,
presentation, sequence, high-bit, and Browse regressions are green. The final
release gate passed in two consecutive source-stable `build.ps1 Test` runs.
Physical monitor LUT,
HDR, and mixed-DPI behavior remains **MANUAL/BLOCKED** on this host.

## Required closure answers

1. **What did MViewer do before M59?** It converted source pixels to sRGB for
   display. That was not proof of conversion to the currently attached
   physical monitor profile.
2. **Did ordinary static, LOD, Compare, and FrameSequence paths share a full
   ICC contract?** No. Static/LOD/Compare mostly shared the sRGB conversion,
   while sequence metadata could lose the profile and target-monitor identity
   was implicit.
3. **What did M57 expose?** Frame/page metadata could retain dimensions but
   lose ICC bytes and the canonical color-space label when materialized through
   the sequence path.
4. **What metadata mismatches were fixed?** `MetadataReader` previously used
   packed `QImage::depth()` and a bitwise orientation mapping; `QtDecoder`
   divided packed depth by channels and had a separate mapping. M59 reports
   source bits per channel and uses the exact EXIF 1–8 mapping in all readers.
5. **What is authoritative for orientation?**
   `core/image/QtMetadataSemantics.h::orientationFromTransform` is the single
   mapping from Qt transform flags to EXIF orientation values.
6. **What happens when a source is untagged?** It is explicitly treated as
   sRGB, recorded as no embedded ICC, and remains safe to present.
7. **What happens with malformed source or target ICC?** Malformed/missing
   source ICC falls back to assumed sRGB; malformed/missing target falls back
   to sRGB. No black frame, crash, or source-byte mutation is allowed.
8. **How is the monitor profile obtained?** The UI provider asks Windows
   `GetICMProfileW` for the current `QWindow` device context. Errors,
   unavailable profiles, and non-Windows builds return an sRGB context.
9. **How are profile changes made safe?** `DisplayColorContext` carries a
   fingerprint and generation. Viewer and Compare cancel display work, clear
   display-only caches, and reject queued results whose target key is stale.
10. **What is the cache identity?** `DisplayColorContext::cacheKey()` is
    `fingerprint@generation`; it is carried by tile, LOD, preload, warm, and
    Compare materialization requests. Persistent source caches remain
    target-independent.
11. **Can presentation mutate source bytes?** No. The conversion copies the
    `ImageData`/`QImage`; the M59 test snapshots and compares the source buffer.
12. **What does Pixel Inspector sample?** It samples the source `ImageFrame`,
    never the display raster or a monitor-converted tile.
13. **Are CPU and GPU outputs different?** No. Both paths use the same CPU
    source-to-target conversion; GPU is an upload/blit transport path.
14. **How is high-bit input represented?** Source metadata reports true bits
    per channel (the 16-bit TIFF probe is covered). The display `ImageData`
    remains an explicit 8-bit SDR presentation buffer; analysis/source data is
    not down-converted in place.
15. **Do static and sequence readers agree?** Yes. The shared helper is used by
    full, LOD/region, probe, and sequence reads. Real ICC-bearing AdobeRGB
    JPEG metadata is asserted through both decoder and `FrameSequenceReader`,
    and M57's real GIF/WebP/TIFF plugin matrix remains green for untagged
    sequence sources.
16. **What Browse scale is covered?** The M59 test evaluates 10,000 and
    50,000 immutable entries through the production evaluator, checks the
    metadata/tag/rating/camera/lens/ISO intersection, and drives a real
    asynchronous `ThumbnailPanel` over 660 files with rapid A→B→C intent.
17. **What is the final automated gate?** Two consecutive source-stable
    `powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test` runs
    passed: **128/128 in 742.80 s**, followed by **128/128 in 743.22 s**.
18. **What remains manual?** Physical monitor-profile/LUT verification, HDR
    policy and tone mapping, mixed-DPI monitor moves, compositor screenshots,
    long-session perceived smoothness, and clean target-machine package
    qualification are **MANUAL/BLOCKED** here; no automated claim is made for
    them.

## Evidence

- RFC: `docs/rfc/M59_COLOR_MANAGED_PRESENTATION_CONTRACT.md`
- Phase 0 RED baseline: `docs/review/M59_PHASE0_COLOR_METADATA_BASELINE_2026-09-01.md`
- Focused gates: `m59_phase0_metadata_tests`, `m59_color_managed_tests`,
  `m59_browse_regression_tests`
- Local release command: `build.ps1 Release`
- Final full-gate transcripts: two consecutive `build.ps1 Test` passes,
  128/128 in 742.80 s and 128/128 in 743.22 s.
